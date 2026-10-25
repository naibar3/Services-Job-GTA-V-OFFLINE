// taxi.h: taxi fares with random passenger events
#pragma once

static const std::vector<Place> &Landmarks() {
    static std::vector<Place> p = {
        Place("สนามบินนานาชาติลอสแซนโตส (LSIA)", -1037, -2737, 14), Place("ท่าเรือ Del Perro", -1850, -1233, 13),
        Place("ลานกว้าง Legion Square", 195, -934, 30), Place("สถานีตำรวจ Mission Row", 428, -980, 30),
        Place("โรงพยาบาล Pillbox Hill", 298, -584, 43), Place("คาสิโน Diamond", 925, 47, 81),
        Place("หอดูดาว Galileo", -413, 1173, 325), Place("ไร่องุ่น Tongva", -1890, 2045, 140),
        Place("สนามกีฬา Maze Bank Arena", -251, -2036, 30), Place("ศูนย์การค้า Rockford Plaza", -150, -230, 44),
        Place("Mirror Park", 1070, -700, 57), Place("แซนดี้ชอร์ส", 1960, 3740, 32), Place("เกรปซีด", 1700, 4800, 41),
        Place("พาเลโต้เบย์", -130, 6390, 31), Place("ชายหาด Vespucci", -1330, -1090, 6), Place("ท่าเรือ Chumash", -3240, 960, 12),
        Place("ตลาด Strawberry (Davis)", 120, -1720, 29), Place("Vinewood Boulevard", 300, 180, 104)};
    return p;
}

struct TaxiJob : Job {
    enum Evt { E_NORMAL, E_URGENT, E_DRUNK, E_CHANGE, E_RUNNER };
    Vector3 pickup;
    float pickHeading;
    Place dest, dest2;
    Evt evt;

    std::unique_ptr<VehicleGate> gate;
    Vehicle cab = 0;
    Ped pax = 0;
    Blip bPick = 0, bDest = 0;
    int stage = 0;   // 0 get taxi, 1 to pickup, 2 boarding, 3 ride, 4 passenger leaves
    float traveled = 0, startBody = 0, initialDist = 0;
    int boardStart = 0, limitEnd = 0, leaveStart = 0;
    bool changed = false;

    TaxiJob(Vector3 p, float h, const Place &d, const Place &d2, Evt e) : pickup(p), pickHeading(h), dest(d), dest2(d2), evt(e) {}
    std::string Name() override { return "แท็กซี่"; }

    static std::unique_ptr<Offer> MakeOffer(Vector3 pp) {
        Vector3 pos;
        float hd;
        if (!FindRoad(pp, 250.0f, 1300.0f, pos, hd)) return nullptr;
        const auto &L = Landmarks();
        const Place *d = nullptr, *d2 = nullptr;
        for (int i = 0; i < 20 && !d; i++) { const Place &c = L[RandInt(0, (int)L.size())]; float k = Dist2(c.pos, pos); if (k > 1500 && k < 9000) d = &c; }
        if (!d) return nullptr;
        for (int i = 0; i < 20 && !d2; i++) { const Place &c = L[RandInt(0, (int)L.size())]; if (&c != d && Dist2(c.pos, pos) > 1500) d2 = &c; }
        int r = RandInt(0, 100);
        Evt e = r < 45 ? E_NORMAL : r < 62 ? E_URGENT : r < 77 ? E_DRUNK : r < 92 ? E_CHANGE : E_RUNNER;
        int pay = 100 + (int)(Dist2(pos, d->pos) * 0.09f);
        if (e == E_URGENT) pay = (int)(pay * 1.5f);
        auto o = std::make_unique<Offer>();
        o->pos = pos;
        o->pay = pay;
        std::string tag = e == E_URGENT ? "ด่วน! " : e == E_DRUNK ? "(เมา) " : "";
        o->title = "[แท็กซี่] " + tag + "รับที่ " + StreetAt(pos);
        o->line1 = "ไปส่ง " + d->name;
        Place dd = *d, dd2 = d2 ? *d2 : *d;
        o->create = [=]() { return std::unique_ptr<Job>(new TaxiJob(pos, hd, dd, dd2, e)); };
        return o;
    }

    void Start() override {
        // any car with a free passenger seat works as a taxi (3+ seats: no bikes); depots only supply a car when you have none
        gate.reset(new VehicleGate(K_TAXI, "แท็กซี่", {"taxi"}, [](Vehicle v) { return FreePassengerSeat(v) != -2; }));
        objective = gate->Objective();
    }

    int seat = 2;

    float Fare() const { return 80.0f + traveled * 0.085f; }

    void Tick() override {
        if (stage == 0) {
            if (!gate->Update()) { objective = gate->Objective(); return; }
            cab = gate->veh;
            gate->Cleanup();
            startBody = VEHICLE::GET_VEHICLE_BODY_HEALTH(cab);
            bPick = BlipPos(pickup, SPR_STD, COL_YELLOW, "Passenger", true);
            stage = 1;
            Ui::Notify("ศูนย์: ผู้โดยสารรออยู่ที่จุดสีเหลือง");
        }
        // before the passenger is aboard the job follows whatever car the player drives (any car is a taxi), so swapping
        // cars on the way is fine; the distance is always measured from the car the player is actually in
        if (stage == 1 && PED::IS_PED_IN_ANY_VEHICLE(PlayerPed(), FALSE)) {
            Vehicle v = PED::GET_VEHICLE_PED_IS_IN(PlayerPed(), FALSE);
            if (v && v != cab && VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1, FALSE) == PlayerPed() && FreePassengerSeat(v) != -2) {
                cab = v;
                startBody = VEHICLE::GET_VEHICLE_BODY_HEALTH(cab);
            }
        }
        if (!Ex(cab) || IsDead(cab)) { Fail("แท็กซี่เสียหาย"); return; }
        if (pax && Ex(pax) && IsDead(pax)) { Fail("ผู้โดยสารเสียชีวิต"); return; }
        switch (stage) {
            case 1: TickPickup(); break;
            case 2: TickBoard(); break;
            case 3: TickRide(); break;
            case 4: TickLeave(); break;
        }
    }

    void TickPickup() {
        bool inCab = PED::IS_PED_IN_VEHICLE(PlayerPed(), cab, FALSE) != 0;
        float d = Dist(Pos(inCab ? (int)cab : (int)PlayerPed()), pickup);
        objective = inCab ? "ไปรับผู้โดยสาร ห่าง " + KmStr(d) : "ขึ้นรถคันไหนก็ได้เพื่อไปรับผู้โดยสาร ห่าง " + KmStr(d);
        if (!inCab) { Ui::Zone(pickup, 9.0f, Ui::kYellow, "จุดรับผู้โดยสาร"); return; }
        Ui::Zone(pickup, 9.0f, Ui::kYellow, "จุดรับผู้โดยสาร");
        if (!pax && d < 220.0f) {
            Vector3 sp = pickup;
            Vector3 safe;
            if (PATHFIND::GET_SAFE_COORD_FOR_PED(pickup, TRUE, &safe, 0)) sp = safe;
            pax = SpawnCiv(sp, pickHeading);
            if (!pax) { Fail("สร้างผู้โดยสารไม่ได้"); return; }
            Scenario(pax, "WORLD_HUMAN_STAND_IMPATIENT");
            DelBlip(bPick);
            bPick = BlipEnt(pax, SPR_STD, COL_YELLOW, "Passenger");
            RouteSet(bPick);
        }
        if (pax && d < 16.0f && SpeedOf(cab) < 4.0f) {
            seat = FreePassengerSeat(cab);
            if (seat == -2) { Fail("รถคันนี้ไม่มีที่นั่งว่างสำหรับผู้โดยสาร"); return; }
            TASK::TASK_ENTER_VEHICLE(pax, cab, 10000, seat, 1.0f, 1, nullptr);
            boardStart = Now();
            stage = 2;
            objective = "ผู้โดยสารกำลังขึ้นรถ";
        }
    }

    void TickBoard() {
        if (PED::IS_PED_IN_VEHICLE(pax, cab, FALSE)) { DelBlip(bPick); BeginRide(); return; }
        if (Now() - boardStart > 12000) PED::SET_PED_INTO_VEHICLE(pax, cab, seat);
    }

    void BeginRide() {
        stage = 3;
        traveled = 0;
        initialDist = Dist2(Pos(cab), dest.pos);
        bDest = BlipPos(SnapRoad(dest.pos), SPR_STD, COL_GREEN, "Destination", true);
        switch (evt) {
            case E_URGENT: limitEnd = Now() + ((int)(initialDist / 17.0f) + 40) * 1000; Ui::Notify("ผู้โดยสาร: \"รีบมาก! ถึงทันเวลาจะให้โบนัส\""); break;
            case E_DRUNK: Ui::Notify("ผู้โดยสารเมา ขับนิ่มๆ อย่าชน จะได้ทิป"); break;
            case E_RUNNER: Ui::Notify("ผู้โดยสารดูมีพิรุธ..."); break;
            default: Ui::Notify("ผู้โดยสาร: \"ไป " + dest.name + " ครับ/ค่ะ\""); break;
        }
    }

    void TickRide() {
        if (!PED::IS_PED_IN_VEHICLE(pax, cab, FALSE)) { Fail("ผู้โดยสารลงจากรถกลางทาง"); return; }
        float sp = SpeedOf(cab);
        if (sp > 0.5f) traveled += sp * Dt();
        float d = Dist(Pos(cab), dest.pos);

        if (evt == E_CHANGE && !changed && d < initialDist * 0.55f) {
            changed = true;
            dest = dest2;
            initialDist = Dist2(Pos(cab), dest.pos);
            DelBlip(bDest);
            bDest = BlipPos(SnapRoad(dest.pos), SPR_STD, COL_GREEN, "Destination", true);
            Ui::Notify("ผู้โดยสารขอเปลี่ยนจุดหมายเป็น " + dest.name);
            d = Dist(Pos(cab), dest.pos);
        }
        objective = "ไปส่งที่ " + dest.name;
        std::string timer;
        if (evt == E_URGENT) {
            int left = (limitEnd - Now()) / 1000;
            timer = "\nเวลา " + (left >= 0 ? ClockStr(left) : "เกินเวลา -" + ClockStr(-left));
        }
        info = "มิเตอร์ " + MoneyStr((int)Fare()) + "  ห่าง " + KmStr(d) + timer;
        Vector3 dp = SnapRoad(dest.pos);
        Ui::Zone(dp, 11.0f, Ui::kGreen, "จุดส่งผู้โดยสาร");
        if (Dist(Pos(cab), dp) < 24.0f && sp < 2.5f) {
            stage = 4;
            leaveStart = Now();
            TASK::TASK_LEAVE_VEHICLE(pax, cab, evt == E_RUNNER ? 4160 : 0);
            DelBlip(bDest);
            objective = "ผู้โดยสารกำลังลงรถ";
            info.clear();
        }
    }

    void TickLeave() {
        if (PED::IS_PED_IN_VEHICLE(pax, cab, FALSE) && Now() - leaveStart < 8000) return;
        if (Now() - leaveStart < 1500) return;
        float fare = Fare();
        int pay = (int)fare;
        std::string note = "ส่งผู้โดยสารเรียบร้อย";
        if (evt == E_RUNNER) {
            TASK::TASK_SMART_FLEE_PED(pax, PlayerPed(), 200.0f, -1, FALSE, FALSE);
            pay = (int)(fare * 0.3f);
            note = "ผู้โดยสารวิ่งหนีไม่จ่ายค่าโดยสาร";
        } else {
            TASK::TASK_WANDER_STANDARD(pax, 10.0f, 10);
            if (evt == E_URGENT && Now() <= limitEnd) { pay = (int)(pay * 1.5f); note = "ทันเวลา! โบนัสด่วน"; }
            float lost = startBody - VEHICLE::GET_VEHICLE_BODY_HEALTH(cab);
            if (evt == E_DRUNK && lost < 40.0f) { pay += (int)(fare * 0.4f); note = "ขับนิ่ม ได้ทิปพิเศษ"; }
            if (lost > 200.0f) pay = (int)(pay * 0.8f);
        }
        Complete(pay, note);
    }

    void Cleanup() override {
        if (gate) gate->Cleanup();
        DelBlip(bPick); DelBlip(bDest);
        Rel(pax); Rel(cab);
    }
};
