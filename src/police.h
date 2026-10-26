// police.h: police chase. Ram the fleeing suspect's car 10 times, he surrenders, pick him up, deliver to a random station.
#pragma once

struct PoliceChaseJob : Job {
    static const int HitsNeeded = 10;
    Vector3 spot;
    float spotHeading;
    std::string carModel;
    int basePay;

    std::unique_ptr<VehicleGate> gate;
    Vehicle cruiser = 0, suspectCar = 0;
    Ped suspect = 0;
    Blip bArea = 0, bSuspect = 0, bStation = 0;
    Place station;
    int stage = 0;   // 0 get police car, 1 chase, 2 surrender, 3 boarding, 4 deliver, 5 handover
    int hits = 0, lastHit = 0, startTime = 0, stageT = 0, awayStart = 0, seat = 2;
    // reckless flight: pick far escape points away from the player, never brake for anything, charge forward again after every crash
    float cruise = 24.0f;
    Vector3 goal = V(0, 0, 0);
    int goalAt = 0, stuckSince = 0, lastBoost = 0;
    // driving style flags: no stopping for cars/peds, no steering around anything, may drive into oncoming traffic,
    // shortcut links + switched-off nodes allowed, no cruise-speed limiting by road type
    static const int kCrazyStyle = 512 + 256 + 262144;
    Blip bSpotPt = 0;   // normal coordinate blip carrying the GPS route (radius blips cannot have a route)

    PoliceChaseJob(Vector3 s, float h, const std::string &car, int pay) : spot(s), spotHeading(h), carModel(car), basePay(pay) {}
    std::string Name() override { return "ตำรวจไล่ล่าคนร้าย"; }

    static std::unique_ptr<Offer> MakeOffer(Vector3 pp) {
        Vector3 pos;
        float hd;
        if (!FindRoad(pp, 600.0f, 2000.0f, pos, hd)) return nullptr;
        static const char *cars[] = {"sultan", "buffalo", "schafter2", "jackal", "banshee", "comet2", "feltzer2", "kuruma"};
        std::string car = cars[RandInt(0, 8)];
        int pay = 1500 + (int)(Dist(pos, pp) * 0.2f);
        auto o = std::make_unique<Offer>();
        o->pos = pos;
        o->pay = pay;
        o->title = "[ตำรวจ] ไล่ล่าคนร้ายหนีหมายจับ แถว " + StreetAt(pos);
        o->line1 = "ชนรถคนร้าย " + std::to_string(HitsNeeded) + " ครั้งให้ยอมจำนน แล้วคุมตัวส่งสถานี";
        o->create = [=]() { return std::unique_ptr<Job>(new PoliceChaseJob(pos, hd, car, pay)); };
        return o;
    }

    static bool IsPolice(Vehicle v) {
        static const char *m[] = {"police", "police2", "police3", "police4", "sheriff", "sheriff2", "fbi", "fbi2"};
        for (auto n : m) if (VehModel(v, n)) return true;
        return false;
    }

    void Start() override {
        gate.reset(new VehicleGate(K_POLICE, "รถตำรวจ", {"police", "police2", "police3"}, [](Vehicle v) { return IsPolice(v); }));
        objective = gate->Objective();
    }

    void Tick() override {
        if (stage == 0) {
            if (!gate->Update()) { objective = gate->Objective(); return; }
            cruiser = gate->veh;
            gate->Cleanup();
            VEHICLE::SET_VEHICLE_SIREN(cruiser, TRUE);
            startTime = Now();
            bArea = HUD::ADD_BLIP_FOR_RADIUS(spot, 220.0f);
            HUD::SET_BLIP_COLOUR(bArea, COL_RED);
            HUD::SET_BLIP_ALPHA(bArea, 110);
            bSpotPt = BlipPos(spot, SPR_WANTED, COL_RED, "Suspect area", true);
            stage = 1;
            Ui::Notify("ศูนย์: พบรถคนร้ายแถวจุดสีแดง ตามไปชนให้หยุด!");
        }
        if (cruiser && stage >= 1 && stage < 5 && (!Ex(cruiser) || IsDead(cruiser))) { Fail("รถตำรวจเสียหายหนัก"); return; }
        switch (stage) {
            case 1: TickChase(); break;
            case 2: TickSurrender(); break;
            case 3: TickBoard(); break;
            case 4: TickDeliver(); break;
            case 5: if (Now() - stageT > 3500) Finish(); break;
        }
    }

    void TickChase() {
        Ped p = PlayerPed();
        Vector3 pp = Pos(p);
        if (!suspectCar) {
            float d0 = Dist(pp, spot);
            objective = "ตามหาคนร้าย ห่าง " + KmStr(d0);
            Ui::Zone(spot, 15.0f, Ui::kRed, "พื้นที่คนร้าย");
            if (d0 < 300.0f) SpawnSuspect();
            return;
        }
        if (!Ex(suspectCar) || IsDead(suspectCar) || !Ex(suspect)) { Fail("คนร้ายหายไปจากระบบ"); return; }

        Vehicle mine = PED::IS_PED_IN_ANY_VEHICLE(p, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(p, FALSE) : 0;
        if (mine && Now() - lastHit > 1200 && ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(suspectCar, mine, TRUE)) {
            hits++;
            lastHit = Now();
            ENTITY::CLEAR_ENTITY_LAST_DAMAGE_ENTITY(suspectCar);
            Ui::Notify("ชนรถคนร้าย! " + std::to_string(hits) + "/" + std::to_string(HitsNeeded), 3.0f);
            Ui::Beep("TIMER_STOP", "HUD_MINI_GAME_SOUNDSET");
            if (hits >= HitsNeeded) { Surrender(); return; }
            ApplyHitEffect();
        } else if (mine) {
            ENTITY::CLEAR_ENTITY_LAST_DAMAGE_ENTITY(suspectCar);
        }

        // the car gets wrecked but never explodes or dies before the 10th hit
        if (VEHICLE::GET_VEHICLE_ENGINE_HEALTH(suspectCar) < 250.0f) VEHICLE::SET_VEHICLE_ENGINE_HEALTH(suspectCar, 250.0f);
        if (VEHICLE::GET_VEHICLE_PETROL_TANK_HEALTH(suspectCar) < 500.0f) VEHICLE::SET_VEHICLE_PETROL_TANK_HEALTH(suspectCar, 500.0f);

        Rampage();

        float d = Dist(pp, Pos(suspectCar));
        objective = "ชนรถคนร้ายให้หยุด";
        info = "ชนแล้ว " + std::to_string(hits) + "/" + std::to_string(HitsNeeded) + "\nสภาพรถคนร้าย " + std::to_string(std::max(0, 100 - hits * 100 / HitsNeeded)) + "%\nห่าง " + KmStr(d);
        if (d > 1400.0f) {
            if (!awayStart) awayStart = Now();
            if (Now() - awayStart > 30000) { Fail("คนร้ายหนีพ้นระยะ"); return; }
            info += "\nคนร้ายกำลังหนีไกล!";
        } else awayStart = 0;
    }

    void SpawnSuspect() {
        suspectCar = SpawnVeh(carModel.c_str(), spot, spotHeading);
        if (!suspectCar) { Fail("สร้างรถคนร้ายไม่ได้"); return; }
        static const char *peds[] = {"g_m_y_mexgoon_01", "g_m_y_lost_01", "g_m_y_ballasout_01", "a_m_y_mexthug_01", "g_m_y_salvagoon_01"};
        suspect = SpawnPedModel(peds[RandInt(0, 5)], spot, spotHeading);
        if (!suspect) suspect = SpawnCiv(spot, spotHeading);
        if (!suspect) { Fail("สร้างคนร้ายไม่ได้"); return; }
        PED::SET_PED_INTO_VEHICLE(suspect, suspectCar, -1);
        ENTITY::SET_ENTITY_INVINCIBLE(suspect, TRUE);
        ENTITY::SET_ENTITY_PROOFS(suspectCar, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE);
        VEHICLE::SET_VEHICLE_TYRES_CAN_BURST(suspectCar, FALSE);
        PED::SET_PED_KEEP_TASK(suspect, TRUE);
        VEHICLE::SET_VEHICLE_ENGINE_ON(suspectCar, TRUE, TRUE, TRUE);
        PED::SET_DRIVER_ABILITY(suspect, 1.0f);
        PED::SET_DRIVER_AGGRESSIVENESS(suspect, 1.0f);
        PED::SET_PED_STEERS_AROUND_VEHICLES(suspect, FALSE);
        PED::SET_PED_STEERS_AROUND_PEDS(suspect, FALSE);
        PED::SET_PED_STEERS_AROUND_OBJECTS(suspect, FALSE);
        DriveFast(24.0f);
        DelBlip(bArea);
        DelBlip(bSpotPt);
        bSuspect = BlipEnt(suspectCar, SPR_WANTED, COL_RED, "Suspect");
        HUD::SET_BLIP_FLASHES(bSuspect, TRUE);
        RouteSet(bSuspect);
        Ui::Notify("เห็นรถคนร้ายแล้ว! มันขับเร็วมาก");
    }

    // new escape point 600-1100 m away from the player (random +-70 degrees), on a main road
    void PickGoal() {
        Vector3 me = Pos(suspectCar), pl = PlayerPos();
        float base = atan2f(me.y - pl.y, me.x - pl.x) + (Rand01() - 0.5f) * 2.4f;
        float d = 600.0f + Rand01() * 500.0f;
        goal = SnapRoad(V(me.x + cosf(base) * d, me.y + sinf(base) * d, me.z));
        goalAt = Now();
    }

    void DriveFast(float speed) {
        cruise = speed;
        PickGoal();
        TASK::TASK_VEHICLE_DRIVE_TO_COORD(suspect, suspectCar, goal, speed, 0, ENTITY::GET_ENTITY_MODEL(suspectCar), kCrazyStyle, 10.0f, 0.0f);
        TASK::SET_DRIVE_TASK_CRUISE_SPEED(suspect, speed);
    }

    // runs every frame during the chase. No velocity hacks (they launched the car): only driving tasks.
    // New escape point every 5 s or when reached; after a crash the drive task is re-issued at once so the car
    // keeps going instead of idling; wedged for 1.2 s: face the escape point and drive again; flipped: back on its wheels.
    void Rampage() {
        Vector3 vel = ENTITY::GET_ENTITY_VELOCITY(suspectCar), fwd = ENTITY::GET_ENTITY_FORWARD_VECTOR(suspectCar);
        float speed = Len2(vel);
        float along = vel.x * fwd.x + vel.y * fwd.y;
        int now = Now();
        if (now - goalAt > 5000 || Dist2(Pos(suspectCar), goal) < 100.0f) DriveFast(cruise);

        Vector3 rot = ENTITY::GET_ENTITY_ROTATION(suspectCar, 2);
        bool upright = fabsf(rot.x) < 40.0f && fabsf(rot.y) < 40.0f;
        if (!upright && !ENTITY::IS_ENTITY_IN_AIR(suspectCar) && now - lastBoost > 2500) {
            VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(suspectCar, 5.0f);
            lastBoost = now;
        }

        if (speed < 3.0f || along < -0.5f) {
            if (!stuckSince) stuckSince = now;
            if (now - stuckSince > 1200 && now - lastBoost > 1200) {
                if (upright) ENTITY::SET_ENTITY_HEADING(suspectCar, HeadingTo(Pos(suspectCar), goal));
                DriveFast(cruise);
                stuckSince = now;
                lastBoost = now;
            }
        } else stuckSince = 0;
    }
    void ApplyHitEffect() {
        float speed = std::max(15.0f, 24.0f - hits * 1.0f);
        VEHICLE::SET_VEHICLE_ENGINE_HEALTH(suspectCar, std::max(300.0f, 1000.0f - hits * 90.0f));
        DriveFast(speed);
    }

    void Surrender() {
        stage = 2; stageT = Now();
        DelBlip(bSuspect);
        VEHICLE::SET_VEHICLE_ENGINE_ON(suspectCar, FALSE, TRUE, TRUE);
        TASK::CLEAR_PED_TASKS(suspect);
        TASK::TASK_LEAVE_VEHICLE(suspect, suspectCar, 0);
        info.clear();
        objective = "คนร้ายยอมจำนน";
        Ui::Notify("คนร้ายยอมจำนน ลงจากรถมามอบตัว ขับรถเข้าไปใกล้ๆ เพื่อรับตัว");
    }

    void TickSurrender() {
        if (!Ex(suspect)) { Fail("คนร้ายหายไป"); return; }
        Ped p = PlayerPed();
        if (!PED::IS_PED_IN_ANY_VEHICLE(suspect, FALSE) && Now() - stageT > 800) {
            if (!bSuspect) { bSuspect = BlipEnt(suspect, SPR_STD, COL_GREEN, "Suspect"); RouteSet(bSuspect); }
            if (Now() - stageT > 1800) TASK::TASK_HANDS_UP(suspect, -1, p, -1, 0);
        }
        objective = "ขับรถไปใกล้คนร้ายเพื่อรับตัว";
        Ui::Zone(Pos(suspect), 8.0f, Ui::kGreen, "คนร้ายมอบตัว");
        Vehicle mine = PED::IS_PED_IN_ANY_VEHICLE(p, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(p, FALSE) : 0;
        if (!mine) { info = "ต้องอยู่ในรถ"; return; }
        info.clear();
        if (!PED::IS_PED_IN_ANY_VEHICLE(suspect, FALSE) && Now() - stageT > 2500 && Dist(Pos(mine), Pos(suspect)) < 12.0f && SpeedOf(mine) < 4.0f) {
            seat = PickSeat(mine);
            if (seat == -2) { Ui::Notify("รถคันนี้ไม่มีที่นั่งว่างสำหรับคนร้าย"); return; }
            cruiser = mine;
            TASK::CLEAR_PED_TASKS(suspect);
            TASK::TASK_ENTER_VEHICLE(suspect, cruiser, 12000, seat, 1.0f, 1, nullptr);
            stage = 3; stageT = Now();
            objective = "คนร้ายกำลังขึ้นรถ";
        }
    }

    static int PickSeat(Vehicle v) {
        int order[] = {2, 1, 0};
        for (int s : order) if (VEHICLE::IS_VEHICLE_SEAT_FREE(v, s, FALSE)) return s;
        return -2;
    }

    void TickBoard() {
        if (!Ex(suspect)) { Fail("คนร้ายหายไป"); return; }
        if (PED::IS_PED_IN_VEHICLE(suspect, cruiser, FALSE)) { DelBlip(bSuspect); BeginDeliver(); return; }
        if (Now() - stageT > 12000) PED::SET_PED_INTO_VEHICLE(suspect, cruiser, seat);
    }

    Place PickStation() {
        std::vector<Place> list;
        for (auto &d : Depots::all) if (d.kind == K_POLICE) { Place pl; pl.name = d.name; pl.pos = d.pos; list.push_back(pl); }
        if (list.empty()) {
            list.push_back(Place("สถานีตำรวจ Mission Row", 425.1f, -979.5f, 30.7f));
            list.push_back(Place("สถานีตำรวจ Vespucci", -1096, -807, 19.1f));
            list.push_back(Place("สถานีตำรวจ Sandy Shores", 1856.5f, 3689.5f, 34.3f));
        }
        std::vector<Place> nearby;
        for (auto &s : list) if (Dist2(s.pos, PlayerPos()) < 7000.0f) nearby.push_back(s);
        if (nearby.empty()) nearby = list;
        return nearby[RandInt(0, (int)nearby.size())];
    }

    void BeginDeliver() {
        station = PickStation();
        stage = 4;
        bStation = BlipPos(station.pos, SPR_POLICE, COL_GREEN, "Police station", true);
        Ui::Notify("คุมตัวคนร้ายแล้ว ส่งที่ " + station.name);
    }

    void TickDeliver() {
        if (!Ex(suspect) || !PED::IS_PED_IN_VEHICLE(suspect, cruiser, FALSE)) { Fail("คนร้ายหลุดจากรถ"); return; }
        if (!Ex(cruiser) || IsDead(cruiser)) { Fail("รถตำรวจเสียหาย"); return; }
        float d = Dist(Pos(cruiser), station.pos);
        objective = "ส่งตัวคนร้ายที่ " + station.name;
        info = "ห่าง " + KmStr(d);
        Ui::Zone(station.pos, 14.0f, Ui::kGreen, "สถานีตำรวจ");
        if (d < 24.0f && SpeedOf(cruiser) < 2.5f) {
            stage = 5; stageT = Now();
            DelBlip(bStation);
            TASK::TASK_LEAVE_VEHICLE(suspect, cruiser, 0);
            objective = "เจ้าหน้าที่รับตัวคนร้าย";
            info.clear();
        }
    }

    void Finish() {
        if (Ex(suspect)) TASK::TASK_GO_STRAIGHT_TO_COORD(suspect, station.pos, 1.0f, -1, 0.0f, 0.5f);
        int secs = (Now() - startTime) / 1000;
        int pay = basePay;
        std::string note = "ส่งตัวคนร้ายสำเร็จ";
        if (secs < 360) { pay += 600; note = "จับกุมรวดเร็ว โบนัสพิเศษ"; }
        Complete(pay, note);
    }

    void Cleanup() override {
        if (gate) gate->Cleanup();
        DelBlip(bArea); DelBlip(bSpotPt); DelBlip(bSuspect); DelBlip(bStation);
        if (cruiser && Ex(cruiser)) VEHICLE::SET_VEHICLE_SIREN(cruiser, FALSE);
        Rel(suspect); Rel(suspectCar); Rel(cruiser);
    }
};
