// common.h: shared helpers for ServicesJob (ScriptHookV C++ ASI). Included once from main.cpp (single translation unit).
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include <memory>
#include <fstream>
#include <sstream>
#include <random>

#include "inc/natives.h"
#include "inc/enums.h"
#include "inc/main.h"

#define SJ_VERSION "1.0"

// ---------------------------------------------------------------- math
static Vector3 V(float x, float y, float z) { Vector3 v; v.x = x; v.y = y; v.z = z; return v; }
static Vector3 operator+(const Vector3 &a, const Vector3 &b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static Vector3 operator-(const Vector3 &a, const Vector3 &b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static Vector3 operator*(const Vector3 &a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static float Len3(const Vector3 &a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static float Len2(const Vector3 &a) { return sqrtf(a.x * a.x + a.y * a.y); }
static float Dist(const Vector3 &a, const Vector3 &b) { return Len3(a - b); }
static float Dist2(const Vector3 &a, const Vector3 &b) { return Len2(a - b); }
static Vector3 Norm3(const Vector3 &a) { float l = Len3(a); return l < 1e-5f ? V(0, 0, 0) : a * (1.0f / l); }
static float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float WrapDeg(float a) { while (a > 180.0f) a -= 360.0f; while (a < -180.0f) a += 360.0f; return a; }
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static Vector3 LerpV(const Vector3 &a, const Vector3 &b, float t) { return a + (b - a) * t; }
static float Smooth(float x) { if (x <= 0) return 0; if (x >= 1) return 1; return x * x * (3.0f - 2.0f * x); }
static float HeadingTo(const Vector3 &from, const Vector3 &to) { Vector3 d = to - from; return atan2f(-d.x, d.y) * 57.29578f; }

static std::mt19937 &Rng() { static std::mt19937 r((unsigned)GetTickCount()); return r; }
static int RandInt(int lo, int hiExclusive) { if (hiExclusive <= lo) return lo; return lo + (int)(Rng()() % (unsigned)(hiExclusive - lo)); }
static float Rand01() { return (Rng()() & 0xFFFFFF) / (float)0x1000000; }

// ---------------------------------------------------------------- paths / log
static std::string g_dir;    // game folder (where the .asi lives), trailing backslash
static std::string g_data;   // g_dir + "ServicesJob\\"

static void Log(const char *fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    FILE *f = fopen((g_data + "log.txt").c_str(), "ab");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d %s\r\n", t.wHour, t.wMinute, t.wSecond, b);
    fclose(f);
}

static std::string ReadAll(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s = s.substr(3);
    return s;
}
static void WriteAll(const std::string &path, const std::string &text, bool bom = true) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (bom) f << "\xEF\xBB\xBF";
    f << text;
}
static std::vector<std::string> SplitLines(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static std::vector<std::string> SplitStr(const std::string &s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
static std::string Trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    return s.substr(a, b - a);
}
static std::string Fmt(const char *fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    return b;
}
static std::string MoneyStr(int v) {
    std::string s = std::to_string(v < 0 ? -v : v), out;
    int n = (int)s.size();
    for (int i = 0; i < n; i++) { out += s[i]; if ((n - i - 1) % 3 == 0 && i != n - 1) out += ','; }
    return std::string("$") + (v < 0 ? "-" : "") + out;
}
// rounded on purpose: fewer distinct strings means fewer Thai textures to render while the distance keeps changing
static std::string KmStr(float m) { return m >= 1000.0f ? Fmt("%.1f กม.", m / 1000.0f) : Fmt("%d ม.", ((int)m / 10) * 10); }
static std::string ClockStr(int sec) { if (sec < 0) sec = 0; return Fmt("%d:%02d", sec / 60, sec % 60); }

// ---------------------------------------------------------------- settings
enum JobKind { K_TOW = 0, K_TAXI, K_TOUR, K_TRUCK, K_AMB, K_POLICE, K_COUNT };

enum { A_PAGER = 0, A_ACCEPT, A_DISMISS, A_ACTION, A_COUNT };

// one action can be bound to a keyboard key AND a wheel/joystick button at the same time (either one works)
struct Bind {
    int key = 0;                    // virtual-key code, 0 = none
    unsigned mid = 0, pid = 0;      // joystick manufacturer / product id (so the binding survives device re-ordering)
    int btn = -1;                   // joystick button index, -1 = none
};

struct Config {
    Bind binds[A_COUNT];
    int offerMinSec = 25, offerMaxSec = 70, offerLifeSec = 240, tourStopSec = 15;
    float payMult = 1.0f;
    std::vector<std::string> slideModels = { "flatbed" };
    float slideZ = 1.15f, slideZShift = 0.0f, slideYShift = 0.0f, slideXShift = 0.0f, slideSeconds = 5.0f;
    bool debugKeys = true;
    float depotMarkerDist = 60.0f;   // depot ring/label is drawn only this close (metres)
    bool duty[K_COUNT] = {true, true, true, true, true, true};
    bool liveCard = true;            // job cards pop up at the right edge, no pager needed
    int cardSeconds = 20;
};
static Config cfg;

static std::string KeyName(int vk) {
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Numpad" + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
        case VK_INSERT: return "Insert"; case VK_DELETE: return "Delete"; case VK_HOME: return "Home"; case VK_END: return "End";
        case VK_PRIOR: return "PageUp"; case VK_NEXT: return "PageDown"; case VK_RETURN: return "Enter"; case VK_TAB: return "Tab";
        case VK_SPACE: return "Space"; case VK_BACK: return "Backspace"; case VK_UP: return "Up"; case VK_DOWN: return "Down";
        case VK_LEFT: return "Left"; case VK_RIGHT: return "Right"; case VK_CAPITAL: return "CapsLock"; case VK_MULTIPLY: return "Multiply";
        case VK_DIVIDE: return "Divide"; case VK_SUBTRACT: return "Subtract"; case VK_ADD: return "Add"; case VK_DECIMAL: return "Decimal";
        case VK_OEM_COMMA: return "Comma"; case VK_OEM_PERIOD: return "Period"; case VK_OEM_MINUS: return "Minus"; case VK_OEM_PLUS: return "Equals";
        case VK_OEM_1: return "Semicolon"; case VK_OEM_2: return "Slash"; case VK_OEM_3: return "Tilde"; case VK_OEM_4: return "LBracket";
        case VK_OEM_5: return "Backslash"; case VK_OEM_6: return "RBracket"; case VK_OEM_7: return "Quote";
    }
    return "VK" + std::to_string(vk);
}

static int ParseKey(const std::string &name, int def) {
    std::string n = Trim(name);
    if (n.empty()) return def;
    if (n == "0" || n == "none" || n == "None") return 0;
    std::string u;
    for (char c : n) u += (char)toupper((unsigned char)c);
    if (u.size() == 1) return (int)u[0];
    if (u[0] == 'F' && u.size() <= 3) { int k = atoi(u.c_str() + 1); if (k >= 1 && k <= 24) return VK_F1 + k - 1; }
    if (u.compare(0, 2, "VK") == 0) return atoi(u.c_str() + 2);
    for (int vk = 1; vk < 256; vk++) { std::string kn = KeyName(vk); for (auto &c : kn) c = (char)toupper((unsigned char)c); if (kn == u) return vk; }
    return def;
}

static std::string WheelStr(const Bind &b) { return b.btn < 0 ? "" : Fmt("%04X-%04X-%d", b.mid, b.pid, b.btn); }
static void ParseWheel(const std::string &s, Bind &b) {
    unsigned m = 0, p = 0;
    int btn = -1;
    if (sscanf(s.c_str(), "%x-%x-%d", &m, &p, &btn) == 3) { b.mid = m; b.pid = p; b.btn = btn; }
    else { b.mid = b.pid = 0; b.btn = -1; }
}

// text for hints: "Y / ปุ่ม 5"
static std::string BindText(int action) {
    const Bind &b = cfg.binds[action];
    std::string s;
    if (b.key) s = KeyName(b.key);
    if (b.btn >= 0) s += std::string(s.empty() ? "" : " / ") + "ปุ่มพวงมาลัย " + std::to_string(b.btn + 1);
    return s.empty() ? "(ยังไม่ได้ตั้งปุ่ม)" : s;
}
static std::string IniStr(const char *sec, const char *key, const char *def) {
    char b[512];
    GetPrivateProfileStringA(sec, key, def, b, sizeof b, (g_data + "ServicesJob.ini").c_str());
    return b;
}
static float IniFloat(const char *sec, const char *key, float def) { return (float)atof(IniStr(sec, key, Fmt("%g", def).c_str()).c_str()); }
static int IniInt(const char *sec, const char *key, int def) { return atoi(IniStr(sec, key, std::to_string(def).c_str()).c_str()); }

static const char *kDefaultIni =
    "[Keys]\r\nPager=F9\r\nAction=E\r\n; ปุ่มดีบักใช้คู่กับ Ctrl+Alt (F7 ดู bone รถ, F8 บันทึกพิกัด) ตั้ง 0 เพื่อปิด\r\nDebugKeys=1\r\n\r\n"
    "[Dispatch]\r\nOfferMinSeconds=25\r\nOfferMaxSeconds=70\r\nOfferLifeSeconds=240\r\n\r\n"
    "[Jobs]\r\nTourStopSeconds=15\r\nPayMultiplier=1.0\r\n; ระยะ (เมตร) ที่เห็นป้ายและวงแหวนจุดรับงาน\r\nDepotMarkerDistance=60\r\n\r\n"
    "[Slide]\r\n; ชื่อโมเดล (spawn name) ของรถสไลด์ คั่นด้วยจุลภาค เช่น flatbed,actros_flatbed\r\nModels=flatbed\r\n"
    "; flatbed ของเกมมีค่าถาดที่วัดจากโมเดลจริงอยู่แล้ว ปรับละเอียดได้ด้วย ZShift/YShift/XShift\r\n"
    "; Z ใช้เฉพาะรถรุ่นอื่นที่ไม่มีค่าวัดไว้\r\nZ=1.15\r\nZShift=0.0\r\nYShift=0.0\r\nXShift=0.0\r\n; เวลาที่วินช์ใช้ดึงรถขึ้น/ลง (วินาที)\r\nSeconds=5.0\r\n";

static void LoadConfig() {
    std::string ini = g_data + "ServicesJob.ini";
    if (GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) WriteAll(ini, kDefaultIni, false);
    static const char *bindNames[A_COUNT] = {"Pager", "Accept", "Dismiss", "Action"};
    static const int bindDef[A_COUNT] = {VK_F9, 'Y', 'N', 'E'};
    for (int a = 0; a < A_COUNT; a++) {
        std::string v = IniStr("Binds", bindNames[a], "?");
        if (v == "?") v = a == A_PAGER ? IniStr("Keys", "Pager", "F9") : a == A_ACTION ? IniStr("Keys", "Action", "E") : KeyName(bindDef[a]);
        cfg.binds[a].key = ParseKey(v, bindDef[a]);
        ParseWheel(IniStr("Binds", (std::string(bindNames[a]) + "Wheel").c_str(), ""), cfg.binds[a]);
    }
    static const char *dutyNames[K_COUNT] = {"Tow", "Taxi", "Tour", "Truck", "Ambulance", "Police"};
    for (int k = 0; k < K_COUNT; k++) cfg.duty[k] = IniInt("Duty", dutyNames[k], 1) != 0;
    cfg.liveCard = IniInt("Live", "Enabled", 1) != 0;
    cfg.cardSeconds = std::max(5, std::min(120, IniInt("Live", "CardSeconds", 20)));
    cfg.debugKeys = IniInt("Keys", "DebugKeys", 1) != 0;
    cfg.offerMinSec = IniInt("Dispatch", "OfferMinSeconds", 25);
    cfg.offerMaxSec = std::max(cfg.offerMinSec + 1, IniInt("Dispatch", "OfferMaxSeconds", 70));
    cfg.offerLifeSec = IniInt("Dispatch", "OfferLifeSeconds", 240);
    cfg.tourStopSec = IniInt("Jobs", "TourStopSeconds", 15);
    cfg.payMult = IniFloat("Jobs", "PayMultiplier", 1.0f);
    cfg.depotMarkerDist = IniFloat("Jobs", "DepotMarkerDistance", 60.0f);
    cfg.slideModels.clear();
    for (auto &m : SplitStr(IniStr("Slide", "Models", "flatbed"), ',')) { std::string t = Trim(m); if (!t.empty()) cfg.slideModels.push_back(t); }
    cfg.slideZ = IniFloat("Slide", "Z", 1.15f);
    cfg.slideZShift = IniFloat("Slide", "ZShift", 0.0f);
    cfg.slideYShift = IniFloat("Slide", "YShift", 0.0f);
    cfg.slideXShift = IniFloat("Slide", "XShift", 0.0f);
    cfg.slideSeconds = IniFloat("Slide", "Seconds", 5.0f);
}

// everything the player can change in game (bindings, job types on/off, live card) is written back to the ini
static void SaveConfig() {
    std::string f = g_data + "ServicesJob.ini";
    static const char *bindNames[A_COUNT] = {"Pager", "Accept", "Dismiss", "Action"};
    for (int a = 0; a < A_COUNT; a++) {
        WritePrivateProfileStringA("Binds", bindNames[a], cfg.binds[a].key ? KeyName(cfg.binds[a].key).c_str() : "0", f.c_str());
        WritePrivateProfileStringA("Binds", (std::string(bindNames[a]) + "Wheel").c_str(), WheelStr(cfg.binds[a]).c_str(), f.c_str());
    }
    static const char *dutyNames[K_COUNT] = {"Tow", "Taxi", "Tour", "Truck", "Ambulance", "Police"};
    for (int k = 0; k < K_COUNT; k++) WritePrivateProfileStringA("Duty", dutyNames[k], cfg.duty[k] ? "1" : "0", f.c_str());
    WritePrivateProfileStringA("Live", "Enabled", cfg.liveCard ? "1" : "0", f.c_str());
    WritePrivateProfileStringA("Live", "CardSeconds", std::to_string(cfg.cardSeconds).c_str(), f.c_str());
}

// ---------------------------------------------------------------- game helpers
static int Now() { return MISC::GET_GAME_TIMER(); }
static float Dt() { return MISC::GET_FRAME_TIME(); }
static Hash HK(const char *s) { return MISC::GET_HASH_KEY(s); }
static Ped PlayerPed() { return PLAYER::PLAYER_PED_ID(); }
static bool Ex(int e) { return e != 0 && ENTITY::DOES_ENTITY_EXIST(e); }
static Vector3 Pos(int e) { return ENTITY::GET_ENTITY_COORDS(e, TRUE); }
static Vector3 PlayerPos() { return Pos(PlayerPed()); }
static float Hdg(int e) { return ENTITY::GET_ENTITY_HEADING(e); }
static Vector3 OffW(int e, float x, float y, float z) { return ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(e, V(x, y, z)); }
static Vector3 OffW(int e, Vector3 o) { return ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(e, o); }
static Vector3 LocalOf(int e, Vector3 w) { return ENTITY::GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS(e, w); }
static float SpeedOf(int e) { return ENTITY::GET_ENTITY_SPEED(e); }
static bool IsDead(int e) { return ENTITY::IS_ENTITY_DEAD(e, FALSE); }
static bool VehModel(Vehicle v, const char *name) { return ENTITY::GET_ENTITY_MODEL(v) == HK(name); }
// first free passenger seat (rear right first), -2 when there is none. Seat 0 = front passenger, 1..n = rear seats
static int FreePassengerSeat(Vehicle v) {
    int n = VEHICLE::GET_VEHICLE_MODEL_NUMBER_OF_SEATS(ENTITY::GET_ENTITY_MODEL(v));
    int order[] = {2, 1, 0, 3, 4, 5, 6, 7};
    for (int s : order) if (s <= n - 2 && VEHICLE::IS_VEHICLE_SEAT_FREE(v, s, FALSE)) return s;
    return -2;
}

static bool ModelOk(const char *name) {
    Hash h = HK(name);
    return STREAMING::IS_MODEL_IN_CDIMAGE(h) && STREAMING::IS_MODEL_VALID(h);
}
static bool LoadModel(Hash h) {
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(h) || !STREAMING::IS_MODEL_VALID(h)) return false;
    STREAMING::REQUEST_MODEL(h);
    int t = Now() + 3000;
    while (!STREAMING::HAS_MODEL_LOADED(h) && Now() < t) WAIT(0);
    return STREAMING::HAS_MODEL_LOADED(h) != 0;
}

static Vehicle SpawnVeh(const char *name, Vector3 pos, float hd) {
    Hash h = HK(name);
    if (!LoadModel(h)) return 0;
    Vehicle v = VEHICLE::CREATE_VEHICLE(h, pos, hd, FALSE, FALSE, FALSE);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(h);
    if (v) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, TRUE, TRUE);
        VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(v, 5.0f);
    }
    return v;
}
static Ped SpawnPedModel(const char *name, Vector3 pos, float hd) {
    Hash h = HK(name);
    if (!LoadModel(h)) return 0;
    Ped p = PED::CREATE_PED(26, h, pos, hd, FALSE, FALSE);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(h);
    if (p) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, TRUE, TRUE);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, TRUE);
    }
    return p;
}
static Ped SpawnCiv(Vector3 pos, float hd) {
    static const char *models[] = {"a_m_y_business_01", "a_f_y_business_01", "a_m_m_business_01", "a_f_y_tourist_01", "a_m_y_hipster_01",
                                   "a_f_y_hipster_01", "a_m_y_beach_01", "a_f_y_bevhills_01", "a_m_y_downtown_01", "a_f_m_downtown_01",
                                   "a_m_y_vinewood_01", "a_m_m_tourist_01", "a_f_y_runner_01"};
    for (int i = 0; i < 6; i++) {
        Ped p = SpawnPedModel(models[RandInt(0, (int)(sizeof models / sizeof *models))], pos, hd);
        if (p) return p;
    }
    return 0;
}
static Object SpawnProp(const std::vector<const char *> &names, Vector3 pos, float hd) {
    for (const char *n : names) {
        if (!ModelOk(n)) continue;
        Hash h = HK(n);
        if (!LoadModel(h)) continue;
        Object o = OBJECT::CREATE_OBJECT(h, pos, FALSE, FALSE, FALSE);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(h);
        if (o) {
            ENTITY::SET_ENTITY_HEADING(o, hd);
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(o, TRUE, TRUE);
            return o;
        }
    }
    return 0;
}

static void Del(int &e) {
    if (e && ENTITY::DOES_ENTITY_EXIST(e)) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e, TRUE, TRUE);
        ENTITY::DELETE_ENTITY(&e);
    }
    e = 0;
}
static void Rel(int &e) {
    if (e && ENTITY::DOES_ENTITY_EXIST(e)) ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&e);
    e = 0;
}

// ---- blips
static void BlipSetName(Blip b, const char *name) {
    HUD::BEGIN_TEXT_COMMAND_SET_BLIP_NAME("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(name);
    HUD::END_TEXT_COMMAND_SET_BLIP_NAME(b);
}
// ---- GPS route: only one blip has the route at a time, and it is re-asserted every 1.5 s so it cannot silently disappear
static Blip g_routeBlip = 0;
static int g_routeAt = 0;
static void RouteSet(Blip b) {
    if (g_routeBlip && g_routeBlip != b && HUD::DOES_BLIP_EXIST(g_routeBlip)) HUD::SET_BLIP_ROUTE(g_routeBlip, FALSE);
    g_routeBlip = b;
    if (b) HUD::SET_BLIP_ROUTE(b, TRUE);
    g_routeAt = Now();
}
static void RouteTick() {
    if (!g_routeBlip) return;
    if (!HUD::DOES_BLIP_EXIST(g_routeBlip)) { g_routeBlip = 0; return; }
    if (Now() - g_routeAt > 1500) { HUD::SET_BLIP_ROUTE(g_routeBlip, TRUE); g_routeAt = Now(); }
}

static Blip BlipPos(Vector3 p, int sprite, int color, const char *name, bool route, float scale = 1.0f) {
    Blip b = HUD::ADD_BLIP_FOR_COORD(p);
    HUD::SET_BLIP_SPRITE(b, sprite);
    HUD::SET_BLIP_COLOUR(b, color);
    HUD::SET_BLIP_SCALE(b, scale);
    HUD::SET_BLIP_AS_SHORT_RANGE(b, FALSE);
    BlipSetName(b, name);
    if (route) RouteSet(b);
    return b;
}
static Blip BlipEnt(Entity e, int sprite, int color, const char *name) {
    Blip b = HUD::ADD_BLIP_FOR_ENTITY(e);
    HUD::SET_BLIP_SPRITE(b, sprite);
    HUD::SET_BLIP_COLOUR(b, color);
    BlipSetName(b, name);
    return b;
}
static void DelBlip(Blip &b) {
    if (b && HUD::DOES_BLIP_EXIST(b)) HUD::REMOVE_BLIP(&b);
    b = 0;
}
enum { SPR_STD = 1, SPR_TOW = 68, SPR_TAXI = 843, SPR_BUS = 513, SPR_TRUCK = 477, SPR_HOSP = 61, SPR_POLICE = 60, SPR_WANTED = 57 };
enum { COL_RED = 1, COL_GREEN = 2, COL_BLUE = 3, COL_YELLOW = 66, COL_ORANGE = 51, COL_WHITE = 0 };

// ---- roads
static bool FindRoad(Vector3 around, float mn, float mx, Vector3 &out, float &hd) {
    for (int i = 0; i < 20; i++) {
        float a = Rand01() * 6.2831853f;
        float d = mn + Rand01() * (mx - mn);
        Vector3 np;
        float h = 0;
        // nodeType 0 = main roads only (1 would include driveways, car parks, alleys and dirt tracks)
        if (PATHFIND::GET_CLOSEST_VEHICLE_NODE_WITH_HEADING(V(around.x + cosf(a) * d, around.y + sinf(a) * d, around.z), &np, &h, 0, 3.0f, 0)) {
            if (Dist2(np, around) >= mn * 0.6f) { out = np; hd = h; return true; }
        }
    }
    return false;
}
static Vector3 SnapRoad(Vector3 p) {
    Vector3 o;
    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(p, &o, 0, 3.0f, 0.0f)) return o;
    return p;
}
static std::string StreetAt(Vector3 p) {
    Hash s = 0, c = 0;
    PATHFIND::GET_STREET_NAME_AT_COORD(p, &s, &c);
    const char *n = s ? HUD::GET_STREET_NAME_FROM_HASH_KEY(s) : nullptr;
    return n ? n : "";
}
static float GroundZ(Vector3 p, float fallback) {
    float z = 0;
    if (MISC::GET_GROUND_Z_FOR_3D_COORD(V(p.x, p.y, p.z + 50.0f), &z, FALSE, FALSE)) return z;
    return fallback;
}

// ---- tasks / anims
static void PlayAnim(Ped p, const char *dict, const char *clip, bool loop) {
    STREAMING::REQUEST_ANIM_DICT(dict);
    int t = Now() + 1500;
    while (!STREAMING::HAS_ANIM_DICT_LOADED(dict) && Now() < t) WAIT(0);
    TASK::TASK_PLAY_ANIM(p, dict, clip, 8.0f, -8.0f, -1, loop ? 1 : 0, 0.0f, FALSE, FALSE, FALSE);
}
static void Scenario(Ped p, const char *name) { TASK::TASK_START_SCENARIO_IN_PLACE(p, name, 0, TRUE); }

// ---- money (single player: add to the active character's cash stat)
static void AddMoney(int amount) {
    Hash m = ENTITY::GET_ENTITY_MODEL(PlayerPed());
    const char *stat = m == 0x0D7114C9 ? "SP0_TOTAL_CASH" : m == 0x9B22DBAF ? "SP1_TOTAL_CASH" : m == 0x9B810FA2 ? "SP2_TOTAL_CASH" : "SP0_TOTAL_CASH";
    int cur = 0;
    STATS::STAT_GET_INT(HK(stat), &cur, -1);
    STATS::STAT_SET_INT(HK(stat), cur + amount, TRUE);
}

// ---------------------------------------------------------------- stats
static std::map<int, int> g_done;
static long long g_earned = 0;
static void StatsLoad() {
    for (auto &l : SplitLines(ReadAll(g_data + "save.txt"))) {
        auto kv = SplitStr(l, '=');
        if (kv.size() != 2) continue;
        if (kv[0] == "earned") g_earned = atoll(kv[1].c_str());
        else g_done[atoi(kv[0].c_str())] = atoi(kv[1].c_str());
    }
}
static void StatsAdd(int kind, int pay) {
    g_done[kind]++;
    g_earned += pay;
    std::string s = "earned=" + std::to_string(g_earned) + "\r\n";
    for (auto &kv : g_done) s += std::to_string(kv.first) + "=" + std::to_string(kv.second) + "\r\n";
    WriteAll(g_data + "save.txt", s, false);
}
static int StatsCount(int kind) { auto it = g_done.find(kind); return it == g_done.end() ? 0 : it->second; }

// ---------------------------------------------------------------- places
struct Place {
    std::string name;
    Vector3 pos;
    Place() { pos = V(0, 0, 0); }
    Place(const std::string &n, float x, float y, float z) : name(n) { pos = V(x, y, z); }
};
