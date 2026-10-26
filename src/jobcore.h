// jobcore.h: Job base class, offers, depots (job pick-up points), vehicle gate, job manager
#pragma once

static const char *KindName(int k) {
    switch (k) {
        case K_TOW: return "ลากรถ / ยึดรถ";
        case K_TAXI: return "แท็กซี่";
        case K_TOUR: return "รถทัวร์ชมวิว";
        case K_TRUCK: return "รถบรรทุกส่งของ";
        case K_AMB: return "รถพยาบาล";
        case K_POLICE: return "ตำรวจไล่ล่า";
    }
    return "?";
}

struct Job;
struct Offer {
    int kind = 0;
    std::string title, line1;
    int pay = 0;
    Vector3 pos = V(0, 0, 0);
    int born = 0;
    bool read = false;
    std::function<std::unique_ptr<Job>()> create;
};

struct Job {
    int kind = 0;
    std::string objective, info;
    bool finished = false, success = false;
    int paid = 0;
    virtual ~Job() {}
    virtual std::string Name() = 0;
    virtual void Start() = 0;
    virtual void Tick() = 0;
    virtual void OnAction() {}
    virtual void Cleanup() {}

    void Complete(int pay, const std::string &msg) {
        pay = (int)lroundf(pay * cfg.payMult);
        AddMoney(pay);
        paid = pay;
        Log("job %d COMPLETE pay=%d: %s", kind, pay, msg.c_str());
        success = true;
        finished = true;
        Ui::Notify(msg + "  ได้รับ " + MoneyStr(pay), 8.0f);
        Ui::Beep("PROPERTY_PURCHASE", "HUD_AWARDS");
    }
    void Fail(const std::string &msg) {
        Log("job %d FAIL: %s", kind, msg.c_str());
        success = false;
        finished = true;
        Ui::Notify("งานล้มเหลว: " + msg, 8.0f);
        Ui::Beep("CANCEL", "HUD_FREEMODE_SOUNDSET");
    }
};

// ---------------------------------------------------------------- depots
struct Slot { Vehicle v = 0, t = 0; Vector3 pos = V(0, 0, 0); int retry = 0; bool used = false; };
struct Depot {
    int kind = 0;
    std::string name;
    Vector3 pos = V(0, 0, 0);
    float heading = 0;
    int count = 1;
    std::vector<Slot> slots;
    Blip blip = 0;
};

namespace Depots {

static std::vector<Depot> all;
static bool blipsMade = false;

static const char *kDefault =
    "# รูปแบบ: [ประเภทงาน] แล้วจุดรับงานบรรทัดละจุด   ชื่อ|X|Y|Z|ทิศรถ(0-360)|จำนวนคัน\r\n"
    "# ประเภทงาน: Tow, Taxi, Tour, Truck, Ambulance, Police\r\n"
    "# กด Ctrl+Alt+F8 ในเกมเพื่อบันทึกพิกัด/ทิศที่ยืนอยู่ลง log.txt แล้วนำมาแก้ไฟล์นี้\r\n\r\n"
    "[Ambulance]\r\n"
    "รพ. Pillbox Hill|299.5|-584.5|43.2|340|1\r\nรพ. Central Los Santos|340.0|-1396.0|32.5|50|1\r\n"
    "รพ. Mount Zonah|-449.5|-340.5|34.5|80|1\r\nรพ. Sandy Shores|1839.5|3672.5|34.3|210|1\r\nรพ. Paleto Bay|-247.5|6331.0|32.4|40|1\r\n\r\n"
    "[Tow]\r\nลานยึดรถ Davis|409.2|-1623.5|29.3|230|1\r\nลานยึดรถ Sandy Shores|1853.5|3686.0|34.2|210|1\r\nลานยึดรถ Paleto Bay|-452.0|6014.0|31.7|135|1\r\n\r\n"
    "[Taxi]\r\nอู่แท็กซี่ Downtown Cab Co.|903.0|-170.0|74.1|57|1\r\nจุดจอดแท็กซี่ สนามบิน LSIA|-1034.0|-2730.0|13.9|330|1\r\n\r\n"
    "[Tour]\r\nสถานีรถทัวร์ Los Santos|453.0|-607.0|28.5|270|1\r\n\r\n"
    "# สถานีตำรวจ: เป็นทั้งจุดรับรถตำรวจ และจุดส่งตัวคนร้าย (ระบบสุ่มเลือกสถานี)\r\n"
    "[Police]\r\n"
    "สถานีตำรวจ Mission Row|425.1|-979.5|30.7|90|1\r\nสถานีตำรวจ Davis (Sheriff)|360.9|-1584.0|29.3|320|1\r\n"
    "สถานีตำรวจ Vespucci|-1096.0|-807.0|19.1|130|1\r\nสถานีตำรวจ Rockford Hills|-561.0|-132.0|38.2|200|1\r\n"
    "สถานีตำรวจ La Mesa|826.0|-1290.0|28.2|90|1\r\nสถานีตำรวจ Vinewood|620.0|1.5|82.8|250|1\r\n"
    "สถานีตำรวจ Sandy Shores|1856.5|3689.5|34.3|210|1\r\nสถานีตำรวจ Paleto Bay|-448.0|6008.0|31.7|135|1\r\n\r\n"
    "[Truck]\r\nท่าเรือ Elysian Island|1198.0|-3115.0|5.6|90|1\r\nคลังสินค้า La Mesa|903.0|-1765.0|31.0|270|1\r\n";

static int KindFromName(const std::string &n) {
    std::string u;
    for (char c : n) u += (char)tolower((unsigned char)c);
    if (u == "tow" || u == "towrepo") return K_TOW;
    if (u == "taxi") return K_TAXI;
    if (u == "tour") return K_TOUR;
    if (u == "truck") return K_TRUCK;
    if (u == "ambulance") return K_AMB;
    if (u == "police") return K_POLICE;
    return -1;
}

static void Load() {
    std::string f = g_data + "depots.txt";
    if (GetFileAttributesA(f.c_str()) == INVALID_FILE_ATTRIBUTES) WriteAll(f, kDefault);
    all.clear();
    int cur = -1;
    for (auto &raw : SplitLines(ReadAll(f))) {
        std::string line = Trim(raw);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') { cur = KindFromName(line.substr(1, line.size() - 2)); continue; }
        if (cur < 0) continue;
        auto p = SplitStr(line, '|');
        if (p.size() < 5) continue;
        Depot d;
        d.kind = cur;
        d.name = p[0];
        d.pos = V((float)atof(p[1].c_str()), (float)atof(p[2].c_str()), (float)atof(p[3].c_str()));
        d.heading = (float)atof(p[4].c_str());
        d.count = p.size() > 5 ? std::max(1, std::min(6, atoi(p[5].c_str()))) : 1;

        // the coordinates in the file are approximate (a hospital bay, a car park...): park the vehicles on the nearest road
        // instead, in the road's driving direction, so they never spawn inside buildings or upside down on kerbs
        Vector3 np;
        float nh = 0;
        // nodeType 0 = main roads only: type 1 also returns driveways / car parks, which is where vehicles got stuck
        if (PATHFIND::GET_CLOSEST_VEHICLE_NODE_WITH_HEADING(d.pos, &np, &nh, 0, 3.0f, 0) && Dist(np, d.pos) < 150.0f) {
            Log("depot '%s' snapped to road (%.0f m): %.1f %.1f %.1f heading %.0f", d.name.c_str(), Dist(np, d.pos), np.x, np.y, np.z, nh);
            d.pos = np;
            d.heading = nh;
        } else {
            Log("depot '%s' no main road within 150 m, kept as written", d.name.c_str());
        }
        all.push_back(d);
    }
    Log("depots loaded: %d", (int)all.size());
}

static std::vector<std::string> SlideValid() {
    std::vector<std::string> v;
    for (auto &m : cfg.slideModels) if (ModelOk(m.c_str())) v.push_back(m);
    return v;
}

static std::vector<std::string> Models(int kind) {
    switch (kind) {
        case K_TOW: { std::vector<std::string> v; for (auto &m : SlideValid()) v.push_back(m); return v; }
        case K_TAXI: return {"taxi"};
        case K_TOUR: return {"coach"};
        case K_TRUCK: return {"phantom", "hauler"};
        case K_AMB: return {"ambulance"};
        case K_POLICE: return {"police", "police2", "police3"};
    }
    return {"taxi"};
}
static float Spacing(int k) {
    switch (k) { case K_TOUR: return 14.0f; case K_TRUCK: return 24.0f; case K_TOW: return 8.0f; case K_POLICE: return 6.0f; case K_TAXI: return 5.5f; }
    return 6.5f;
}
static int Sprite(int k) {
    switch (k) { case K_TOW: return SPR_TOW; case K_TAXI: return SPR_TAXI; case K_TOUR: return SPR_BUS; case K_TRUCK: return SPR_TRUCK; case K_AMB: return SPR_HOSP; case K_POLICE: return SPR_POLICE; }
    return SPR_STD;
}
static const char *BlipLabel(int k) {
    switch (k) {
        case K_TOW: return "Job depot: Tow"; case K_TAXI: return "Job depot: Taxi"; case K_TOUR: return "Job depot: Tour bus";
        case K_TRUCK: return "Job depot: Trucking"; case K_AMB: return "Job depot: Ambulance"; case K_POLICE: return "Job depot: Police station";
    }
    return "Job depot";
}

static Depot *Nearest(int kind, Vector3 from) {
    Depot *best = nullptr;
    for (auto &d : all) {
        if (d.kind != kind) continue;
        if (!best || Dist2(d.pos, from) < Dist2(best->pos, from)) best = &d;
    }
    return best;
}

// the player took this vehicle for a job: it is no longer a depot vehicle
static void Claim(Vehicle v) {
    if (!v) return;
    for (auto &d : all) for (auto &s : d.slots) {
        if (s.v == v) { s.v = 0; s.t = 0; s.used = true; s.retry = Now() + 15000; }
    }
}

// a slot whose vehicle was taken is refilled only when nobody can see it happen (player far away) and the spot is empty,
// otherwise the new vehicle would spawn inside the one the player just drove off in
static bool SlotClear(const Depot &d, const Slot &s) {
    if (Dist(PlayerPos(), s.pos) < 80.0f) return false;
    if (VEHICLE::IS_ANY_VEHICLE_NEAR_POINT(s.pos, 6.0f)) return false;
    if (d.kind == K_TRUCK) {
        float a = d.heading * 0.0174533f;
        Vector3 trailerPos = s.pos + V(sinf(a), -cosf(a), 0) * 11.0f;
        if (VEHICLE::IS_ANY_VEHICLE_NEAR_POINT(trailerPos, 6.0f)) return false;
    }
    return true;
}

static void SpawnSlot(Depot &d, Slot &s, Vector3 pos) {
    auto ms = Models(d.kind);
    Vehicle v = SpawnVeh(ms[RandInt(0, (int)ms.size())].c_str(), pos, d.heading);
    if (!v) return;
    s.v = v;
    if (d.kind == K_TRUCK) {
        Vehicle t = SpawnVeh("trailers", OffW(v, 0.0f, -11.0f, 0.0f), d.heading);
        if (t) { VEHICLE::ATTACH_VEHICLE_TO_TRAILER(v, t, 1.0f); s.t = t; }
    }
}

static void Fill(Depot &d) {
    float a = d.heading * 0.0174533f;
    Vector3 back = V(sinf(a), -cosf(a), 0);   // along the road, behind the first vehicle
    float sp = Spacing(d.kind);
    if ((int)d.slots.size() < d.count) d.slots.resize(d.count);
    for (int i = 0; i < d.count; i++) {
        Slot &s = d.slots[i];
        s.pos = d.pos + back * (i * sp);
        if (s.v && Ex(s.v) && !IsDead(s.v)) {
            // driven away from the spot: it is the player's now, free the slot for a new vehicle
            if (Dist(Pos(s.v), s.pos) > 40.0f) { Rel(s.v); Rel(s.t); s.v = 0; s.t = 0; s.used = true; }
            else continue;
        }
        if (Now() < s.retry) continue;
        s.retry = Now() + 5000;
        if (s.used && !SlotClear(d, s)) continue;
        s.used = false;
        SpawnSlot(d, s, s.pos);
    }
}

static void Clear(Depot &d) {
    for (auto &s : d.slots) {
        bool home = s.v && Ex(s.v) && Dist(Pos(s.v), s.pos) < 40.0f && VEHICLE::IS_VEHICLE_SEAT_FREE(s.v, -1, FALSE) &&
                    VEHICLE::GET_PED_IN_VEHICLE_SEAT(s.v, -1, FALSE) == 0;
        if (home) { Del(s.v); Del(s.t); }
        else { Rel(s.v); Rel(s.t); }
        s.v = 0; s.t = 0;
    }
    d.slots.clear();
}

static void Tick() {
    if (all.empty()) return;
    Vector3 pp = PlayerPos();
    if (!blipsMade) {
        blipsMade = true;
        for (auto &d : all) {
            d.blip = HUD::ADD_BLIP_FOR_COORD(d.pos);
            HUD::SET_BLIP_SPRITE(d.blip, Sprite(d.kind));
            HUD::SET_BLIP_COLOUR(d.blip, COL_BLUE);
            HUD::SET_BLIP_SCALE(d.blip, 0.8f);
            HUD::SET_BLIP_AS_SHORT_RANGE(d.blip, TRUE);
            BlipSetName(d.blip, BlipLabel(d.kind));
        }
    }
    for (auto &d : all) {
        float dist = Dist(pp, d.pos);
        if (dist < cfg.depotMarkerDist) Ui::Zone(d.pos, 5.0f, Ui::kBlue, "จุดรับงาน: " + d.name, false);
        if (dist < 220.0f) Fill(d);
        else if (dist > 520.0f) Clear(d);
    }
}

static void Shutdown() {
    for (auto &d : all) { Clear(d); DelBlip(d.blip); }
}

}  // namespace Depots

// ---------------------------------------------------------------- vehicle gate
// The job needs a certain vehicle: use the one the player already drives, otherwise guide to the nearest depot
// (or spawn one next to the player if this job type has no depot).
struct VehicleGate {
    int kind;
    std::string label;
    std::vector<std::string> models;
    std::function<bool(Vehicle)> accept;
    std::function<void(Vehicle)> onSpawn;
    Vehicle veh = 0;
    Blip blip = 0, depotBlip = 0;
    Depot *depot = nullptr;
    int retry = 0;

    VehicleGate(int k, const std::string &lbl, std::vector<std::string> m, std::function<bool(Vehicle)> a, std::function<void(Vehicle)> sp = nullptr)
        : kind(k), label(lbl), models(std::move(m)), accept(std::move(a)), onSpawn(std::move(sp)) {}

    std::string Objective() const {
        if (depot && kind == K_TAXI) return "ขึ้นรถคันไหนก็ได้ หรือไปเอารถที่จุดรับงาน " + depot->name;
        if (depot) return "ไปที่จุดรับงาน " + depot->name + " แล้วขึ้นรถ" + label;
        return "ขึ้นรถ" + label + " (จุดสีฟ้าบนแผนที่)";
    }

    bool Update() {
        Ped p = PlayerPed();
        if (PED::IS_PED_IN_ANY_VEHICLE(p, FALSE)) {
            Vehicle v = PED::GET_VEHICLE_PED_IS_IN(p, FALSE);
            if (v && accept(v) && VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1, FALSE) == p) {
                veh = v;
                Depots::Claim(v);
                DelBlip(blip);
                DelBlip(depotBlip);
                return true;
            }
        }
        if (!depot) depot = Depots::Nearest(kind, PlayerPos());
        if (depot) {
            // taxi: any car works, so the depot is only a hint on the map (no route, no marker, nothing forced)
            bool optional = kind == K_TAXI;
            if (!depotBlip) {
                depotBlip = BlipPos(depot->pos, SPR_STD, COL_BLUE, "Job depot", !optional);
                Ui::Notify(optional ? "ขึ้นรถคันไหนก็ได้เพื่อเริ่มงาน (จุดรับงาน " + depot->name + " มีรถให้ถ้าไม่มีรถ)"
                                    : "ไปรับรถที่จุดรับงาน: " + depot->name);
            }
            if (!optional) Ui::Zone(depot->pos, 6.0f, Ui::kBlue, "จุดรับงาน");
            return false;
        }
        if (!veh || !Ex(veh)) {
            if (Now() < retry) return false;
            retry = Now() + 1500;
            Vector3 pos;
            float hd;
            if (!FindRoad(PlayerPos(), 18.0f, 45.0f, pos, hd)) return false;
            veh = SpawnVeh(models[RandInt(0, (int)models.size())].c_str(), pos, hd);
            if (!veh) return false;
            if (onSpawn) onSpawn(veh);
            DelBlip(blip);
            blip = BlipEnt(veh, SPR_STD, COL_BLUE, "Job vehicle");
            RouteSet(blip);
            Ui::Notify("จัดรถ" + label + "ไว้ให้แล้ว ใกล้ตัวคุณ");
        }
        return false;
    }
    void Cleanup() { DelBlip(blip); DelBlip(depotBlip); }
};

// ---------------------------------------------------------------- job manager
namespace JobManager {

static std::unique_ptr<Job> active;

static bool Begin(Offer &o) {
    if (active) { Ui::Notify("มีงานที่กำลังทำอยู่แล้ว (เปิดเพจเจอร์เพื่อยกเลิก)"); return false; }
    try {
        auto j = o.create();
        j->kind = o.kind;
        j->Start();
        std::string nm = j->Name();
        Log("job %d BEGIN: %s", o.kind, nm.c_str());
        active = std::move(j);
        Ui::Notify("รับงาน: " + nm);
        return true;
    } catch (...) {
        Log("exception starting job");
        Ui::Notify("เริ่มงานไม่สำเร็จ ดู log.txt");
        return false;
    }
}
static void Cancel() {
    if (active) Log("job %d CANCELLED", active->kind);
    if (!active) return;
    active->Cleanup();
    Ui::Notify("ยกเลิกงาน: " + active->Name());
    active.reset();
}
static void Shutdown() {
    if (active) { active->Cleanup(); active.reset(); }
}

static bool g_pagerOpen = false;   // set by pager so the HUD hides while the menu is open

static void DrawHud(Job &j) {
    if (g_pagerOpen) return;
    float x = 22, y = 22;
    std::vector<std::string> extra;
    if (!j.info.empty()) extra = SplitStr(j.info, '\n');
    std::string name = j.Name();
    float w = std::max(Ui::TextW(name, 19, true), Ui::TextW(j.objective, 16));
    for (auto &e : extra) w = std::max(w, Ui::TextW(e, 16));
    w = std::min(560.0f, std::max(320.0f, w + 40.0f));
    float h = 74 + extra.size() * 27;
    Ui::Panel(x, y, w, h, Col(0, 225, 255), Col(60, 230, 130), 14);
    Ui::Text(name, x + 20, y + 10, 19, Col(255, 214, 79), AL_LEFT, true);
    Ui::Rect(x + 16, y + 42, w - 32, 1, Col(255, 255, 255, 30));
    Ui::Text(j.objective, x + 20, y + 48, 16, Col());
    for (size_t i = 0; i < extra.size(); i++) Ui::Text(extra[i], x + 20, y + 76 + i * 27, 16, Col(190, 215, 235));
}
static void Tick() {
    if (!active) return;
    try {
        if (ENTITY::IS_ENTITY_DEAD(PlayerPed(), FALSE)) {
            active->Cleanup();
            active.reset();
            Ui::Notify("งานถูกยกเลิก (ผู้เล่นตาย)");
            return;
        }
        active->Tick();
        if (active->finished) {
            active->Cleanup();
            if (active->success) StatsAdd(active->kind, active->paid);
            active.reset();
            return;
        }
        DrawHud(*active);
    } catch (const std::exception &e) {
        Log("C++ exception in job tick: %s", e.what());
        if (active) { active->Cleanup(); active.reset(); }
        Ui::Notify("งานมีข้อผิดพลาด ถูกยกเลิก (ดู log.txt)");
    } catch (...) {
        Log("exception in job tick");
        if (active) { active->Cleanup(); active.reset(); }
        Ui::Notify("งานมีข้อผิดพลาด ถูกยกเลิก (ดู log.txt)");
    }
}

}  // namespace JobManager

