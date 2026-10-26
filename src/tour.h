// tour.h: long-distance sightseeing tour: numbered stops on the map, passengers get off for a cooldown then re-board
#pragma once

struct TourDef { std::string name; std::vector<Place> stops; };

static const char *kDefaultTours =
    "# รูปแบบ: [ชื่อทัวร์] แล้วตามด้วยจุดแวะบรรทัดละจุด  ชื่อจุด|X|Y|Z\r\n"
    "# จุดจะถูกดึงเข้าถนนที่ใกล้ที่สุดอัตโนมัติ เรียงตามลำดับ 1,2,3... บนแผนที่\r\n\r\n"
    "[ทัวร์ชมวิวลอสแซนโตส]\r\nป้ายฮอลลีวูด|719|1198|325\r\nหอดูดาว Galileo|-413|1173|325\r\nท่าเรือ Del Perro|-1850|-1233|13\r\n"
    "ชายหาด Vespucci|-1330|-1090|6\r\nสนามบิน LSIA|-1037|-2737|14\r\n\r\n"
    "[ทัวร์ทะเลทรายและทะเลสาบ]\r\nแซนดี้ชอร์ส|1960|3740|32\r\nทะเลสาบ Alamo|1300|4300|35\r\nเกรปซีด|1700|4800|41\r\n"
    "ไร่องุ่น Tongva|-1890|2045|140\r\nพาเลโต้เบย์|-130|6390|31\r\n\r\n"
    "[ทัวร์เหนือสุดสู่ชายฝั่งตะวันตก]\r\nพาเลโต้เบย์|-130|6390|31\r\nไร่องุ่น Tongva|-1890|2045|140\r\nท่าเรือ Chumash|-3240|960|12\r\nท่าเรือ Del Perro|-1850|-1233|13\r\n";

static std::vector<TourDef> g_tours;
static void LoadTours() {
    std::string f = g_data + "tours.txt";
    if (GetFileAttributesA(f.c_str()) == INVALID_FILE_ATTRIBUTES) WriteAll(f, kDefaultTours);
    g_tours.clear();
    for (auto &raw : SplitLines(ReadAll(f))) {
        std::string line = Trim(raw);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') { TourDef t; t.name = line.substr(1, line.size() - 2); g_tours.push_back(t); continue; }
        if (g_tours.empty()) continue;
        auto p = SplitStr(line, '|');
        if (p.size() != 4) continue;
        g_tours.back().stops.push_back(Place(p[0], (float)atof(p[1].c_str()), (float)atof(p[2].c_str()), (float)atof(p[3].c_str())));
    }
    g_tours.erase(std::remove_if(g_tours.begin(), g_tours.end(), [](const TourDef &t) { return t.stops.size() < 2; }), g_tours.end());
}

struct TourJob : Job {
    struct Pax { Ped p; int seat; };
    TourDef tour;
    std::vector<Vector3> stops;
    std::vector<Blip> blips;
    std::vector<Pax> pax;
    std::unique_ptr<VehicleGate> gate;
    Vehicle bus = 0;
    int stage = 0;   // 0 get bus, 1 drive, 2 sightseeing, 3 boarding, 4 finish
    int cur = 0, timerEnd = 0, boardStart = 0, endTime = 0, earned = 0;
    float startBody = 0;

    explicit TourJob(const TourDef &t) : tour(t) {}
    std::string Name() override { return "รถทัวร์: " + tour.name; }

    static std::unique_ptr<Offer> MakeOffer(Vector3) {
        if (g_tours.empty()) return nullptr;
        TourDef t = g_tours[RandInt(0, (int)g_tours.size())];
        float total = 0;
        for (size_t i = 1; i < t.stops.size(); i++) total += Dist2(t.stops[i].pos, t.stops[i - 1].pos);
        auto o = std::make_unique<Offer>();
        o->pos = t.stops[0].pos;
        o->pay = (int)t.stops.size() * 700 + (int)(total * 0.08f);
        o->title = "[รถทัวร์] " + t.name;
        o->line1 = std::to_string(t.stops.size()) + " จุดแวะ  ระยะทางรวม " + KmStr(total);
        o->create = [=]() { return std::unique_ptr<Job>(new TourJob(t)); };
        return o;
    }

    void Start() override {
        for (auto &s : tour.stops) stops.push_back(SnapNear(s.pos, 200.0f));
        gate.reset(new VehicleGate(K_TOUR, "รถทัวร์", {"coach"}, [](Vehicle v) { return VehModel(v, "coach") || VehModel(v, "bus"); }));
        objective = gate->Objective();
    }

    void Tick() override {
        if (stage == 0) {
            if (!gate->Update()) { objective = gate->Objective(); return; }
            bus = gate->veh;
            gate->Cleanup();
            startBody = VEHICLE::GET_VEHICLE_BODY_HEALTH(bus);
            SpawnPassengers();
            BuildBlips();
            stage = 1;
            Ui::Notify("ผู้โดยสาร " + std::to_string(pax.size()) + " คนขึ้นรถแล้ว เริ่มทัวร์ " + tour.name);
        }
        if (!Ex(bus) || IsDead(bus)) { Fail("รถทัวร์เสียหาย"); return; }
        switch (stage) {
            case 1: TickDrive(); break;
            case 2: TickSightsee(); break;
            case 3: TickBoard(); break;
            case 4:
                if (Now() > endTime) {
                    float lost = std::max(0.0f, startBody - VEHICLE::GET_VEHICLE_BODY_HEALTH(bus));
                    int bonus = lost < 100 ? 600 : 0;
                    Complete(earned + bonus, std::string("ทัวร์จบ ผู้โดยสารประทับใจ") + (bonus > 0 ? " (โบนัสขับนิ่ม)" : ""));
                }
                break;
        }
    }

    void SpawnPassengers() {
        int cap = VEHICLE::GET_VEHICLE_MODEL_NUMBER_OF_SEATS(ENTITY::GET_ENTITY_MODEL(bus));
        int n = std::max(2, std::min(cap - 1, 8));
        for (int i = 0; i < n; i++) {
            if (!VEHICLE::IS_VEHICLE_SEAT_FREE(bus, i, FALSE)) continue;
            Ped p = SpawnCiv(Pos(bus) + V(0, 0, 2.0f), 0.0f);
            if (!p) continue;
            PED::SET_PED_INTO_VEHICLE(p, bus, i);
            pax.push_back({p, i});
        }
    }

    // numbered blips for every stop not yet visited; the GPS route points to the current one only
    void BuildBlips() {
        for (auto &b : blips) DelBlip(b);
        blips.clear();
        for (size_t i = 0; i < stops.size(); i++) {
            Blip b = HUD::ADD_BLIP_FOR_COORD(stops[i]);
            HUD::SET_BLIP_SPRITE(b, SPR_STD);
            HUD::SET_BLIP_COLOUR(b, (int)i == cur ? COL_YELLOW : COL_BLUE);
            BlipSetName(b, ("Tour stop " + std::to_string(i + 1)).c_str());
            HUD::SHOW_NUMBER_ON_BLIP(b, (int)i + 1);
            if ((int)i == cur) RouteSet(b);
            blips.push_back(b);
        }
    }

    void AdvanceBlips() {
        DelBlip(blips[cur]);
        cur++;
        if (cur < (int)stops.size()) {
            HUD::SET_BLIP_COLOUR(blips[cur], COL_YELLOW);
            RouteSet(blips[cur]);
        }
    }

    void TickDrive() {
        Vector3 target = stops[cur];
        float d = Dist(Pos(bus), target);
        objective = "จุดที่ " + std::to_string(cur + 1) + "/" + std::to_string(stops.size()) + ": " + tour.stops[cur].name;
        info = "ห่าง " + KmStr(d);
        Ui::Zone(target, 16.0f, Ui::kYellow, "จุดแวะที่ " + std::to_string(cur + 1));
        if (d < 30.0f && SpeedOf(bus) < 2.5f) {
            stage = 2;
            timerEnd = Now() + cfg.tourStopSec * 1000;
            for (auto &x : pax) if (Ex(x.p)) TASK::TASK_LEAVE_VEHICLE(x.p, bus, 0);
            earned += 700;
            AdvanceBlips();
            Ui::Notify("ถึงจุดที่ " + std::to_string(cur) + " " + tour.stops[cur - 1].name + " ผู้โดยสารลงชมวิว " + std::to_string(cfg.tourStopSec) + " วินาที");
        }
    }

    // the bus cannot leave while passengers are outside: handbrake held and the accelerate / reverse controls disabled
    void Lock(bool on) {
        VEHICLE::SET_VEHICLE_HANDBRAKE(bus, on ? TRUE : FALSE);
        if (on) {
            PAD::DISABLE_CONTROL_ACTION(0, 71, TRUE);   // vehicle accelerate
            PAD::DISABLE_CONTROL_ACTION(0, 72, TRUE);   // vehicle brake / reverse
        }
    }

    void TickSightsee() {
        Lock(true);
        int left = (timerEnd - Now() + 999) / 1000;
        objective = "ผู้โดยสารกำลังชมวิว (" + tour.stops[cur - 1].name + ")";
        info = "ออกรถอีก " + ClockStr(left) + "  (ออกรถไม่ได้จนกว่าผู้โดยสารจะขึ้นครบ)" + (cur < (int)stops.size() ? "\nจุดต่อไป: " + tour.stops[cur].name : "");
        for (auto &x : pax) {
            if (!Ex(x.p) || PED::IS_PED_IN_VEHICLE(x.p, bus, FALSE)) continue;
            if (!PED::IS_PED_USING_ANY_SCENARIO(x.p) && SpeedOf(x.p) < 0.5f)
                Scenario(x.p, RandInt(0, 2) == 0 ? "WORLD_HUMAN_TOURIST_MAP" : "WORLD_HUMAN_TOURIST_MOBILE");
        }
        if (left <= 0) {
            stage = 3;
            boardStart = Now();
            for (auto &x : pax) if (Ex(x.p)) { TASK::CLEAR_PED_TASKS(x.p); TASK::TASK_ENTER_VEHICLE(x.p, bus, 15000, x.seat, 1.0f, 1, nullptr); }
            Ui::Notify("หมดเวลาชมวิว ผู้โดยสารกำลังเดินขึ้นรถ");
        }
    }

    void TickBoard() {
        Lock(true);
        objective = "รอผู้โดยสารขึ้นรถ";
        info = "ออกรถไม่ได้จนกว่าผู้โดยสารจะขึ้นครบ";
        bool all = true;
        for (auto &x : pax) if (Ex(x.p) && !PED::IS_PED_IN_VEHICLE(x.p, bus, FALSE)) all = false;
        if (!all && Now() - boardStart > 25000) {
            for (auto &x : pax) if (Ex(x.p) && !PED::IS_PED_IN_VEHICLE(x.p, bus, FALSE)) PED::SET_PED_INTO_VEHICLE(x.p, bus, x.seat);
            all = true;
        }
        if (!all) return;
        Lock(false);
        if (cur >= (int)stops.size()) { stage = 4; endTime = Now() + 1500; objective = "ทัวร์จบแล้ว"; }
        else { stage = 1; Ui::Notify("ออกเดินทางไปจุดที่ " + std::to_string(cur + 1) + ": " + tour.stops[cur].name); }
    }

    void Cleanup() override {
        if (gate) gate->Cleanup();
        for (auto &b : blips) DelBlip(b);
        blips.clear();
        for (auto &x : pax) Rel(x.p);
        if (Ex(bus)) VEHICLE::SET_VEHICLE_HANDBRAKE(bus, FALSE);
        Rel(bus);
    }
};

