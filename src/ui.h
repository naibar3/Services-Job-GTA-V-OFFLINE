// ui.h: Thai text + mouse UI. The game's fonts have no Thai glyphs. Text is shaped and rasterised by GDI+ with the embedded
// TH Sarabun PSK font at the exact size it is shown (hinted, one texel per screen pixel, no scaling = sharp) and drawn by a
// small Direct3D 11 overlay in ScriptHookV's present callback (same method as the current WheelPlugin).
// All layout coordinates are on a 1280x720 canvas.
#pragma once
#include <gdiplus.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <atomic>
#include <mutex>
#include <climits>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#include "font_data.h"

struct Col {
    int r, g, b, a;
    Col(int R = 255, int G = 255, int B = 255, int A = 255) : r(R), g(G), b(B), a(A) {}
};
enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

namespace Ui {

// ---------------------------------------------------------------- GDI+ shaping and rasterising
static ULONG_PTR g_gdipToken;
static Gdiplus::PrivateFontCollection *g_fonts;
static int g_state;   // 0 not started, 1 ready, -1 failed

static bool Init() {
    if (g_state) return g_state > 0;
    Gdiplus::GdiplusStartupInput in;
    if (Gdiplus::GdiplusStartup(&g_gdipToken, &in, nullptr) != Gdiplus::Ok) { g_state = -1; Log("GDI+ failed to start"); return false; }
    g_fonts = new Gdiplus::PrivateFontCollection();
    g_fonts->AddMemoryFont(FONT_REG, (INT)FONT_REG_LEN);
    g_fonts->AddMemoryFont(FONT_BOLD, (INT)FONT_BOLD_LEN);
    if (g_fonts->GetFamilyCount() < 1) { g_state = -1; Log("embedded font did not load"); return false; }
    g_state = 1;
    Log("thai text ready");
    return true;
}

// white glyphs on a transparent background at the size they are shown; the shader tints the white. px = BGRA, straight alpha.
static bool Render(const std::string &utf8, bool bold, int targetH, std::vector<uint8_t> *px, int *outW) {
    int wl = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wl < 2) return false;
    std::wstring w((size_t)wl - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wl);
    Gdiplus::FontFamily fam;
    int found = 0;
    g_fonts->GetFamilies(1, &fam, &found);
    if (!found) return false;
    Gdiplus::FontStyle st = bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
    if (!fam.IsStyleAvailable(st)) st = Gdiplus::FontStyleRegular;
    float ratio = (float)fam.GetLineSpacing(st) / (float)fam.GetEmHeight(st);
    float em = std::max(8.0f, (float)(targetH - 2) / (ratio * 1.15f));
    Gdiplus::Font font(&fam, em, st, Gdiplus::UnitPixel);
    Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
    sf.SetFormatFlags(sf.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    Gdiplus::Bitmap probe(8, 8, PixelFormat32bppARGB);
    Gdiplus::Graphics pg(&probe);
    pg.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::RectF box;
    pg.MeasureString(w.c_str(), (INT)w.size(), &font, Gdiplus::PointF(0.0f, 0.0f), &sf, &box);
    int W = (int)ceilf(box.Width) + 5;
    if (W < 5 || W > 4000) return false;
    Gdiplus::Bitmap bmp(W, targetH, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    Gdiplus::SolidBrush br(Gdiplus::Color(255, 255, 255, 255));
    g.DrawString(w.c_str(), (INT)w.size(), &font, Gdiplus::PointF(2.0f, 1.0f), &sf, &br);
    if (!bold) g.DrawString(w.c_str(), (INT)w.size(), &font, Gdiplus::PointF(2.4f, 1.0f), &sf, &br);   // slightly thicker stems: the font is thin at small sizes
    Gdiplus::BitmapData bd;
    Gdiplus::Rect rc(0, 0, W, targetH);
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return false;
    px->resize((size_t)W * targetH * 4);
    for (int y = 0; y < targetH; y++) memcpy(&(*px)[(size_t)y * W * 4], (const uint8_t *)bd.Scan0 + (ptrdiff_t)y * bd.Stride, (size_t)W * 4);
    bmp.UnlockBits(&bd);
    *outW = W;
    return true;
}

// ---------------------------------------------------------------- texture cache (script thread) + draw list for the render thread
struct Tex { std::vector<uint8_t> px; int w = 0, h = 0, pad = 0; ID3D11ShaderResourceView *srv = nullptr; int lastFrame = 0; };
static std::map<std::string, std::unique_ptr<Tex>> g_tex;
static std::vector<std::pair<std::unique_ptr<Tex>, ULONGLONG>> g_grave;   // evicted textures, freed a few seconds later
static int g_budget = 90;      // new strings rendered per frame, so opening a page never stalls
static int g_frame = 0;

static std::atomic<int> g_bbW{0}, g_bbH{0};   // back buffer size, written by the render thread
static void ScreenSize(int &w, int &h) {
    w = g_bbW; h = g_bbH;
    if (w < 200 || h < 200) { w = 1920; h = 1080; }
}

static void EvictOld() {
    ULONGLONG now = GetTickCount64();
    for (auto it = g_grave.begin(); it != g_grave.end();) {
        if (now - it->second > 5000) {
            if (it->first->srv) { it->first->srv->Release(); it->first->srv = nullptr; }
            it = g_grave.erase(it);
        } else ++it;
    }
    if (g_tex.size() < 700) return;
    for (auto it = g_tex.begin(); it != g_tex.end();) {
        if (g_frame - it->second->lastFrame > 900) { g_grave.emplace_back(std::move(it->second), now); it = g_tex.erase(it); }
        else ++it;
    }
}

static Tex *Texture(const std::string &text, bool bold, int targetH, bool *pending) {
    *pending = false;
    std::string key = (bold ? "B" : "R") + std::to_string(targetH) + ":" + text;
    auto it = g_tex.find(key);
    if (it != g_tex.end()) { it->second->lastFrame = g_frame; return it->second->w > 0 ? it->second.get() : nullptr; }
    if (!Init()) return nullptr;
    if (g_budget <= 0) { *pending = true; return nullptr; }
    g_budget--;
    std::unique_ptr<Tex> t(new Tex());
    if (!Render(text, bold, targetH, &t->px, &t->w)) { t->w = 0; Log("could not render '%s'", text.c_str()); }
    t->h = targetH;
    t->lastFrame = g_frame;
    Tex *raw = t.get();
    g_tex[key] = std::move(t);
    return raw->w > 0 ? raw : nullptr;
}

struct Cmd { Tex *t; float x, y; int align; float r, g, b, a; float w = 0, h = 0; };   // w,h in pixels: stretch (solid rects), 0 = texture size
static std::vector<Cmd> g_cur, g_ready;
static std::mutex g_mx;
static ULONGLONG g_readyAt;

// once per script frame: what was drawn during this frame is handed to the render thread
static void Flush() {
    g_budget = 90;
    g_frame++;
    if ((g_frame & 63) == 0) EvictOld();
    std::lock_guard<std::mutex> l(g_mx);
    g_ready.swap(g_cur);
    g_cur.clear();
    g_readyAt = GetTickCount64();
}

// ---------------------------------------------------------------- Direct3D 11 overlay (render thread)
struct D3D {
    ID3D11Device *dev = nullptr; ID3D11DeviceContext *ctx = nullptr;
    ID3D11VertexShader *vs = nullptr; ID3D11PixelShader *ps = nullptr; ID3D11InputLayout *il = nullptr; ID3D11Buffer *vb = nullptr;
    ID3D11BlendState *bs = nullptr; ID3D11RasterizerState *rs = nullptr; ID3D11SamplerState *ss = nullptr; ID3D11DepthStencilState *ds = nullptr;
};
static D3D g_d3d;
static int g_d3dState;   // 0 not started, 1 ready, -1 failed

static bool D3DInit(IDXGISwapChain *sc) {
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void **)&g_d3d.dev)) || !g_d3d.dev) { g_d3dState = -1; return false; }
    g_d3d.dev->GetImmediateContext(&g_d3d.ctx);
    static const char src[] =
        "struct VI { float2 p : POSITION; float2 uv : TEXCOORD0; float4 c : COLOR0; };\n"
        "struct PI { float4 p : SV_POSITION; float2 uv : TEXCOORD0; float4 c : COLOR0; };\n"
        "PI vs(VI i) { PI o; o.p = float4(i.p, 0, 1); o.uv = i.uv; o.c = i.c; return o; }\n"
        "Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
        "float4 ps(PI i) : SV_Target { return t0.Sample(s0, i.uv) * i.c; }\n";
    ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
    bool ok = SUCCEEDED(D3DCompile(src, sizeof src - 1, nullptr, nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vsb, &err)) && vsb &&
              SUCCEEDED(D3DCompile(src, sizeof src - 1, nullptr, nullptr, nullptr, "ps", "ps_4_0", 0, 0, &psb, &err)) && psb;
    if (err) { Log("overlay shader: %s", (const char *)err->GetBufferPointer()); err->Release(); }
    if (ok) {
        D3D11_INPUT_ELEMENT_DESC il[3] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        ok = SUCCEEDED(g_d3d.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_d3d.vs)) &&
             SUCCEEDED(g_d3d.dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_d3d.ps)) &&
             SUCCEEDED(g_d3d.dev->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_d3d.il));
    }
    if (vsb) vsb->Release();
    if (psb) psb->Release();
    if (ok) {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(float) * 8 * 6; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0].BlendEnable = TRUE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_NONE; r.DepthClipEnable = TRUE;
        D3D11_SAMPLER_DESC s = {};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        s.ComparisonFunc = D3D11_COMPARISON_ALWAYS; s.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable = FALSE; d.StencilEnable = FALSE;
        ok = SUCCEEDED(g_d3d.dev->CreateBuffer(&bd, nullptr, &g_d3d.vb)) && SUCCEEDED(g_d3d.dev->CreateBlendState(&b, &g_d3d.bs)) &&
             SUCCEEDED(g_d3d.dev->CreateRasterizerState(&r, &g_d3d.rs)) && SUCCEEDED(g_d3d.dev->CreateSamplerState(&s, &g_d3d.ss)) &&
             SUCCEEDED(g_d3d.dev->CreateDepthStencilState(&d, &g_d3d.ds));
    }
    g_d3dState = ok ? 1 : -1;
    Log(ok ? "overlay ready" : "overlay: Direct3D setup failed, Thai text stays off");
    return ok;
}

// ScriptHookV calls this right before Present (render thread; no natives allowed here)
static void OnPresent(void *p) {
    IDXGISwapChain *sc = (IDXGISwapChain *)p;
    if (!sc || g_d3dState < 0) return;
    DXGI_SWAP_CHAIN_DESC sd;
    if (SUCCEEDED(sc->GetDesc(&sd))) { g_bbW = (int)sd.BufferDesc.Width; g_bbH = (int)sd.BufferDesc.Height; }
    std::vector<Cmd> cmds;
    { std::lock_guard<std::mutex> l(g_mx); if (GetTickCount64() - g_readyAt < 120) cmds = g_ready; }
    if (cmds.empty()) return;
    if (g_d3dState == 0 && !D3DInit(sc)) return;
    const float bw = (float)g_bbW, bh = (float)g_bbH;
    if (bw < 64 || bh < 64) return;
    ID3D11DeviceContext *c = g_d3d.ctx;
    ID3D11Texture2D *bb = nullptr;
    ID3D11RenderTargetView *rtv = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)) || !bb) return;
    g_d3d.dev->CreateRenderTargetView(bb, nullptr, &rtv);
    bb->Release();
    if (!rtv) return;

    // save the game's pipeline state, draw, restore it
    ID3D11RenderTargetView *oRtv[1] = {}; ID3D11DepthStencilView *oDsv = nullptr;
    c->OMGetRenderTargets(1, oRtv, &oDsv);
    UINT nvp = 16; D3D11_VIEWPORT oVp[16] = {};
    c->RSGetViewports(&nvp, oVp);
    ID3D11RasterizerState *oRs = nullptr; c->RSGetState(&oRs);
    ID3D11BlendState *oBs = nullptr; FLOAT oBf[4] = {}; UINT oMask = 0; c->OMGetBlendState(&oBs, oBf, &oMask);
    ID3D11DepthStencilState *oDs = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDs, &oRef);
    ID3D11VertexShader *oVs = nullptr; c->VSGetShader(&oVs, nullptr, nullptr);
    ID3D11PixelShader *oPs = nullptr; c->PSGetShader(&oPs, nullptr, nullptr);
    ID3D11GeometryShader *oGs = nullptr; c->GSGetShader(&oGs, nullptr, nullptr);
    ID3D11HullShader *oHs = nullptr; c->HSGetShader(&oHs, nullptr, nullptr);
    ID3D11DomainShader *oDms = nullptr; c->DSGetShader(&oDms, nullptr, nullptr);
    ID3D11InputLayout *oIl = nullptr; c->IAGetInputLayout(&oIl);
    D3D11_PRIMITIVE_TOPOLOGY oTop; c->IAGetPrimitiveTopology(&oTop);
    ID3D11Buffer *oVb = nullptr; UINT oStr = 0, oOff = 0; c->IAGetVertexBuffers(0, 1, &oVb, &oStr, &oOff);
    ID3D11ShaderResourceView *oSrv = nullptr; c->PSGetShaderResources(0, 1, &oSrv);
    ID3D11SamplerState *oSs = nullptr; c->PSGetSamplers(0, 1, &oSs);

    D3D11_VIEWPORT vp = {0.0f, 0.0f, bw, bh, 0.0f, 1.0f};
    c->OMSetRenderTargets(1, &rtv, nullptr);
    c->RSSetViewports(1, &vp);
    c->RSSetState(g_d3d.rs);
    const FLOAT bf[4] = {0, 0, 0, 0};
    c->OMSetBlendState(g_d3d.bs, bf, 0xffffffff);
    c->OMSetDepthStencilState(g_d3d.ds, 0);
    c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->VSSetShader(g_d3d.vs, nullptr, 0);
    c->PSSetShader(g_d3d.ps, nullptr, 0);
    c->IASetInputLayout(g_d3d.il);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(float) * 8, off = 0;
    c->IASetVertexBuffers(0, 1, &g_d3d.vb, &stride, &off);
    c->PSSetSamplers(0, 1, &g_d3d.ss);

    for (const Cmd &k : cmds) {
        Tex *t = k.t;
        if (!t || t->w <= 0) continue;
        if (!t->srv) {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = (UINT)t->w; td.Height = (UINT)t->h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sr = {t->px.data(), (UINT)t->w * 4, 0};
            ID3D11Texture2D *tex = nullptr;
            if (SUCCEEDED(g_d3d.dev->CreateTexture2D(&td, &sr, &tex)) && tex) { g_d3d.dev->CreateShaderResourceView(tex, nullptr, &t->srv); tex->Release(); }
            if (!t->srv) { t->w = 0; continue; }
        }
        float qw = k.w > 0 ? k.w : (float)t->w, qh = k.h > 0 ? k.h : (float)t->h;
        float left = k.align == AL_CENTER ? k.x * bw - qw * 0.5f : k.align == AL_RIGHT ? k.x * bw - qw : k.x * bw;
        left = floorf(left + 0.5f);
        float top = floorf(k.y * bh + 0.5f);   // y = top of the box
        float x0 = left / bw * 2.0f - 1.0f, x1 = (left + qw) / bw * 2.0f - 1.0f;
        float y0 = 1.0f - top / bh * 2.0f, y1 = 1.0f - (top + qh) / bh * 2.0f;
        const float v[6][8] = {
            {x0, y0, 0, 0, k.r, k.g, k.b, k.a}, {x1, y0, 1, 0, k.r, k.g, k.b, k.a}, {x0, y1, 0, 1, k.r, k.g, k.b, k.a},
            {x1, y0, 1, 0, k.r, k.g, k.b, k.a}, {x1, y1, 1, 1, k.r, k.g, k.b, k.a}, {x0, y1, 0, 1, k.r, k.g, k.b, k.a}};
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(c->Map(g_d3d.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) continue;
        memcpy(ms.pData, v, sizeof v);
        c->Unmap(g_d3d.vb, 0);
        c->PSSetShaderResources(0, 1, &t->srv);
        c->Draw(6, 0);
    }

    c->OMSetRenderTargets(1, oRtv, oDsv);
    c->RSSetViewports(nvp, oVp);
    c->RSSetState(oRs);
    c->OMSetBlendState(oBs, oBf, oMask);
    c->OMSetDepthStencilState(oDs, oRef);
    c->VSSetShader(oVs, nullptr, 0); c->PSSetShader(oPs, nullptr, 0);
    c->GSSetShader(oGs, nullptr, 0); c->HSSetShader(oHs, nullptr, 0); c->DSSetShader(oDms, nullptr, 0);
    c->IASetInputLayout(oIl);
    c->IASetPrimitiveTopology(oTop);
    c->IASetVertexBuffers(0, 1, &oVb, &oStr, &oOff);
    c->PSSetShaderResources(0, 1, &oSrv);
    c->PSSetSamplers(0, 1, &oSs);
    if (oRtv[0]) oRtv[0]->Release();
    for (IUnknown *u : {(IUnknown *)oDsv, (IUnknown *)oRs, (IUnknown *)oBs, (IUnknown *)oDs, (IUnknown *)oVs, (IUnknown *)oPs, (IUnknown *)oGs,
                        (IUnknown *)oHs, (IUnknown *)oDms, (IUnknown *)oIl, (IUnknown *)oVb, (IUnknown *)oSrv, (IUnknown *)oSs})
        if (u) u->Release();
    rtv->Release();
}

// ---------------------------------------------------------------- text API (1280x720 canvas)
static const float kLine = 1.5f;   // line height as a multiple of the nominal text height

static int TargetH(float h) {
    int sw, sh;
    ScreenSize(sw, sh);
    return std::max(14, (int)(h * kLine / 720.0f * (float)sh + 0.5f) & ~1);
}

static float TextW(const std::string &s, float h, bool bold = false) {
    if (s.empty()) return 0.0f;
    int sw, sh;
    ScreenSize(sw, sh);
    bool pending;
    Tex *t = Texture(s, bold, TargetH(h), &pending);
    return t ? (float)t->w * 1280.0f / (float)sw : 0.0f;
}

// h = nominal text height on the 720p canvas (about 14-24); y = top of the line box
static void Text(const std::string &s, float x, float y, float h, Col c, int align = AL_LEFT, bool bold = false) {
    if (s.empty()) return;
    bool pending;
    Tex *t = Texture(s, bold, TargetH(h), &pending);
    if (!t) return;
    if (g_cur.size() < 1800) g_cur.push_back({t, x / 1280.0f, y / 720.0f, align, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f});
}

// ---------------------------------------------------------------- solid rectangles (drawn by the same overlay, so draw order is call order)
static Tex g_solid;
static void Rect(float x, float y, float w, float h, Col c) {
    if (g_solid.px.empty()) { g_solid.px = {255, 255, 255, 255}; g_solid.w = 1; g_solid.h = 1; }
    int sw, sh;
    ScreenSize(sw, sh);
    if (g_cur.size() < 1800)
        g_cur.push_back({&g_solid, x / 1280.0f, y / 720.0f, AL_LEFT, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f,
                         w * (float)sw / 1280.0f, h * (float)sh / 720.0f});
}

// ---------------------------------------------------------------- shapes: glass panels, glowing pills, bars, icons (rasterised by GDI+)
enum { SH_PANEL, SH_ROUND, SH_BAR, SH_CURSOR, SH_PIN, SH_WARN, SH_PAGER, SH_DOT, SH_SPARK };
struct ShapeSpec { int kind = 0, w = 0, h = 0, radius = 0, glow = 0, q = 0; Col c1, c2, fill; };

static int GlowPad() {
    int sw, sh;
    ScreenSize(sw, sh);
    return std::max(5, (int)(sh / 1080.0f * 12.0f + 0.5f));
}

static Gdiplus::Color GC(Col c, int a = -1) { return Gdiplus::Color((BYTE)(a < 0 ? c.a : a), (BYTE)c.r, (BYTE)c.g, (BYTE)c.b); }
static Col Mix(Col a, Col b, float t) { return Col((int)Lerp((float)a.r, (float)b.r, t), (int)Lerp((float)a.g, (float)b.g, t), (int)Lerp((float)a.b, (float)b.b, t), (int)Lerp((float)a.a, (float)b.a, t)); }
static Col Scale(Col c, float k, int a = -1) { return Col(std::min(255, (int)(c.r * k)), std::min(255, (int)(c.g * k)), std::min(255, (int)(c.b * k)), a < 0 ? c.a : a); }

static void RoundPath(Gdiplus::GraphicsPath &p, float x, float y, float w, float h, float r) {
    float d = std::min(r * 2.0f, std::min(w, h));
    if (d < 1.0f) { p.AddRectangle(Gdiplus::RectF(x, y, w, h)); return; }
    p.AddArc(x, y, d, d, 180.0f, 90.0f);
    p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

// soft outer glow made of fading strokes around a rounded rectangle
static void GlowStroke(Gdiplus::Graphics &g, float x, float y, float w, float h, float r, int pad, Col a, Col b, float strength) {
    for (int i = pad; i >= 1; i--) {
        float f = 1.0f - (float)(i - 1) / (float)pad;
        int alpha = (int)(strength * 90.0f * f * f);
        if (alpha < 2) continue;
        Gdiplus::GraphicsPath path;
        RoundPath(path, x - i, y - i, w + 2 * i, h + 2 * i, r + i);
        Gdiplus::LinearGradientBrush lg(Gdiplus::PointF(x, y), Gdiplus::PointF(x + w, y + h), GC(a, alpha), GC(b, alpha));
        Gdiplus::Pen pen(&lg, 1.6f);
        g.DrawPath(&pen, &path);
    }
}

static bool RenderShape(const ShapeSpec &s, std::vector<uint8_t> *px, int *outW, int *outH, int *outPad) {
    int pad = (s.kind == SH_PANEL || (s.kind == SH_ROUND && s.glow > 0)) ? GlowPad() : (s.kind == SH_BAR ? GlowPad() / 2 + 1 : 2);
    int W = s.w + pad * 2, H = s.h + pad * 2;
    if (W < 4 || H < 4 || W > 3000 || H > 2200) return false;
    Gdiplus::Bitmap bmp(W, H, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    float x = (float)pad, y = (float)pad, w = (float)s.w, h = (float)s.h, r = (float)s.radius;

    switch (s.kind) {
        case SH_PANEL: {
            GlowStroke(g, x, y, w, h, r, pad, s.c1, s.c2, 1.0f);
            Gdiplus::GraphicsPath path;
            RoundPath(path, x, y, w, h, r);
            Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(x, y), Gdiplus::PointF(x, y + h), GC(Scale(s.fill, 1.5f)), GC(Scale(s.fill, 0.8f)));
            g.FillPath(&fill, &path);
            Gdiplus::LinearGradientBrush sheen(Gdiplus::PointF(x, y), Gdiplus::PointF(x + w * 0.9f, y + h * 0.6f), Gdiplus::Color(26, 255, 255, 255), Gdiplus::Color(0, 255, 255, 255));
            g.FillPath(&sheen, &path);
            Gdiplus::LinearGradientBrush lg(Gdiplus::PointF(x, y), Gdiplus::PointF(x + w, y + h), GC(s.c1, 255), GC(s.c2, 255));
            Gdiplus::Pen border(&lg, 2.4f);
            g.DrawPath(&border, &path);
            Gdiplus::GraphicsPath inner;
            RoundPath(inner, x + 4, y + 4, w - 8, h - 8, std::max(2.0f, r - 4));
            Gdiplus::Pen ip(Gdiplus::Color(34, 255, 255, 255), 1.0f);
            g.DrawPath(&ip, &inner);
            break;
        }
        case SH_ROUND: {
            if (s.glow > 0) GlowStroke(g, x, y, w, h, r, pad, s.c2, s.c2, s.glow >= 2 ? 1.0f : 0.55f);
            Gdiplus::GraphicsPath path;
            RoundPath(path, x, y, w, h, r);
            Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(x, y), Gdiplus::PointF(x, y + h), GC(Scale(s.fill, 1.35f)), GC(Scale(s.fill, 0.85f)));
            g.FillPath(&fill, &path);
            Gdiplus::Pen border(GC(s.c2), s.glow >= 2 ? 2.2f : 1.6f);
            g.DrawPath(&border, &path);
            if (s.glow >= 2) {
                Gdiplus::GraphicsPath hi;
                RoundPath(hi, x + 3, y + 3, w - 6, std::max(2.0f, h * 0.42f), std::max(2.0f, r - 3));
                Gdiplus::SolidBrush hb(Gdiplus::Color(40, 255, 255, 255));
                g.FillPath(&hb, &hi);
            }
            break;
        }
        case SH_BAR: {   // track only; the fill is drawn per frame by Bar() so it moves smoothly
            Gdiplus::GraphicsPath track;
            RoundPath(track, x, y, w, h, h / 2.0f);
            Gdiplus::SolidBrush tb(Gdiplus::Color(130, 6, 14, 24));
            g.FillPath(&tb, &track);
            Gdiplus::Pen bp(GC(s.c2, 170), 1.6f);
            g.DrawPath(&bp, &track);
            break;
        }
        case SH_DOT: {
            Gdiplus::SolidBrush b(GC(s.c1, 255));
            g.FillEllipse(&b, x, y, w, h);
            break;
        }
        case SH_SPARK: {   // white core with a soft halo
            for (int i = 8; i >= 1; i--) {
                float f = (float)i / 8.0f;
                int alpha = i == 1 ? 255 : (int)(10 + 55 * (1.0f - f));
                Gdiplus::SolidBrush b(Gdiplus::Color((BYTE)alpha, 255, 255, 255));
                float rw = w * f * 0.5f, rh = h * f * 0.5f;
                g.FillEllipse(&b, x + w / 2.0f - rw, y + h / 2.0f - rh, rw * 2, rh * 2);
            }
            break;
        }        case SH_CURSOR: {
            Gdiplus::PointF pts[7] = {{x + 1, y + 1}, {x + 1, y + h * 0.82f}, {x + w * 0.24f, y + h * 0.62f}, {x + w * 0.42f, y + h * 0.98f},
                                      {x + w * 0.58f, y + h * 0.90f}, {x + w * 0.40f, y + h * 0.56f}, {x + w * 0.72f, y + h * 0.56f}};
            Gdiplus::SolidBrush wb(Gdiplus::Color(255, 255, 255, 255));
            g.FillPolygon(&wb, pts, 7);
            Gdiplus::Pen op(Gdiplus::Color(255, 10, 20, 30), 1.6f);
            g.DrawPolygon(&op, pts, 7);
            break;
        }
        case SH_PIN: {
            Gdiplus::Pen pen(GC(s.c1), std::max(1.6f, w * 0.09f));
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            Gdiplus::GraphicsPath p;
            float cx = x + w / 2.0f, cy = y + h * 0.38f, rr = w * 0.30f;
            p.AddArc(cx - rr, cy - rr, rr * 2, rr * 2, 150.0f, 240.0f);
            p.AddLine(cx + rr * 0.87f, cy + rr * 0.5f, cx, y + h * 0.93f);
            p.AddLine(cx, y + h * 0.93f, cx - rr * 0.87f, cy + rr * 0.5f);
            g.DrawPath(&pen, &p);
            g.DrawEllipse(&pen, cx - rr * 0.38f, cy - rr * 0.38f, rr * 0.76f, rr * 0.76f);
            break;
        }
        case SH_WARN: {
            Gdiplus::Pen pen(GC(s.c1), std::max(1.8f, w * 0.09f));
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            g.DrawEllipse(&pen, x + 1, y + 1, w - 2, h - 2);
            g.DrawLine(&pen, x + w / 2.0f, y + h * 0.26f, x + w / 2.0f, y + h * 0.58f);
            Gdiplus::SolidBrush b(GC(s.c1));
            g.FillEllipse(&b, x + w / 2.0f - w * 0.06f, y + h * 0.69f, w * 0.12f, w * 0.12f);
            break;
        }
        case SH_PAGER: {
            Gdiplus::Pen pen(GC(s.c1), std::max(1.8f, w * 0.08f));
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            Gdiplus::GraphicsPath body;
            RoundPath(body, x + w * 0.2f, y + h * 0.24f, w * 0.6f, h * 0.72f, w * 0.10f);
            g.DrawPath(&pen, &body);
            g.DrawRectangle(&pen, x + w * 0.32f, y + h * 0.36f, w * 0.36f, h * 0.22f);
            g.DrawLine(&pen, x + w * 0.34f, y + h * 0.74f, x + w * 0.66f, y + h * 0.74f);
            g.DrawLine(&pen, x + w * 0.34f, y + h * 0.24f, x + w * 0.34f, y + h * 0.04f);
            break;
        }
    }

    Gdiplus::BitmapData bd;
    Gdiplus::Rect rc(0, 0, W, H);
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return false;
    px->resize((size_t)W * H * 4);
    for (int yy = 0; yy < H; yy++) memcpy(&(*px)[(size_t)yy * W * 4], (const uint8_t *)bd.Scan0 + (ptrdiff_t)yy * bd.Stride, (size_t)W * 4);
    bmp.UnlockBits(&bd);
    *outW = W; *outH = H; *outPad = pad;
    return true;
}

static Tex *ShapeTexture(const ShapeSpec &s) {
    std::string key = Fmt("S%d:%d:%d:%d:%d:%d|%d,%d,%d,%d|%d,%d,%d,%d|%d,%d,%d,%d", s.kind, s.w, s.h, s.radius, s.glow, s.q,
                          s.c1.r, s.c1.g, s.c1.b, s.c1.a, s.c2.r, s.c2.g, s.c2.b, s.c2.a, s.fill.r, s.fill.g, s.fill.b, s.fill.a);
    auto it = g_tex.find(key);
    if (it != g_tex.end()) { it->second->lastFrame = g_frame; return it->second->w > 0 ? it->second.get() : nullptr; }
    if (!Init()) return nullptr;
    if (g_budget <= 0) return nullptr;
    g_budget--;
    std::unique_ptr<Tex> t(new Tex());
    int w = 0, h = 0, pad = 0;
    if (!RenderShape(s, &t->px, &w, &h, &pad)) { t->w = 0; }
    else { t->w = w; t->h = h; t->pad = pad; }
    t->lastFrame = g_frame;
    Tex *raw = t.get();
    g_tex[key] = std::move(t);
    return raw->w > 0 ? raw : nullptr;
}

// draws a shape whose logical rectangle starts at canvas (x,y); the glow margin reaches outside it
static void DrawShape(ShapeSpec s, float x, float y, float w, float h, float radius) {
    int sw, sh;
    ScreenSize(sw, sh);
    s.w = std::max(4, (int)(w * sw / 1280.0f + 0.5f));
    s.h = std::max(4, (int)(h * sh / 720.0f + 0.5f));
    s.radius = std::max(2, (int)(radius * sh / 720.0f + 0.5f));
    Tex *t = ShapeTexture(s);
    if (!t) return;
    if (g_cur.size() < 1800) g_cur.push_back({t, x / 1280.0f - (float)t->pad / (float)sw, y / 720.0f - (float)t->pad / (float)sh, AL_LEFT, 1, 1, 1, 1});
}

static const Col kCyan(0, 225, 255), kMagenta(255, 70, 205), kGlass(14, 26, 42, 232);

// the big window: dark glass, neon gradient frame (a -> b)
static void Panel(float x, float y, float w, float h, Col a = kCyan, Col b = kMagenta, float radius = 16) {
    ShapeSpec s;
    s.kind = SH_PANEL; s.c1 = a; s.c2 = b; s.fill = kGlass;
    DrawShape(s, x, y, w, h, radius);
}
// generic rounded box: fill, border colour, glow 0/1/2
static void Glass(float x, float y, float w, float h, Col fill, Col border, int glow = 0, float radius = 9) {
    ShapeSpec s;
    s.kind = SH_ROUND; s.fill = fill; s.c2 = border; s.glow = glow;
    DrawShape(s, x, y, w, h, radius);
}
// progress bar with a glowing end. Track = one cached texture; the fill is drawn every frame from thin gradient slices,
// so it moves smoothly (no stepping) at any fraction.
static void DrawDot(float x, float y, float d, Col c) {
    ShapeSpec s;
    s.kind = SH_DOT; s.c1 = c;
    DrawShape(s, x, y, d, d, 1);
}
static void Bar(float x, float y, float w, float h, float frac, Col c1, Col c2) {
    ShapeSpec track;
    track.kind = SH_BAR; track.c1 = c1; track.c2 = c2;
    DrawShape(track, x, y, w, h, h / 2.0f);
    frac = Clamp(frac, 0.0f, 1.0f);
    float inset = 2.0f, hh = h - 2 * inset, r = hh / 2.0f;
    float fw = (w - 2 * inset) * frac;
    if (fw < hh * 0.5f) return;
    fw = std::max(fw, hh);
    float bodyStart = x + inset + r, bodyEnd = x + inset + fw - r;
    DrawDot(x + inset, y + inset, hh, c1);
    int n = std::max(1, (int)((bodyEnd - bodyStart) / 3.0f));
    float segW = (bodyEnd - bodyStart) / (float)n;
    for (int i = 0; i < n; i++) {
        float t = ((bodyStart + i * segW) - x) / w;
        Rect(bodyStart + i * segW, y + inset, segW + 0.7f, hh, Mix(c1, c2, Clamp(t, 0.0f, 1.0f)));
    }
    float te = Clamp(std::floor(((bodyEnd - x) / w) * 8.0f + 0.5f) / 8.0f, 0.0f, 1.0f);   // quantised: keeps the cap texture count small
    DrawDot(x + inset + fw - hh, y + inset, hh, Mix(c1, c2, te));
    Rect(bodyStart, y + inset + hh * 0.12f, std::max(0.0f, bodyEnd - bodyStart), hh * 0.26f, Col(255, 255, 255, 55));
    float sd = hh * 2.4f;
    ShapeSpec sp;
    sp.kind = SH_SPARK;
    DrawShape(sp, x + inset + fw - r - sd / 2.0f, y + h / 2.0f - sd / 2.0f, sd, sd, 1);
}enum { IC_PIN, IC_WARN, IC_PAGER };
static void Icon(int kind, float x, float y, float size, Col c) {
    ShapeSpec s;
    s.kind = kind == IC_PIN ? SH_PIN : kind == IC_WARN ? SH_WARN : SH_PAGER;
    s.c1 = c;
    DrawShape(s, x, y, size, size, 1);
}

// ---------------------------------------------------------------- mouse
static float MX = -1000, MY = -1000;
static bool Click = false;
static int Wheel = 0;

static bool Hit(float x, float y, float w, float h) { return MX >= x && MX <= x + w && MY >= y && MY <= y + h; }
static void Beep(const char *name, const char *set) { AUDIO::PLAY_SOUND_FRONTEND(-1, name, set, TRUE); }

// call every frame while a menu is open: cursor on + read position/click/wheel
static void MouseFrame() {
    PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
    HUD::SET_MOUSE_CURSOR_THIS_FRAME();
    MX = PAD::GET_DISABLED_CONTROL_NORMAL(0, 239) * 1280.0f;
    MY = PAD::GET_DISABLED_CONTROL_NORMAL(0, 240) * 720.0f;
    Click = PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 237) != 0;
    Wheel = 0;
    if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 241)) Wheel = -1;
    if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 242)) Wheel = 1;
}
static void MouseOff() { Click = false; Wheel = 0; MX = MY = -1000.0f; }

// the overlay covers the game's own cursor, so draw one on top (last thing drawn while a menu is open)
static void Cursor() {
    if (MX < -500) return;
    ShapeSpec s;
    s.kind = SH_CURSOR;
    DrawShape(s, MX, MY, 15, 22, 1);
}

// neon button; returns true on click
static bool Button(float x, float y, float w, float h, const std::string &label, Col accent, bool enabled = true) {
    bool hover = enabled && Hit(x, y, w, h);
    Col fill = !enabled ? Col(40, 44, 56, 200) : hover ? Scale(accent, 0.62f, 240) : Scale(accent, 0.24f, 230);
    Glass(x, y, w, h, fill, enabled ? accent : Col(80, 86, 100), enabled ? (hover ? 2 : 1) : 0, std::min(9.0f, h / 2.0f));
    Col tc = !enabled ? Col(120, 126, 140) : Col(255, 255, 255);
    Text(label, x + w / 2.0f, y + (h - 17.0f * kLine) / 2.0f + 1.0f, 17.0f, tc, AL_CENTER, true);
    if (hover && Click) { Beep("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); return true; }
    return false;
}

// switch pill (ON green / OFF red)
static void Toggle(float x, float y, float w, float h, bool on, const std::string &onText, const std::string &offText) {
    Glass(x, y, w, h, on ? Col(24, 170, 70, 240) : Col(100, 36, 46, 235), on ? Col(130, 255, 160) : Col(255, 95, 105), on ? 2 : 1, h / 2.0f);
    Text(on ? onText : offText, x + w / 2.0f, y + (h - 16.0f * kLine) / 2.0f + 1.0f, 16.0f, Col(), AL_CENTER, true);
}

// key cap such as [ Y ]; returns its width
static float Badge(float x, float y, const std::string &label, Col c, float fs = 15) {
    float tw = TextW(label, fs, true);
    float w = std::max(26.0f, tw + 18.0f), h = fs * kLine + 6.0f;
    Glass(x, y, w, h, Scale(c, 0.22f, 225), c, 1, 7);
    Text(label, x + w / 2.0f, y + 3.0f, fs, Col(), AL_CENTER, true);
    return w;
}

// ---------------------------------------------------------------- toast / prompt / overlay
struct Toast { std::string text; int until; };
static std::vector<Toast> g_toasts;
static std::string g_prompt;

static void Notify(const std::string &text, float seconds = 6.0f) {
    g_toasts.push_back({text, Now() + (int)(seconds * 1000)});
    while (g_toasts.size() > 4) g_toasts.erase(g_toasts.begin());
}
static void Prompt(const std::string &text) { g_prompt = text; }

static void DrawOverlay() {
    int now = Now();
    g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(), [&](const Toast &t) { return t.until < now; }), g_toasts.end());
    float y = 22;
    for (auto &t : g_toasts) {
        float w = std::min(840.0f, TextW(t.text, 18.0f) + 52.0f);
        Glass(640.0f - w / 2.0f, y, w, 40.0f, Col(10, 18, 30, 225), Col(255, 190, 40), 1, 10);
        Text(t.text, 640.0f, y + 6.0f, 18.0f, Col(), AL_CENTER);
        y += 48.0f;
    }
    if (!g_prompt.empty()) {
        float w = TextW(g_prompt, 20.0f, true) + 56.0f;
        Glass(640.0f - w / 2.0f, 606.0f, w, 46.0f, Col(10, 18, 30, 230), kCyan, 2, 12);
        Text(g_prompt, 640.0f, 614.0f, 20.0f, Col(), AL_CENTER, true);
        g_prompt.clear();
    }
}

// Zone marker: ground ring + translucent cylinder + tall beam (far: a world-space light pillar) + on-screen label with distance.
// Turns bright green with "อยู่ในโซนแล้ว" once the player's vehicle is inside.
static void DrawMk(int type, Vector3 p, float sx, float sy, float sz, Col c) {
    GRAPHICS::DRAW_MARKER(type, p, V(0, 0, 0), V(0, 0, 0), V(sx, sy, sz), c.r, c.g, c.b, c.a, FALSE, FALSE, 2, FALSE, nullptr, nullptr, FALSE);
}
static const Col kYellow(255, 205, 40), kGreen(60, 220, 120), kRed(255, 70, 70), kBlue(70, 160, 255);

static void Zone(Vector3 p, float radius, Col c, const std::string &label, bool beam = true) {
    Ped pl = PlayerPed();
    Entity who = PED::IS_PED_IN_ANY_VEHICLE(pl, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(pl, FALSE) : pl;
    bool inside = Dist2(Pos(who), p) < radius;
    float pulse = 0.7f + 0.3f * sinf(Now() / 200.0f);
    Col col = inside ? Col(90, 255, 130) : c;
    DrawMk(25, V(p.x, p.y, p.z + 0.12f), radius * 2.0f, radius * 2.0f, 1.0f, Col(col.r, col.g, col.b, (int)(235 * pulse)));
    DrawMk(1, V(p.x, p.y, p.z - 1.0f), radius * 2.0f, radius * 2.0f, 5.0f, Col(col.r, col.g, col.b, (int)(60 * pulse)));
    if (beam) DrawMk(1, V(p.x, p.y, p.z - 1.0f), 1.4f, 1.4f, 160.0f, Col(col.r, col.g, col.b, (int)(120 * pulse)));

    // The game stops drawing DRAW_MARKER shapes at long range, so far targets get a tall light pillar made of
    // world-space polygons (two crossed planes, both faces) that grows with distance to stay visible from kilometres away.
    float distP = Dist(PlayerPos(), p);
    if (beam && distP > 90.0f) {
        float wdt = Clamp(distP * 0.005f, 1.2f, 14.0f);
        float topH = Clamp(distP * 0.5f, 120.0f, 400.0f);
        Col fc(col.r, col.g, col.b, (int)(105 * pulse));
        for (int k = 0; k < 2; k++) {
            float ax = k == 0 ? wdt : 0.0f, ay = k == 0 ? 0.0f : wdt;
            Vector3 b1 = V(p.x - ax, p.y - ay, p.z - 2.0f), b2 = V(p.x + ax, p.y + ay, p.z - 2.0f);
            Vector3 t1 = V(p.x - ax, p.y - ay, p.z + topH), t2 = V(p.x + ax, p.y + ay, p.z + topH);
            GRAPHICS::DRAW_POLY(b1, b2, t1, fc.r, fc.g, fc.b, fc.a);
            GRAPHICS::DRAW_POLY(b1, t1, b2, fc.r, fc.g, fc.b, fc.a);
            GRAPHICS::DRAW_POLY(b2, t2, t1, fc.r, fc.g, fc.b, fc.a);
            GRAPHICS::DRAW_POLY(b2, t1, t2, fc.r, fc.g, fc.b, fc.a);
        }
    }

    float sx = 0, sy = 0;
    if (!GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(V(p.x, p.y, p.z + 18.0f), &sx, &sy)) return;
    sx *= 1280.0f;
    sy *= 720.0f;
    if (sx < 0 || sx > 1280 || sy < 0 || sy > 720) return;
    // small tag kept in the upper part of the screen so it never covers the road ahead
    sy = std::min(sy, 150.0f);
    std::string text = label + "  " + KmStr(Dist(PlayerPos(), p));
    float w = TextW(text, 13.0f, true) + 20.0f;
    Glass(sx - w / 2.0f, sy - 3.0f, w, 24.0f, Col(8, 14, 24, 150), col, 1, 7);
    Text(text, sx, sy, 13.0f, Col(), AL_CENTER, true);
    if (inside) Text("อยู่ในโซนแล้ว - หยุดรถ", sx, sy + 26.0f, 13.0f, Col(90, 255, 130), AL_CENTER, true);
}

}  // namespace Ui