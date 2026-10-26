// ambulance.h: the player only drives; two medics (NPCs) load the patient on a gurney and unload at the hospital
#pragma once

struct AmbulanceJob : Job {
    Vector3 spot;
    float spotHeading;
    bool crash;
    Place hospital;
    int basePay;

    std::unique_ptr<VehicleGate> gate;
    Vehicle amb = 0, wreck = 0;
    Ped patient = 0, medicA = 0, medicB = 0;
    Object gurney = 0;
    Blip bScene = 0, bHosp = 0;
    int stage = 0;   // 0 get ambulance, 1 go to scene, 2 loading, 3 to hospital, 4 unloading
    int sub = 0, subT = 0, startTime = 0;
    float condition = 100.0f, prevSpeed = 0;
    bool hasGurney = false;
    Vector3 unloadTo = V(0, 0, 0);

    AmbulanceJob(Vector3 s, float h, bool c, const Place &hosp, int pay) : spot(s), spotHeading(h), crash(c), hospital(hosp), basePay(pay) { hospital.pos = SnapNear(hospital.pos); }
    std::string Name() override { return "รถพยาบาลฉุกเฉิน"; }

    static const std::vector<Place> &Hospitals() {
        static std::vector<Place> h = {Place("รพ. Pillbox Hill", 299.5f, -584.5f, 43.2f), Place("รพ. Central Los Santos", 340.0f, -1396.0f, 32.5f),
                                       Place("รพ. Mount Zonah", -449.5f, -340.5f, 34.5f), Place("รพ. Sandy Shores", 1839.5f, 3672.5f, 34.3f),
                                       Place("รพ. Paleto Bay", -247.5f, 6331.0f, 32.4f)};
        return h;
    }

    static std::unique_ptr<Offer> MakeOffer(Vector3 pp) {
        Vector3 pos;
        float hd;
        if (!FindRoad(pp, 400.0f, 2200.0f, pos, hd)) return nullptr;
        Place best = Hospitals()[0];
        for (auto &h : Hospitals()) if (Dist2(h.pos, pos) < Dist2(best.pos, pos)) best = h;
        bool crash = RandInt(0, 100) < 60;
        int pay = 700 + (int)(Dist(pos, best.pos) * 0.25f);
        auto o = std::make_unique<Offer>();
        o->pos = pos;
        o->pay = pay;
        std::string st = StreetAt(pos);
        o->title = crash ? "[รถพยาบาล] อุบัติเหตุรถชน แถว " + st : "[รถพยาบาล] คนล้มหมดสติ แถว " + st;
        o->line1 = "ส่งต่อที่ " + best.name;
        o->create = [=]() { return std::unique_ptr<Job>(new AmbulanceJob(pos, hd, crash, best, pay)); };
        return o;
    }

    void Start() override {
        gate.reset(new VehicleGate(K_AMB, "รถพยาบาล", {"ambulance"}, [](Vehicle v) { return VehModel(v, "ambulance"); }));
        objective = gate->Objective();
    }

    std::string Cond() { return "อาการผู้ป่วย " + std::to_string((int)condition) + "%"; }

    void Tick() override {
        if (stage == 0) {
            if (!gate->Update()) { objective = gate->Objective(); return; }
            amb = gate->veh;
            gate->Cleanup();
            SpawnMedics();
            bScene = BlipPos(spot, SPR_STD, COL_RED, "Emergency", true);
            VEHICLE::SET_VEHICLE_SIREN(amb, TRUE);
            startTime = Now();
            stage = 1;
            Ui::Notify("ศูนย์: ผู้ช่วยพยาบาลประจำรถแล้ว รีบไปที่เกิดเหตุ");
        }
        if (!Ex(amb) || IsDead(amb)) { Fail("รถพยาบาลเสียหาย"); return; }
        if (patient && Ex(patient) && IsDead(patient)) { Fail("ผู้ป่วยเสียชีวิต"); return; }
        if (stage >= 1) {
            condition -= Dt() * 0.14f;
            float sp = SpeedOf(amb);
            if (prevSpeed - sp > 7.0f) { condition -= 8.0f; Ui::Notify("ชนแรง! อาการผู้ป่วยแย่ลง"); }
            prevSpeed = sp;
            if (condition <= 0.0f) { Fail("ผู้ป่วยอาการวิกฤตเกินช่วย"); return; }
        }
        switch (stage) {
            case 1: TickGo(); break;
            case 2: TickLoad(); break;
            case 3: TickHospital(); break;
            case 4: TickUnload(); break;
        }
    }

    void SpawnMedics() {
        medicA = SpawnPedModel("s_m_m_paramedic_01", Pos(amb) + V(0, 0, 2.0f), 0.0f);
        medicB = SpawnPedModel("s_m_m_paramedic_01", Pos(amb) + V(0, 0, 2.0f), 0.0f);
        if (medicA) PED::SET_PED_INTO_VEHICLE(medicA, amb, 0);
        if (medicB) PED::SET_PED_INTO_VEHICLE(medicB, amb, 1);
    }

    void TickGo() {
        float d = Dist(Pos(amb), spot);
        objective = "ไปที่เกิดเหตุ ห่าง " + KmStr(d);
        info = "ผู้ช่วยนั่งรอในรถ";
        Ui::Zone(patient ? Pos(patient) : spot, 11.0f, Ui::kRed, "จุดเกิดเหตุ - จอดใกล้ผู้ป่วย");
        if (!patient && d < 230.0f) SpawnScene();
        if (!patient) return;
        Vector3 pp = Pos(patient);
        if (Dist(Pos(amb), pp) < 15.0f && SpeedOf(amb) < 2.5f) {
            stage = 2; sub = 0; subT = Now();
            DelBlip(bScene);
            objective = "จอดนิ่งๆ ผู้ช่วยกำลังเข้าไปช่วยผู้ป่วย";
            info.clear();
            if (medicA) TASK::TASK_LEAVE_VEHICLE(medicA, amb, 0);
            if (medicB) TASK::TASK_LEAVE_VEHICLE(medicB, amb, 0);
        }
    }

    void SpawnScene() {
        Vector3 pp = spot + V(3, 0, 0), safe;
        if (PATHFIND::GET_SAFE_COORD_FOR_PED(pp, TRUE, &safe, 0)) pp = safe;
        patient = SpawnCiv(pp, spotHeading);
        if (!patient) { Fail("สร้างผู้ป่วยไม่ได้"); return; }
        ENTITY::SET_ENTITY_INVINCIBLE(patient, TRUE);
        PED::SET_PED_CAN_RAGDOLL(patient, FALSE);
        PlayAnim(patient, "dead", "dead_a", true);
        if (crash) {
            static const char *w[] = {"asea", "sentinel", "blista", "emperor", "stanier", "primo"};
            wreck = SpawnVeh(w[RandInt(0, 6)], spot, spotHeading);
            if (wreck) {
                VEHICLE::SET_VEHICLE_ENGINE_HEALTH(wreck, 0.0f);
                VEHICLE::SET_VEHICLE_BODY_HEALTH(wreck, 250.0f);
                VEHICLE::SET_VEHICLE_ENGINE_ON(wreck, FALSE, TRUE, TRUE);
                VEHICLE::SET_VEHICLE_DOOR_OPEN(wreck, 0, FALSE, FALSE);
                VEHICLE::SET_VEHICLE_DOOR_OPEN(wreck, 4, FALSE, FALSE);
            }
        }
        DelBlip(bScene);
        bScene = BlipEnt(patient, SPR_STD, COL_RED, "Patient");
        RouteSet(bScene);
    }

    static std::vector<const char *> GurneyModels() { return {"prop_gurney_01", "v_med_bed2", "v_med_bed1", "v_med_emptybed", "prop_ld_hospital_bed"}; }

    Vector3 GroundAt(Vector3 p) { return V(p.x, p.y, GroundZ(p, p.z)); }

    bool MoveGurney(Vector3 to, float speed) {
        if (!gurney || !Ex(gurney)) return true;
        Vector3 cur = Pos(gurney);
        Vector3 dir = V(to.x - cur.x, to.y - cur.y, 0);
        float len = Len2(dir);
        if (len < 1.0f) return true;
        float step = std::min(len, speed * Dt());
        Vector3 np = cur + dir * (step / len);
        np.z = GroundZ(np, np.z);
        ENTITY::SET_ENTITY_HEADING(gurney, HeadingTo(cur, to));
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(gurney, np, FALSE, FALSE, FALSE);
        return false;
    }
    void Follow(Ped m, float ox, float oy) {
        if (!m || !Ex(m) || !gurney) return;
        TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(m, gurney, V(ox, oy, 0.0f), 1.2f, -1, 0.4f, TRUE);
    }
    void WalkTo(Ped m, Vector3 pos) {
        if (!m || !Ex(m)) return;
        TASK::TASK_GO_STRAIGHT_TO_COORD(m, pos, 1.2f, -1, 0.0f, 0.5f);
    }

    void TickLoad() {
        info = Cond();
        int now = Now();
        switch (sub) {
            case 0:
                if (now - subT > 2500) {
                    gurney = SpawnProp(GurneyModels(), GroundAt(OffW(amb, 0.0f, -4.2f, 0.0f)), Hdg(amb));
                    hasGurney = gurney != 0;
                    if (hasGurney) {
                        ENTITY::FREEZE_ENTITY_POSITION(gurney, TRUE);
                        Follow(medicA, 0.9f, -0.9f);
                        Follow(medicB, -0.9f, -0.9f);
                    } else { WalkTo(medicA, Pos(patient)); WalkTo(medicB, Pos(patient)); }
                    sub = 1; subT = now;
                    objective = "ผู้ช่วยเข็นเปลไปหาผู้ป่วย";
                }
                break;
            case 1:
                if (hasGurney) { if (MoveGurney(Pos(patient) + V(1.0f, 0, 0), 1.4f)) AttachPatientToGurney(); }
                else if (now - subT > 6000) AttachPatientToAmbulance();
                break;
            case 2:
                if (now - subT > 1800) {
                    sub = 3;
                    objective = "ผู้ช่วยเข็นผู้ป่วยกลับขึ้นรถ";
                    Follow(medicA, 0.9f, -0.9f);
                    Follow(medicB, -0.9f, -0.9f);
                }
                break;
            case 3:
                if (MoveGurney(OffW(amb, 0.0f, -3.6f, 0.0f), 1.4f)) AttachPatientToAmbulance();
                break;
            case 4: {
                bool a = !medicA || PED::IS_PED_IN_VEHICLE(medicA, amb, FALSE), b = !medicB || PED::IS_PED_IN_VEHICLE(medicB, amb, FALSE);
                if (a && b) BeginToHospital();
                else if (now - subT > 14000) {
                    if (medicA) PED::SET_PED_INTO_VEHICLE(medicA, amb, 0);
                    if (medicB) PED::SET_PED_INTO_VEHICLE(medicB, amb, 1);
                    BeginToHospital();
                }
                break;
            }
        }
    }

    void AttachPatientToGurney() {
        ENTITY::ATTACH_ENTITY_TO_ENTITY(patient, gurney, 0, V(0, 0, 0.9f), V(0, 0, 0), FALSE, FALSE, FALSE, FALSE, 2, TRUE, 0);
        PlayAnim(patient, "dead", "dead_a", true);
        sub = 2; subT = Now();
        objective = "ผู้ช่วยยกผู้ป่วยขึ้นเปล";
    }

    void AttachPatientToAmbulance() {
        ENTITY::DETACH_ENTITY(patient, TRUE, TRUE);
        if (gurney) Del(gurney);
        ENTITY::ATTACH_ENTITY_TO_ENTITY(patient, amb, 0, V(0, -1.7f, 0.45f), V(0, 0, 0), FALSE, FALSE, FALSE, FALSE, 2, TRUE, 0);
        PlayAnim(patient, "dead", "dead_a", true);
        if (medicA) TASK::TASK_ENTER_VEHICLE(medicA, amb, 12000, 0, 1.2f, 1, nullptr);
        if (medicB) TASK::TASK_ENTER_VEHICLE(medicB, amb, 12000, 1, 1.2f, 1, nullptr);
        sub = 4; subT = Now();
        objective = "นำผู้ป่วยขึ้นรถแล้ว รอผู้ช่วยขึ้นรถ";
    }

    void BeginToHospital() {
        stage = 3;
        VEHICLE::SET_VEHICLE_SIREN(amb, TRUE);
        bHosp = BlipPos(hospital.pos, SPR_HOSP, COL_GREEN, "Hospital", true);
        Ui::Notify("ผู้ป่วยอยู่บนรถแล้ว รีบไป " + hospital.name);
    }

    void TickHospital() {
        float d = Dist(Pos(amb), hospital.pos);
        objective = "ส่งผู้ป่วยที่ " + hospital.name;
        info = Cond() + "\nห่าง " + KmStr(d);
        Ui::Zone(hospital.pos, 9.0f, Ui::kGreen, "จุดส่งผู้ป่วย");
        if (d < 20.0f && SpeedOf(amb) < 2.5f) {
            stage = 4; sub = 0; subT = Now();
            DelBlip(bHosp);
            objective = "จอดนิ่งๆ ผู้ช่วยกำลังนำผู้ป่วยลง";
            VEHICLE::SET_VEHICLE_SIREN(amb, FALSE);
            if (medicA) TASK::TASK_LEAVE_VEHICLE(medicA, amb, 0);
            if (medicB) TASK::TASK_LEAVE_VEHICLE(medicB, amb, 0);
        }
    }

    void TickUnload() {
        info = Cond();
        int now = Now();
        switch (sub) {
            case 0:
                if (now - subT > 2500) {
                    gurney = SpawnProp(GurneyModels(), GroundAt(OffW(amb, 0.0f, -4.0f, 0.0f)), Hdg(amb));
                    hasGurney = gurney != 0;
                    ENTITY::DETACH_ENTITY(patient, TRUE, TRUE);
                    if (hasGurney) {
                        ENTITY::FREEZE_ENTITY_POSITION(gurney, TRUE);
                        ENTITY::ATTACH_ENTITY_TO_ENTITY(patient, gurney, 0, V(0, 0, 0.9f), V(0, 0, 0), FALSE, FALSE, FALSE, FALSE, 2, TRUE, 0);
                        Follow(medicA, 0.9f, -0.9f);
                        Follow(medicB, -0.9f, -0.9f);
                    } else {
                        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(patient, OffW(amb, 2.5f, -4.0f, 0.0f), FALSE, FALSE, FALSE);
                    }
                    Vector3 rightV = OffW(amb, 1.0f, 0.0f, 0.0f) - Pos(amb);
                    unloadTo = hospital.pos + rightV * 14.0f;
                    sub = 1; subT = now;
                    objective = "ผู้ช่วยเข็นผู้ป่วยเข้าโรงพยาบาล";
                }
                break;
            case 1:
                if (hasGurney ? MoveGurney(unloadTo, 1.5f) : now - subT > 3000) { sub = 2; subT = now; }
                break;
            case 2:
                if (now - subT > 1200) {
                    float speedBonus = std::max(0.0f, 1.0f - (now - startTime) / 600000.0f);
                    int pay = (int)(basePay * (0.6f + 0.4f * condition / 100.0f) + 400.0f * speedBonus);
                    Complete(pay, "ส่งผู้ป่วยถึงมือแพทย์ อาการ " + std::to_string((int)condition) + "%");
                }
                break;
        }
    }

    void Cleanup() override {
        if (gate) gate->Cleanup();
        DelBlip(bScene); DelBlip(bHosp);
        if (Ex(amb)) VEHICLE::SET_VEHICLE_SIREN(amb, FALSE);
        Del(gurney); Del(patient); Del(medicA); Del(medicB);
        Rel(wreck); Rel(amb);
    }
};
