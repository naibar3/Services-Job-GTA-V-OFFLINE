// pager.h: dispatch center. Live job cards on the right side (accept straight away with a key or a wheel button) and the
// pager window (inbox, job types, key bindings, status) for mouse + keyboard.
#pragma once

namespace Offers {
static std::unique_ptr<Offer> Make(int kind, Vector3 pp) {
    std::unique_ptr<Offer> o;
    switch (kind) {
        case K_TOW: o = TowRepoJob::MakeOffer(pp); break;
        case K_TAXI: o = TaxiJob::MakeOffer(pp); break;
        case K_TOUR: o = TourJob::MakeOffer(pp); break;
        case K_TRUCK: o = TruckJob::MakeOffer(pp); break;
        case K_AMB: o = AmbulanceJob::MakeOffer(pp); break;
        case K_POLICE: o = PoliceChaseJob::MakeOffer(pp); break;
    }
    if (o) { o->kind = kind; o->born = Now(); }
    return o;
}
}  // namespace Offers

namespace Pager {

enum { T_INBOX = 0, T_DUTY, T_SETTINGS, T_STATUS, T_COUNT };

static bool isOpen = false;
static std::vector<Offer> inbox;
static int tab = 0, sel = 0, nextOffer = 0;

static const float X = 760, Y = 76, W = 500, HH = 568;
static const Col Teal(38, 166, 154), Gold(255, 214, 79);
static const float PAD = 18;

static void Nav() { Ui::Beep("NAV_UP_DOWN", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }

// colour per job kind: chosen to be clearly different from each other
static Col KindCol(int k) {
    switch (k) {
        case K_TOW: return Col(255, 140, 20);      // orange
        case K_TAXI: return Col(255, 226, 40);     // yellow
        case K_TOUR: return Col(60, 230, 130);     // green
        case K_TRUCK: return Col(70, 170, 255);    // sky blue
        case K_AMB: return Col(255, 60, 80);       // red
        case K_POLICE: return Col(175, 95, 255);   // violet
    }
    return Col(200, 200, 200);
}
// "[ยึดรถ] ค้างชำระ แถว X"  ->  tag "[ยึดรถ]" + rest
static void SplitTag(const std::string &title, std::string &tag, std::string &rest) {
    size_t e = title.find(']');
    if (!title.empty() && title[0] == '[' && e != std::string::npos) {
        tag = title.substr(0, e + 1);
        rest = Trim(title.substr(e + 1));
    } else { tag.clear(); rest = title; }
}

// ---------------------------------------------------------------- live card
namespace Live {
static std::unique_ptr<Offer> card;
static int until = 0;

static void ToInbox() {
    if (!card) return;
    inbox.insert(inbox.begin(), *card);
    card.reset();
}
static void Show(std::unique_ptr<Offer> o) {
    ToInbox();
    card = std::move(o);
    until = Now() + cfg.cardSeconds * 1000;
}
static void Accept() {
    if (!card) return;
    if (JobManager::Begin(*card)) card.reset();
}
static void Dismiss() {
    if (!card) return;
    card.reset();
    Ui::Beep("CANCEL", "HUD_FREEMODE_SOUNDSET");
}

static void Draw() {
    if (!card) return;
    if (Now() > until) { ToInbox(); return; }
    // card is drawn at 80% of the original size (S scales every length and font), top right corner
    const float S = 0.80f;
    const float cw = 372 * S, ch = 270 * S, cx = 1280 - cw - 12, cy = 12;
    Offer &o = *card;
    Col kc = KindCol(o.kind);
    Ui::Panel(cx, cy, cw, ch, kc, Ui::kMagenta, 16 * S);

    std::string tag, rest;
    SplitTag(o.title, tag, rest);
    Ui::Text(tag.empty() ? std::string(KindName(o.kind)) : tag, cx + 22 * S, cy + 12 * S, 17 * S, kc, AL_LEFT, true);
    Ui::Text("งานเข้า!", cx + cw - 50 * S, cy + 12 * S, 18 * S, Col(255, 80, 90), AL_RIGHT, true);
    Ui::Icon(Ui::IC_WARN, cx + cw - 40 * S, cy + 12 * S, 26 * S, Col(255, 80, 90));
    Ui::Rect(cx + 18 * S, cy + 44 * S, cw - 36 * S, 1, Col(255, 255, 255, 30));

    // title fits the card: shrink the font until it does
    float fs = 20 * S;
    while (fs > 11 && Ui::TextW(rest, fs, true) > cw - 44 * S) fs -= 0.5f;
    Ui::Text(rest, cx + 22 * S, cy + 52 * S, fs, Col(), AL_LEFT, true);
    Ui::Icon(Ui::IC_PIN, cx + 22 * S, cy + 90 * S, 22 * S, Col(190, 205, 220));
    float fl = 16 * S;
    while (fl > 10 && Ui::TextW(o.line1, fl) > cw - 74 * S) fl -= 0.5f;
    Ui::Text(o.line1, cx + 52 * S, cy + 90 * S, fl, Col(190, 205, 220));

    float dist = Dist(PlayerPos(), o.pos);
    Ui::Text("ค่าจ้าง", cx + 22 * S, cy + 124 * S, 16 * S, Col(150, 165, 180));
    Ui::Text(MoneyStr(o.pay), cx + 22 * S + Ui::TextW("ค่าจ้าง", 16 * S) + 10 * S, cy + 119 * S, 24 * S, Col(70, 255, 130), AL_LEFT, true);
    std::string dtxt = "ห่าง " + KmStr(dist);
    Ui::Icon(Ui::IC_PIN, cx + cw - 22 * S - Ui::TextW(dtxt, 16 * S, true) - 26 * S, cy + 124 * S, 20 * S, Col(255, 205, 60));
    Ui::Text(dtxt, cx + cw - 22 * S, cy + 124 * S, 16 * S, Col(255, 205, 60), AL_RIGHT, true);

    float frac = Clamp((float)(until - Now()) / (cfg.cardSeconds * 1000.0f), 0.0f, 1.0f);
    Ui::Bar(cx + 22 * S, cy + 160 * S, cw - 44 * S, 14 * S, frac, Col(60, 255, 120), Col(0, 220, 255));

    // key hints: rows of [KEY] label
    float hx = cx + 22 * S;
    Ui::Text("รับงาน", hx, cy + 192 * S, 17 * S, Col(210, 225, 240), AL_LEFT, true);
    float bx = hx + 84 * S;
    const Bind &ba = cfg.binds[A_ACCEPT];
    if (ba.key) bx += Ui::Badge(bx, cy + 190 * S, KeyName(ba.key), Col(60, 230, 140), 15 * S) + 8 * S;
    if (ba.btn >= 0) bx += Ui::Badge(bx, cy + 190 * S, "พวงมาลัย " + std::to_string(ba.btn + 1), Col(70, 170, 255), 15 * S) + 8 * S;
    if (!ba.key && ba.btn < 0) Ui::Text("ยังไม่ได้ตั้งปุ่ม", bx, cy + 192 * S, 15 * S, Col(150, 165, 180));
    Ui::Text("ปฏิเสธ", hx, cy + 230 * S, 15 * S, Col(220, 165, 165));
    bx = hx + 84 * S;
    const Bind &bd = cfg.binds[A_DISMISS];
    if (bd.key) bx += Ui::Badge(bx, cy + 228 * S, KeyName(bd.key), Col(255, 80, 90), 14 * S) + 8 * S;
    if (bd.btn >= 0) Ui::Badge(bx, cy + 228 * S, "พวงมาลัย " + std::to_string(bd.btn + 1), Col(255, 80, 90), 14 * S);
}}  // namespace Live

// ---------------------------------------------------------------- pager window logic
static void Close() { isOpen = false; JobManager::g_pagerOpen = false; Ui::MouseOff(); Input::CancelCapture(); }
static void Toggle() {
    isOpen = !isOpen;
    JobManager::g_pagerOpen = isOpen;
    Ui::Beep("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
    if (isOpen) { sel = 0; for (auto &o : inbox) o.read = true; }
    else { Ui::MouseOff(); Input::CancelCapture(); }
}

static int RowCount() {
    switch (tab) {
        case T_INBOX: return (int)inbox.size();
        case T_DUTY: return (int)K_COUNT;
        case T_SETTINGS: return A_COUNT + 1;
    }
    return 1;
}

static void DeleteSelected() {
    if (tab == T_INBOX && sel < (int)inbox.size()) {
        inbox.erase(inbox.begin() + sel);
        sel = std::max(0, std::min(sel, (int)inbox.size() - 1));
    }
}

static void Accept() {
    Ui::Beep("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
    if (tab == T_INBOX) {
        if (sel >= (int)inbox.size()) return;
        if (JobManager::Begin(inbox[sel])) { inbox.erase(inbox.begin() + sel); sel = std::max(0, std::min(sel, (int)inbox.size() - 1)); Close(); }
    } else if (tab == T_DUTY) {
        cfg.duty[sel] = !cfg.duty[sel];
        SaveConfig();
    } else if (tab == T_SETTINGS) {
        if (sel < A_COUNT) Input::StartCapture(sel, 0);
        else { cfg.liveCard = !cfg.liveCard; SaveConfig(); }
    } else if (JobManager::active) {
        JobManager::Cancel();
    }
}

static void Key(int vk) {
    int rows = RowCount();
    switch (vk) {
        case VK_ESCAPE: case VK_BACK: Close(); break;
        case VK_LEFT: tab = (tab + T_COUNT - 1) % T_COUNT; sel = 0; Nav(); break;
        case VK_RIGHT: tab = (tab + 1) % T_COUNT; sel = 0; Nav(); break;
        case VK_UP: if (rows > 0) sel = (sel + rows - 1) % rows; Nav(); break;
        case VK_DOWN: if (rows > 0) sel = (sel + 1) % rows; Nav(); break;
        case VK_DELETE: DeleteSelected(); break;
        case VK_RETURN: Accept(); break;
    }
}

// send a new job every 25-70 s (configurable) while no job is running: as a live card, or into the inbox
static void Update() {
    int now = Now();
    for (int i = (int)inbox.size() - 1; i >= 0; i--)
        if (now - inbox[i].born > cfg.offerLifeSec * 1000) inbox.erase(inbox.begin() + i);
    if (tab == T_INBOX && sel >= (int)inbox.size()) sel = std::max(0, (int)inbox.size() - 1);
    if (JobManager::active && Live::card) Live::card.reset();

    if (nextOffer == 0) nextOffer = now + 8000;
    if (now < nextOffer) return;
    nextOffer = now + RandInt(cfg.offerMinSec, cfg.offerMaxSec) * 1000;
    if (JobManager::active || inbox.size() >= 6) return;
    if (ENTITY::IS_ENTITY_DEAD(PlayerPed(), FALSE) || CUTSCENE::IS_CUTSCENE_ACTIVE()) return;

    std::vector<int> enabled;
    for (int k = 0; k < K_COUNT; k++) if (cfg.duty[k]) enabled.push_back(k);
    if (enabled.empty()) return;
    int pick = enabled[RandInt(0, (int)enabled.size())];
    std::unique_ptr<Offer> o;
    try { o = Offers::Make(pick, PlayerPos()); } catch (...) { Log("exception making offer"); }
    if (!o) { nextOffer = now + 4000; return; }
    Ui::Beep("Text_Arrive_Tone", "Phone_SoundSet_Default");
    if (cfg.liveCard && !isOpen) {
        Live::Show(std::move(o));
    } else {
        Ui::Notify("เพจเจอร์: " + o->title + "  (กด " + BindText(A_PAGER) + " เพื่อเปิด)", 7.0f);
        inbox.insert(inbox.begin(), *o);
    }
}

// ---------------------------------------------------------------- pager window drawing (neon glass style)
static void Heading(const std::string &s, float x, float y) { Ui::Text(s, x, y, 18, Gold, AL_LEFT, true); }

static void DrawInbox(float cy) {
    if (inbox.empty()) {
        Ui::Icon(Ui::IC_PAGER, X + W / 2 - 28, cy + 70, 56, Col(120, 140, 160));
        Ui::Text("ยังไม่มีข้อความ", X + W / 2, cy + 140, 22, Col(200, 215, 230), AL_CENTER, true);
        Ui::Text("รอศูนย์ส่งงานเข้ามาในเพจเจอร์สักครู่", X + W / 2, cy + 176, 16, Col(150, 165, 180), AL_CENTER);
        return;
    }
    const float rowH = 88;
    const int visible = 5;
    if (Ui::Wheel != 0 && Ui::Hit(X, cy, W, rowH * visible)) sel = std::max(0, std::min((int)inbox.size() - 1, sel + Ui::Wheel));
    int first = std::max(0, std::min(sel - visible + 1, (int)inbox.size() - visible));
    for (int i = 0; i < visible && first + i < (int)inbox.size(); i++) {
        int idx = first + i;
        Offer &o = inbox[idx];
        float ry = cy + i * rowH;
        float rh = rowH - 8;
        bool hoverRow = Ui::Hit(X + 12, ry, W - 24, rh);
        bool isSel = idx == sel;
        Col kc = KindCol(o.kind);
        Ui::Glass(X + 14, ry + 2, W - 28, rh, isSel ? Col(22, 40, 58, 240) : Col(14, 24, 38, 215), isSel ? kc : Ui::Mix(kc, Col(60, 70, 90), 0.55f), isSel ? 2 : 1, 10);

        std::string tag, rest;
        SplitTag(o.title, tag, rest);
        float tx = X + 32;
        if (!tag.empty()) { Ui::Text(tag, tx, ry + 8, 16, kc, AL_LEFT, true); tx += Ui::TextW(tag, 16, true) + 8; }
        float fs = 17;
        while (fs > 13 && Ui::TextW(rest, fs, true) > X + W - 90 - tx) fs -= 1;
        Ui::Text(rest, tx, ry + 8, fs, Col(), AL_LEFT, true);
        Ui::Icon(Ui::IC_PIN, X + 32, ry + 36, 19, Col(170, 185, 200));
        Ui::Text(o.line1, X + 56, ry + 36, 15, Col(185, 200, 215));

        float dist = Dist(PlayerPos(), o.pos);
        int left = cfg.offerLifeSec - (Now() - o.born) / 1000;
        Ui::Text("ค่าจ้าง", X + 32, ry + 62, 15, Col(150, 165, 180));
        float px = X + 32 + Ui::TextW("ค่าจ้าง", 15) + 8;
        std::string pay = MoneyStr(o.pay);
        Ui::Text(pay, px, ry + 59, 19, Col(70, 255, 130), AL_LEFT, true);
        px += Ui::TextW(pay, 19, true) + 16;
        Ui::Text("ห่าง " + KmStr(dist), px, ry + 62, 15, Col(255, 205, 60), AL_LEFT, true);
        Ui::Text(ClockStr(left), X + W - 34, ry + 8, 15, left < 45 ? Col(255, 110, 100) : Col(150, 165, 180), AL_RIGHT);

        if (hoverRow && Ui::Click && !isSel) { sel = idx; Nav(); }
        if (isSel) {
            if (Ui::Button(X + W - 162, ry + rh - 34, 96, 32, "รับงาน", Col(40, 220, 130))) { Accept(); return; }
            if (Ui::Button(X + W - 58, ry + rh - 34, 34, 32, "ลบ", Col(255, 90, 100))) { DeleteSelected(); return; }
        }
    }
    if ((int)inbox.size() > visible)
        Ui::Text(std::to_string(sel + 1) + " / " + std::to_string(inbox.size()), X + W - 34, cy + rowH * visible + 2, 14, Col(150, 165, 180), AL_RIGHT);
}

static void DrawDuty(float cy) {
    Heading("เลือกประเภทงานที่ให้ศูนย์ส่งมา", X + PAD + 6, cy);
    Ui::Text("คลิกที่แถวเพื่อเปิด / ปิด  (จำค่าไว้ให้อัตโนมัติ)", X + PAD + 6, cy + 28, 14, Col(140, 155, 170));
    for (int i = 0; i < K_COUNT; i++) {
        float ry = cy + 60 + i * 56;
        bool hover = Ui::Hit(X + 14, ry, W - 28, 50);
        Col kc = KindCol(i);
        Ui::Glass(X + 14, ry, W - 28, 50, i == sel || hover ? Col(22, 40, 58, 240) : Col(14, 24, 38, 215), cfg.duty[i] ? kc : Col(80, 90, 105), cfg.duty[i] ? 1 : 0, 11);
        Ui::Text(KindName(i), X + 36, ry + 10, 20, cfg.duty[i] ? Col() : Col(140, 150, 165), AL_LEFT, true);
        Ui::Toggle(X + W - 112, ry + 11, 84, 28, cfg.duty[i], "เปิดรับ", "ปิด");
        if (hover && Ui::Click) { sel = i; cfg.duty[i] = !cfg.duty[i]; SaveConfig(); Nav(); }
    }
}

static void DrawSettings(float cy) {
    Heading("ตั้งปุ่ม", X + PAD + 6, cy);
    Ui::Text("คีย์บอร์ด + ปุ่มพวงมาลัย ใช้แทนกันได้  ·  คลิกช่องแล้วกดปุ่ม  ·  Esc ยกเลิก", X + PAD + 6, cy + 28, 14, Col(140, 155, 170));
    static const char *labels[A_COUNT] = {"เปิด / ปิดเพจเจอร์", "รับงานที่ขึ้นการ์ด", "ปฏิเสธงาน", "ปุ่มกระทำ (เกี่ยว / ปล่อยรถ)"};
    Ui::Text("คีย์บอร์ด", X + W - 222, cy + 60, 14, Col(150, 165, 180), AL_CENTER);
    Ui::Text("พวงมาลัย", X + W - 112, cy + 60, 14, Col(150, 165, 180), AL_CENTER);
    for (int a = 0; a < A_COUNT; a++) {
        float ry = cy + 84 + a * 58;
        bool isSel = a == sel;
        Ui::Glass(X + 14, ry, W - 28, 52, isSel ? Col(22, 40, 58, 240) : Col(14, 24, 38, 215), isSel ? Ui::kCyan : Col(70, 85, 105), isSel ? 1 : 0, 11);
        Ui::Text(labels[a], X + 34, ry + 12, 17, Col(), AL_LEFT, true);
        Bind &b = cfg.binds[a];
        bool capKb = Input::Capturing() && Input::capAction == a && Input::capSlot == 0;
        bool capWh = Input::Capturing() && Input::capAction == a && Input::capSlot == 1;
        std::string kt = capKb ? "กดปุ่ม..." : b.key ? KeyName(b.key) : "-";
        std::string wt = capWh ? "กดปุ่ม..." : b.btn >= 0 ? "ปุ่ม " + std::to_string(b.btn + 1) : "-";
        if (Ui::Button(X + W - 278, ry + 10, 112, 32, kt, capKb ? Col(255, 190, 40) : Col(40, 220, 130))) { sel = a; Input::StartCapture(a, 0); }
        if (Ui::Button(X + W - 160, ry + 10, 112, 32, wt, capWh ? Col(255, 190, 40) : Col(70, 170, 255))) { sel = a; Input::StartCapture(a, 1); }
        if (Ui::Button(X + W - 44, ry + 10, 28, 32, "x", Col(255, 90, 100))) { b = Bind(); SaveConfig(); }
    }
    float ry = cy + 84 + A_COUNT * 58;
    bool hover = Ui::Hit(X + 14, ry, W - 28, 52);
    Ui::Glass(X + 14, ry, W - 28, 52, A_COUNT == sel || hover ? Col(22, 40, 58, 240) : Col(14, 24, 38, 215), Col(70, 85, 105), 0, 11);
    Ui::Text("การ์ดงานขึ้นขวามือ (รับงานสดได้เลย)", X + 34, ry + 12, 17, Col(), AL_LEFT, true);
    Ui::Toggle(X + W - 112, ry + 12, 84, 28, cfg.liveCard, "เปิด", "ปิด");
    if (hover && Ui::Click) { sel = A_COUNT; cfg.liveCard = !cfg.liveCard; SaveConfig(); Nav(); }
}

static void DrawStatus(float cy) {
    Heading("งานปัจจุบัน", X + PAD + 6, cy);
    if (!JobManager::active) {
        Ui::Text("ตอนนี้ไม่มีงานที่กำลังทำ", X + PAD + 6, cy + 34, 18, Col(150, 165, 180));
    } else {
        Ui::Text(JobManager::active->Name(), X + PAD + 6, cy + 34, 20, Col(), AL_LEFT, true);
        Ui::Text(JobManager::active->objective, X + PAD + 6, cy + 64, 15, Col(190, 205, 220));
        if (Ui::Button(X + PAD + 6, cy + 96, W - 2 * PAD - 12, 36, "ยกเลิกงานนี้", Col(255, 90, 100))) { JobManager::Cancel(); return; }
    }
    float y = cy + 156;
    Ui::Rect(X + PAD + 6, y - 8, W - 2 * PAD - 12, 1, Col(255, 255, 255, 38));
    Heading("สถิติ", X + PAD + 6, y);
    y += 32;
    for (int k = 0; k < K_COUNT; k++) {
        Ui::Glass(X + PAD + 6, y + 2, 6, 24, KindCol(k), KindCol(k), 1, 3);
        Ui::Text(KindName(k), X + PAD + 24, y, 16, Col(200, 215, 230));
        Ui::Text(std::to_string(StatsCount(k)) + " งาน", X + W - PAD - 6, y, 16, Col(), AL_RIGHT, true);
        y += 28;
    }
    Ui::Rect(X + PAD + 6, y + 2, W - 2 * PAD - 12, 1, Col(255, 255, 255, 38));
    Ui::Text("รายได้สะสม", X + PAD + 6, y + 14, 17, Col(190, 205, 220));
    Ui::Text(MoneyStr((int)std::min<long long>(INT_MAX, g_earned)), X + W - PAD - 6, y + 10, 22, Col(70, 255, 130), AL_RIGHT, true);
}

static void Draw() {
    if (!isOpen) { Live::Draw(); return; }
    Ui::MouseFrame();
    Ui::Panel(X, Y, W, HH, Ui::kCyan, Ui::kMagenta, 18);
    Ui::Icon(Ui::IC_PAGER, X + 22, Y + 14, 34, Ui::kCyan);
    Ui::Text("เพจเจอร์ ศูนย์สั่งการ", X + 66, Y + 12, 25, Col(), AL_LEFT, true);
    if (Ui::Button(X + W - 98, Y + 14, 78, 34, "ปิด", Col(255, 80, 90))) { Close(); return; }
    Ui::Rect(X + 18, Y + 60, W - 36, 1, Col(255, 255, 255, 30));

    const char *names[T_COUNT] = {"ข้อความ", "รับงาน", "ตั้งค่า", "สถานะ"};
    for (int i = 0; i < T_COUNT; i++) {
        float tw = (W - 36) / (float)T_COUNT, tx = X + 18 + i * tw;
        bool hover = Ui::Hit(tx, Y + 68, tw, 38);
        if (i == tab) Ui::Glass(tx + 4, Y + 68, tw - 8, 38, Col(8, 70, 84, 235), Ui::kCyan, 2, 12);
        else if (hover) Ui::Glass(tx + 4, Y + 68, tw - 8, 38, Col(20, 34, 50, 200), Col(60, 85, 110), 0, 12);
        std::string label = names[i];
        if (i == T_INBOX && !inbox.empty()) label += " (" + std::to_string(inbox.size()) + ")";
        Ui::Text(label, tx + tw / 2.0f, Y + 74, 19, i == tab ? Col() : Col(190, 205, 220), AL_CENTER, i == tab);
        if (hover && Ui::Click && i != tab) { tab = i; sel = 0; Nav(); Input::CancelCapture(); }
    }
    float cy = Y + 122;
    if (tab == T_INBOX) DrawInbox(cy); else if (tab == T_DUTY) DrawDuty(cy); else if (tab == T_SETTINGS) DrawSettings(cy); else DrawStatus(cy);

    // footer: key caps
    float fy = Y + HH - 38;
    Ui::Rect(X + 18, fy - 6, W - 36, 1, Col(255, 255, 255, 30));
    float fx = X + 24;
    Ui::Text("เมาส์ คลิก", fx, fy, 14, Col(150, 165, 180));
    fx += Ui::TextW("เมาส์ คลิก", 14) + 14;
    fx += Ui::Badge(fx, fy - 2, "Enter", Col(120, 150, 180), 13) + 4;
    Ui::Text("ตกลง", fx, fy, 14, Col(150, 165, 180)); fx += Ui::TextW("ตกลง", 14) + 12;
    fx += Ui::Badge(fx, fy - 2, "Del", Col(120, 150, 180), 13) + 4;
    Ui::Text("ลบ", fx, fy, 14, Col(150, 165, 180)); fx += Ui::TextW("ลบ", 14) + 12;
    fx += Ui::Badge(fx, fy - 2, "< >", Col(120, 150, 180), 13) + 4;
    Ui::Text("แท็บ", fx, fy, 14, Col(150, 165, 180)); fx += Ui::TextW("แท็บ", 14) + 12;
    fx += Ui::Badge(fx, fy - 2, KeyName(cfg.binds[A_PAGER].key ? cfg.binds[A_PAGER].key : VK_F9), Col(120, 150, 180), 13) + 4;
    Ui::Text("ปิด", fx, fy, 14, Col(150, 165, 180));

    Ui::Cursor();
}

}  // namespace Pager