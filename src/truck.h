// truck.h: long-haul trucking with cargo types
#pragma once

struct TruckJob : Job {
    enum Cargo { C_GENERAL, C_FRAGILE, C_RUSH, C_LIQUID };
    Cargo cargo;
    Place depot, target;
    int basePay;

    std::unique_ptr<VehicleGate> gate;
    Vehicle truck = 0, trailer = 0;
    Blip bDepot = 0, bTarget = 0;
    int stage = 0;   // 0 get truck+trailer, 1 to depot, 2 loading, 3 transport, 4 unloading
    int holdStart = 0, limitEnd = 0;
    float trailerHp0 = 0;

    TruckJob(Cargo c, const Place &d, const Place &t, int pay) : cargo(c), depot(d), target(t), basePay(pay) { depot.pos = SnapNear(depot.pos); target.pos = SnapNear(target.pos); }

    static const char *CargoName(Cargo c) {
        switch (c) { case C_FRAGILE: return "สินค้าเปราะบาง"; case C_RUSH: return "สินค้าด่วน"; case C_LIQUID: return "ของเหลว (ถังน้ำมัน)"; }
        return "สินค้าทั่วไป";
    }
    std::string Name() override { return std::string("ขนส่งสินค้า: ") + CargoName(cargo); }

    static const std::vector<Place> &Depots_() {
        static std::vector<Place> d = {Place("ท่าเรือขนส่งสินค้า Elysian Island", 1198, -3115, 5.6f), Place("คลังสินค้า La Mesa", 903, -1765, 31),
                                       Place("คลังสินค้า Cypress Flats", 1000, -2530, 28), Place("ลานตู้คอนเทนเนอร์ Terminal", 800, -2965, 6)};
        return d;
    }
    static const std::vector<Place> &Targets() {
        static std::vector<Place> t = {Place("ปั๊มน้ำมัน แซนดี้ชอร์ส", 2003, 3770, 32), Place("โกดัง พาเลโต้เบย์", -125, 6280, 31),
                                       Place("โรงงาน Harmony", 590, 2760, 42), Place("ฟาร์ม เกรปซีด", 1700, 4800, 41),
                                       Place("โรงงานไวน์ Tongva", -1890, 2045, 140), Place("ท่าเรือ Chumash", -3240, 960, 12),
                                       Place("โรงไฟฟ้า Palmer-Taylor", 2740, 1440, 24), Place("ลานขนถ่าย Davis Quartz", 2700, 2790, 36),
                                       Place("คลังสินค้าสนามบิน Zancudo", -2300, 3200, 32)};
        return t;
    }

    static std::unique_ptr<Offer> MakeOffer(Vector3 pp) {
        Place dep = Depots_()[0];
        for (auto &d : Depots_()) if (Dist2(d.pos, pp) < Dist2(dep.pos, pp)) dep = d;
        const Place *tg = nullptr;
        for (int i = 0; i < 20 && !tg; i++) { const Place &c = Targets()[RandInt(0, (int)Targets().size())]; if (Dist2(c.pos, dep.pos) > 2500) tg = &c; }
        if (!tg) return nullptr;
        Cargo cg = (Cargo)RandInt(0, 4);
        float dist = Dist2(dep.pos, tg->pos);
        int pay = 900 + (int)(dist * 0.4f);
        if (cg == C_FRAGILE) pay = (int)(pay * 1.4f);
        if (cg == C_RUSH) pay = (int)(pay * 1.6f);
        if (cg == C_LIQUID) pay = (int)(pay * 1.25f);
        auto o = std::make_unique<Offer>();
        o->pos = dep.pos;
        o->pay = pay;
        o->title = std::string("[ขนส่ง] ") + CargoName(cg);
        o->line1 = "รับที่ " + dep.name + " ส่ง " + tg->name;
        Place t = *tg;
        o->create = [=]() { return std::unique_ptr<Job>(new TruckJob(cg, dep, t, pay)); };
        return o;
    }

    void Start() override {
        Cargo cg = cargo;
        gate.reset(new VehicleGate(K_TRUCK, "หัวลากพร้อมพ่วง", {"phantom", "hauler"},
                                   [](Vehicle v) { return VEHICLE::IS_VEHICLE_ATTACHED_TO_TRAILER(v) != 0; },
                                   [cg](Vehicle cab) {
                                       const char *tm = cg == C_LIQUID ? "tanker" : cg == C_FRAGILE ? "trailers2" : "trailers";
                                       if (!ModelOk(tm)) tm = "trailers";
                                       Vehicle tr = SpawnVeh(tm, OffW(cab, 0.0f, -11.0f, 0.0f), Hdg(cab));
                                       if (tr) VEHICLE::ATTACH_VEHICLE_TO_TRAILER(cab, tr, 1.0f);
                                   }));
        objective = gate->Objective();
    }

    Vehicle GetTrailer() {
        Vehicle t = 0;
        if (VEHICLE::GET_VEHICLE_TRAILER_VEHICLE(truck, &t) && t) return t;
        return 0;
    }

    void Tick() override {
        if (stage == 0) {
            if (!gate->Update()) { objective = gate->Objective(); return; }
            truck = gate->veh;
            gate->Cleanup();
            trailer = GetTrailer();
            if (!trailer) { Fail("ไม่พบรถพ่วง"); return; }
            trailerHp0 = VEHICLE::GET_VEHICLE_BODY_HEALTH(trailer);
            bDepot = BlipPos(depot.pos, SPR_STD, COL_YELLOW, "Depot", true);
            stage = 1;
            Ui::Notify("ศูนย์: ไปรับสินค้าที่ " + depot.name);
        }
        if (!Ex(truck) || IsDead(truck)) { Fail("หัวลากเสียหาย"); return; }
        trailer = GetTrailer();
        if (!trailer) { Fail("รถพ่วงหลุดหรือถูกทำลาย"); return; }
        switch (stage) {
            case 1: TickDepot(); break;
            case 2: TickHold(depot, "กำลังโหลดสินค้า", [this]() { BeginTransport(); }); break;
            case 3: TickTransport(); break;
            case 4: TickHold(target, "กำลังขนสินค้าลง", [this]() { Finish(); }); break;
        }
    }

    void TickDepot() {
        float d = Dist(Pos(truck), depot.pos);
        objective = "ไปรับสินค้าที่ " + depot.name;
        info = "ห่าง " + KmStr(d);
        Ui::Zone(depot.pos, 12.0f, Ui::kYellow, "จุดรับสินค้า");
        if (d < 22.0f && SpeedOf(truck) < 2.0f) { stage = 2; holdStart = Now(); }
    }

    // stay still inside the zone for 6 seconds to load / unload
    void TickHold(const Place &where, const char *label, std::function<void()> done) {
        float d = Dist(Pos(truck), where.pos);
        if (d > 28.0f || SpeedOf(truck) > 2.5f) {
            stage = stage == 2 ? 1 : 3;
            Ui::Notify("ขยับรถ! ต้องจอดนิ่งในโซนจนกว่าจะเสร็จ");
            return;
        }
        int sec = (Now() - holdStart) / 1000;
        objective = std::string(label) + " " + std::to_string(std::min(6, sec)) + "/6";
        info.clear();
        if (sec >= 6) done();
    }

    void BeginTransport() {
        stage = 3;
        DelBlip(bDepot);
        bTarget = BlipPos(target.pos, SPR_STD, COL_GREEN, "Delivery", true);
        trailerHp0 = VEHICLE::GET_VEHICLE_BODY_HEALTH(trailer);
        if (cargo == C_RUSH) limitEnd = Now() + ((int)(Dist2(depot.pos, target.pos) / 15.0f) + 60) * 1000;
        Ui::Notify("โหลดสินค้าเสร็จ ไปส่งที่ " + target.name);
    }

    void TickTransport() {
        float d = Dist(Pos(truck), target.pos);
        float lost = std::max(0.0f, trailerHp0 - VEHICLE::GET_VEHICLE_BODY_HEALTH(trailer));
        objective = "ไปส่งที่ " + target.name;
        std::string line = "ห่าง " + KmStr(d);
        if (cargo == C_FRAGILE) line += "\nความเสียหายสินค้า " + std::to_string((int)std::min(100.0f, lost / 3.5f)) + "%";
        if (cargo == C_RUSH) line += "\nเวลา " + ClockStr((limitEnd - Now()) / 1000);
        info = line;
        Ui::Zone(target.pos, 12.0f, Ui::kGreen, "จุดส่งสินค้า");
        if (cargo == C_FRAGILE && lost > 350.0f) { Fail("สินค้าเปราะบางเสียหายหนักเกินไป"); return; }
        if (d < 22.0f && SpeedOf(truck) < 2.0f) { stage = 4; holdStart = Now(); }
    }

    void Finish() {
        float lost = std::max(0.0f, trailerHp0 - VEHICLE::GET_VEHICLE_BODY_HEALTH(trailer));
        float factor = 1.0f - std::min(0.5f, lost / 1000.0f);
        int pay = (int)(basePay * factor);
        std::string note = "ส่งสินค้าครบ";
        if (cargo == C_RUSH) {
            if (Now() <= limitEnd) { pay += 600; note = "ส่งทันเวลา โบนัสด่วน"; }
            else { pay = (int)(pay * 0.7f); note = "ส่งช้ากว่ากำหนด"; }
        }
        Complete(pay, note);
    }

    void Cleanup() override {
        if (gate) gate->Cleanup();
        DelBlip(bDepot); DelBlip(bTarget);
        Rel(trailer); Rel(truck);
    }
};
