// towrepo.h: tow broken cars / repossess cars (one event type, dispatched randomly). Hook truck (E) or slide-bed winch.
#pragma once

struct TowRepoJob : Job {
    bool repo;
    Vector3 spot;
    float spotHeading;
    std::string carModel;
    Place drop;
    int basePay;

    std::unique_ptr<VehicleGate> gate;
    int stage = 0;   // 0 get truck, 1 go/hook, 2 deliver, 3 released, 6 winch up, 7 winch down
    Vehicle truck = 0, target = 0;
    Ped owner = 0;
    Blip bSpot = 0, bDrop = 0, bTarget = 0;
    int hookTime = 0, ownerTime = 0, doneTime = 0;
    bool ownerRan = false, slide = false;
    std::unique_ptr<Winch> winch;

    TowRepoJob(bool r, Vector3 s, float h, const std::string &car, const Place &d, int pay)
        : repo(r), spot(s), spotHeading(h), carModel(car), drop(d), basePay(pay) {}

    std::string Name() override { return repo ? "ยึดรถค้างชำระ" : "ลากรถเสีย"; }

    static const std::vector<Place> &Drops() {
        static std::vector<Place> d = {Place("ลานยึดรถ ลอสแซนโตส (Davis)", 409.2f, -1623.5f, 29.3f),
                                       Place("ลานยึดรถ แซนดี้ชอร์ส", 1853.5f, 3686.0f, 34.2f),
                                       Place("ลานยึดรถ พาเลโต้เบย์", -452.0f, 6014.0f, 31.7f)};
        return d;
    }

    static std::unique_ptr<Offer> MakeOffer(Vector3 pp) {
        Vector3 pos;
        float hd;
        if (!FindRoad(pp, 500.0f, 1800.0f, pos, hd)) return nullptr;
        Place best = Drops()[0];
        for (auto &d : Drops()) if (Dist2(d.pos, pos) < Dist2(best.pos, pos)) best = d;
        float hauling = Dist(pos, best.pos);
        bool repo = RandInt(0, 100) < 40;
        static const char *tow[] = {"asea", "premier", "sentinel", "blista", "emperor", "ingot", "stanier", "fugitive", "primo", "washington", "dilettante", "tailgater"};
        static const char *rep[] = {"sultan", "schafter2", "felon", "jackal", "oracle2", "comet2", "banshee", "carbonizzare", "dominator", "rapidgt", "zion", "buffalo2"};
        std::string car = repo ? rep[RandInt(0, 12)] : tow[RandInt(0, 12)];
        int pay = repo ? 900 + (int)(hauling * 0.22f) : 350 + (int)(hauling * 0.12f);
        std::string street = StreetAt(pos);
        auto o = std::make_unique<Offer>();
        o->pos = pos;
        o->pay = pay;
        o->title = repo ? "[ยึดรถ] ค้างชำระ แถว " + street : "[ลากรถ] รถเสีย แถว " + street;
        o->line1 = "ส่งที่ " + best.name;
        o->create = [=]() { return std::unique_ptr<Job>(new TowRepoJob(repo, pos, hd, car, best, pay)); };
        return o;
    }

    void Start() override {
        std::vector<std::string> models = {"towtruck", "towtruck2"};
        for (auto &m : Depots::SlideValid()) models.push_back(m);
        gate.reset(new VehicleGate(K_TOW, "ลากรถ", models, [](Vehicle v) {
            return VehModel(v, "towtruck") || VehModel(v, "towtruck2") || Slide::IsSlide(v);
        }));
        objective = gate->Objective();
    }

    void Tick() override {
        if (stage == 0) {
            if (!gate->Update()) { objective = gate->Objective(); return; }
            truck = gate->veh;
            slide = Slide::IsSlide(truck);
            gate->Cleanup();
            bSpot = BlipPos(spot, SPR_TOW, COL_ORANGE, repo ? "Repo" : "Tow", true);
            stage = 1;
            Ui::Notify(repo ? "ศูนย์: ตามไปยึดรถคันนั้น ระวังเจ้าของ!" : "ศูนย์: ไปรับรถเสียตามจุดบนแผนที่");
        }
        if (!Ex(truck) || IsDead(truck)) { Fail("รถลากเสียหาย"); return; }
        if (target && Ex(target) && IsDead(target) && stage < 3) { Fail("รถเป้าหมายพัง"); return; }

        switch (stage) {
            case 1: TickGo(); break;
            case 2: TickDeliver(); break;
            case 6:
                objective = "วินช์กำลังดึงรถขึ้นถาด";
                winch->Tick();
                if (winch->done) AfterHook();
                break;
            case 7:
                objective = "วินช์กำลังปล่อยรถลงพื้น";
                winch->Tick();
                if (winch->done) { stage = 3; doneTime = Now() + 1500; objective = "ปล่อยรถเรียบร้อย"; }
                break;
            case 3:
                if (Now() > doneTime)
                    Complete(repo ? basePay + ((Now() - hookTime) < 150000 ? 400 : 0) : basePay, repo ? "ยึดรถสำเร็จ" : "ลากรถสำเร็จ");
                break;
        }
    }

    void TickGo() {
        Vector3 pp = PlayerPos();
        if (!target) {
            objective = "ไปยังจุดเกิดเหตุ ห่าง " + KmStr(Dist(pp, spot));
            Ui::Zone(spot, 10.0f, Ui::kYellow, repo ? "จุดยึดรถ" : "จุดรถเสีย");
            if (Dist(pp, spot) < 230.0f) SpawnTarget();
            return;
        }
        float reach = slide ? 14.0f : 11.0f;
        float d = Dist(Pos(truck), Pos(target));
        objective = slide ? "ถอยท้ายรถสไลด์เข้าหารถเป้าหมาย" : "ถอยท้ายรถลากเข้าหารถเป้าหมาย";
        if (d < reach) { Ui::Prompt(std::string("กด ") + BindText(A_ACTION) + (slide ? " เพื่อดึงรถขึ้นถาด" : " เพื่อเกี่ยวรถ")); info.clear(); }
        else info = "ห่าง " + KmStr(d);

        if (repo && owner && Ex(owner) && !ownerRan && d < 25.0f && Now() > ownerTime) {
            ownerRan = true;
            TASK::TASK_GO_TO_ENTITY(owner, truck, -1, 3.0f, 3.0f, 1073741824.0f, 0);
            AUDIO::PLAY_PED_AMBIENT_SPEECH_NATIVE(owner, "GENERIC_INSULT_MED", "SPEECH_PARAMS_FORCE_SHOUTED", 0);
            Ui::Notify("เจ้าของรถเห็นเข้าแล้ว วิ่งมาตะโกนด่า!");
        }
    }

    void SpawnTarget() {
        target = SpawnVeh(carModel.c_str(), spot, spotHeading);
        if (!target) { Fail("สร้างรถเป้าหมายไม่ได้"); return; }
        DelBlip(bSpot);
        if (repo) {
            VEHICLE::SET_VEHICLE_DOORS_LOCKED(target, 4);
            VEHICLE::SET_VEHICLE_ENGINE_ON(target, FALSE, TRUE, TRUE);
        } else {
            VEHICLE::SET_VEHICLE_ENGINE_HEALTH(target, 120.0f);
            VEHICLE::SET_VEHICLE_ENGINE_ON(target, FALSE, TRUE, TRUE);
            VEHICLE::SET_VEHICLE_DOOR_OPEN(target, 4, FALSE, FALSE);
        }
        bTarget = BlipEnt(target, SPR_STD, COL_RED, repo ? "Repo target" : "Broken car");
        RouteSet(bTarget);
        owner = SpawnCiv(OffW(target, -2.4f, 2.8f, 0.0f), spotHeading + 90.0f);
        if (owner) { Scenario(owner, "WORLD_HUMAN_STAND_MOBILE"); ownerTime = Now() + 2500; }
    }

    void OnAction() override {
        if (stage == 1 && target && Ex(target) && Dist(Pos(truck), Pos(target)) < (slide ? 14.0f : 11.0f)) Hook();
        else if (stage == 2 && Ex(truck) && Dist(Pos(truck), drop.pos) < 28.0f && SpeedOf(truck) < 4.0f) Release();
    }

    void Hook() {
        if (!VEHICLE::IS_VEHICLE_SEAT_FREE(target, -1, FALSE)) { Ui::Notify("มีคนอยู่ในรถ!"); return; }
        if (slide) {
            if (!Slide::IsBehind(truck, target, 14.0f)) { Ui::Notify("ถอยท้ายรถสไลด์ให้อยู่ด้านหลังรถเป้าหมายก่อน"); return; }
            winch.reset(new Winch(truck, target, true));
            stage = 6;
            return;
        }
        VEHICLE::ATTACH_VEHICLE_TO_TOW_TRUCK(truck, target, TRUE, V(0, 0, 0));
        AfterHook();
    }

    void AfterHook() {
        stage = 2;
        hookTime = Now();
        DelBlip(bTarget);
        bDrop = BlipPos(drop.pos, SPR_TOW, COL_GREEN, "Impound", true);
        objective = "ลากไปส่งที่ " + drop.name;
        Ui::Notify("เกี่ยวรถแล้ว ขับไปส่งที่ " + drop.name);
        if (repo && owner && Ex(owner)) {
            TASK::TASK_GO_TO_ENTITY(owner, truck, -1, 2.0f, 3.0f, 1073741824.0f, 0);
            AUDIO::PLAY_PED_AMBIENT_SPEECH_NATIVE(owner, "GENERIC_INSULT_HIGH", "SPEECH_PARAMS_FORCE_SHOUTED", 0);
            ownerRan = true;
            ownerTime = Now() + 6000;
        }
    }

    void TickDeliver() {
        bool attached = slide ? ENTITY::IS_ENTITY_ATTACHED_TO_ENTITY(target, truck) != 0 : VEHICLE::IS_VEHICLE_ATTACHED_TO_TOW_TRUCK(truck, target) != 0;
        if (!attached && Now() - hookTime > 1500) {
            stage = 1;
            DelBlip(bDrop);
            bTarget = BlipEnt(target, SPR_STD, COL_RED, "Target");
            RouteSet(bTarget);
            Ui::Notify(slide ? "รถหลุดจากถาด! กลับไปดึงขึ้นใหม่" : "รถหลุดจากตะขอ! กลับไปเกี่ยวใหม่");
            return;
        }
        float d = Dist(Pos(truck), drop.pos);
        objective = "ลากไปส่งที่ " + drop.name;
        info = "ห่าง " + KmStr(d);
        Ui::Zone(drop.pos, 14.0f, Ui::kGreen, "ลานส่งรถ");
        if (d < 28.0f && SpeedOf(truck) < 4.0f) Ui::Prompt(std::string("กด ") + BindText(A_ACTION) + " เพื่อปล่อยรถ");
        if (repo && owner && Ex(owner) && ownerTime != 0 && Now() > ownerTime) {
            TASK::TASK_SMART_FLEE_PED(owner, PlayerPed(), 80.0f, -1, FALSE, FALSE);
            ownerTime = 0;
        }
    }

    void Release() {
        if (slide) { winch.reset(new Winch(truck, target, false)); stage = 7; info.clear(); return; }
        VEHICLE::DETACH_VEHICLE_FROM_TOW_TRUCK(truck, target);
        stage = 3;
        doneTime = Now() + 2500;
        objective = "ปล่อยรถเรียบร้อย";
        info.clear();
    }

    void Cleanup() override {
        if (gate) gate->Cleanup();
        DelBlip(bSpot); DelBlip(bDrop); DelBlip(bTarget);
        Rel(target); Rel(owner); Rel(truck);
    }
};
