// slide.h: flatbed ("slide bed") winch. The bed of the game's flatbed is part of the body (no movable bone, verified from
// flatbed.yft), so the car is pulled up an invisible ramp: it first slides to the ground behind the truck, then climbs the
// slope onto the deck with its nose tilted to the ramp angle. While moving, the car is frozen, collision with the truck is
// off and velocity is zero, so physics cannot throw it into the air.
#pragma once

namespace Slide {

static bool IsSlide(Vehicle v) {
    Hash m = ENTITY::GET_ENTITY_MODEL(v);
    for (auto &n : cfg.slideModels) if (HK(n.c_str()) == m) return true;
    return false;
}

// deck height / centre measured from the real model (truck-local coordinates)
static bool ProfileOf(Vehicle truck, float &deckZ, float &centerY) {
    if (ENTITY::GET_ENTITY_MODEL(truck) == HK("flatbed")) { deckZ = 0.58f; centerY = -3.4f; return true; }
    return false;
}

static void Dims(Hash model, Vector3 &mn, Vector3 &mx) { MISC::GET_MODEL_DIMENSIONS(model, &mn, &mx); }

static Vector3 BedOffset(Vehicle truck, Vehicle target) {
    Vector3 tmn, tmx, vmn, vmx;
    Dims(ENTITY::GET_ENTITY_MODEL(truck), tmn, tmx);
    Dims(ENTITY::GET_ENTITY_MODEL(target), vmn, vmx);
    float targetLen = vmx.y - vmn.y;
    float deckZ, centerY;
    float y, z;
    bool known = ProfileOf(truck, deckZ, centerY);
    if (known) { y = centerY; z = deckZ - vmn.z + 0.04f + cfg.slideZShift; }
    else { y = tmn.y + targetLen * 0.5f + 0.3f; z = cfg.slideZ; }
    return V(cfg.slideXShift, y + cfg.slideYShift, z);
}

static bool IsBehind(Vehicle truck, Vehicle target, float maxDist) {
    Vector3 local = LocalOf(truck, Pos(target));
    return local.y < -1.0f && fabsf(local.x) < 6.0f && Dist(Pos(target), Pos(truck)) < maxDist;
}

}  // namespace Slide

struct Winch {
    Vehicle truck, target;
    bool loading;
    float duration, t = 0.0f;
    Vector3 p0, r0, off;
    float h0, relHeading, sign, tgtHalfLen, tgtBottom, rearY;
    bool done = false;

    Winch(Vehicle tr, Vehicle tg, bool load) : truck(tr), target(tg), loading(load) {
        duration = std::max(2.5f, cfg.slideSeconds);
        Vector3 vmn, vmx, tmn, tmx;
        Slide::Dims(ENTITY::GET_ENTITY_MODEL(target), vmn, vmx);
        Slide::Dims(ENTITY::GET_ENTITY_MODEL(truck), tmn, tmx);
        tgtHalfLen = (vmx.y - vmn.y) * 0.5f;
        tgtBottom = -vmn.z;
        rearY = tmn.y;
        off = Slide::BedOffset(truck, target);

        if (!loading) ENTITY::DETACH_ENTITY(target, TRUE, TRUE);

        p0 = Pos(target);
        r0 = ENTITY::GET_ENTITY_ROTATION(target, 2);
        h0 = Hdg(target);
        float delta = WrapDeg(h0 - Hdg(truck));
        relHeading = fabsf(delta) < 90.0f ? 0.0f : 180.0f;
        sign = relHeading == 0.0f ? 1.0f : -1.0f;

        ENTITY::FREEZE_ENTITY_POSITION(target, TRUE);
        ENTITY::SET_ENTITY_VELOCITY(target, V(0, 0, 0));
    }

    static float Band(float x, float a, float b) { return Smooth((x - a) / (b - a)); }

    // spot on the ground behind the truck, centred on the bed
    Vector3 GroundPoint() {
        Vector3 p = OffW(truck, off.x, rearY - tgtHalfLen - 0.8f, 0.0f);
        float gz = GroundZ(p, Pos(truck).z - 0.8f);
        p.z = gz + tgtBottom + 0.04f;
        return p;
    }

    void Tick() {
        if (done) return;
        if (!Ex(target) || !Ex(truck)) { done = true; return; }

        t += Dt() / duration;
        float tt = std::min(1.0f, t);

        ENTITY::SET_ENTITY_NO_COLLISION_ENTITY(target, truck, TRUE);
        ENTITY::SET_ENTITY_NO_COLLISION_ENTITY(truck, target, TRUE);

        Vector3 deck = OffW(truck, off);
        Vector3 ground = GroundPoint();
        float endHead = Hdg(truck) + relHeading;
        float run = std::max(0.5f, Len2(deck - ground));
        float rampDeg = atan2f(deck.z - ground.z, run) * 57.29578f;

        Vector3 pos;
        float head, pitch, k;
        if (loading) {
            float a = Smooth(std::min(tt / 0.3f, 1.0f));
            k = Smooth(std::max(0.0f, (tt - 0.3f) / 0.7f));
            pos = tt < 0.3f ? LerpV(p0, ground, a) : LerpV(ground, deck, k);
            head = h0 + WrapDeg(endHead - h0) * a;
            pitch = sign * rampDeg * Band(k, 0.0f, 0.15f) * (1.0f - Band(k, 0.85f, 1.0f));
            if (tt < 0.3f) pitch = r0.x * (1.0f - a);
        } else {
            Vector3 away = Norm3(Pos(truck) - ground);
            Vector3 rest = ground - away * 1.0f;   // a little further back from the truck
            rest.z = ground.z;
            float a = Smooth(std::min(tt / 0.7f, 1.0f));
            float b = Smooth(std::max(0.0f, (tt - 0.7f) / 0.3f));
            k = 1.0f - a;
            pos = tt < 0.7f ? LerpV(p0, ground, a) : LerpV(ground, rest, b);
            head = h0;
            pitch = sign * rampDeg * Band(k, 0.0f, 0.15f) * (1.0f - Band(k, 0.85f, 1.0f));
        }
        float roll = loading ? ENTITY::GET_ENTITY_ROTATION(truck, 2).y * k : 0.0f;

        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(target, pos, FALSE, FALSE, FALSE);
        ENTITY::SET_ENTITY_ROTATION(target, pitch, roll, head, 2, TRUE);

        if (t >= 1.0f) Finish();
    }

    void Finish() {
        done = true;
        if (loading) {
            ENTITY::FREEZE_ENTITY_POSITION(target, FALSE);
            ENTITY::ATTACH_ENTITY_TO_ENTITY(target, truck, -1, off, V(0, 0, relHeading), FALSE, FALSE, FALSE, FALSE, 2, TRUE, 0);
        } else {
            ENTITY::SET_ENTITY_VELOCITY(target, V(0, 0, 0));
            VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(target, 5.0f);
            ENTITY::FREEZE_ENTITY_POSITION(target, FALSE);
            ENTITY::SET_ENTITY_VELOCITY(target, V(0, 0, 0));
        }
    }
};


// ---------------------------------------------------------------- free use (no job needed)
// Drive any vehicle listed in [Slide] Models, reverse towards ANY car and press the action key: the car is winched onto the bed.
// Press the key again (truck almost stopped) to lower it back to the ground. Inactive while a tow job runs (the job does it).
namespace FreeSlide {

static Vehicle truck = 0, target = 0;
static int mode = 0;   // 0 idle, 1 loading, 2 loaded, 3 unloading
static std::unique_ptr<Winch> winch;

static bool Allowed() { return !(JobManager::active && JobManager::active->kind == K_TOW); }

static Vehicle DrivenSlideTruck() {
    Ped p = PlayerPed();
    if (!PED::IS_PED_IN_ANY_VEHICLE(p, FALSE)) return 0;
    Vehicle v = PED::GET_VEHICLE_PED_IS_IN(p, FALSE);
    if (v && VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1, FALSE) == p && Slide::IsSlide(v)) return v;
    return 0;
}

// nearest vehicle behind the truck, close enough and roughly centred
static Vehicle FindBehind(Vehicle tk) {
    int arr[256];
    int n = worldGetAllVehicles(arr, 256);
    Vehicle best = 0;
    float bd = 1e9f;
    for (int i = 0; i < n; i++) {
        Vehicle v = arr[i];
        if (v == tk || !Ex(v)) continue;
        Vector3 l = LocalOf(tk, Pos(v));
        if (l.y > -1.0f || fabsf(l.x) > 6.0f) continue;
        float d = Dist(Pos(v), Pos(tk));
        if (d > 14.0f || d >= bd) continue;
        if (ENTITY::IS_ENTITY_ATTACHED(v)) continue;
        best = v;
        bd = d;
    }
    return best;
}

static void Reset() { mode = 0; target = 0; winch.reset(); }

static void Tick() {
    if (mode == 1 || mode == 3) {
        if (!Ex(truck) || !Ex(target)) { Reset(); return; }
        winch->Tick();
        if (winch->done) {
            if (mode == 1) mode = 2; else { target = 0; mode = 0; }
            winch.reset();
        }
        return;
    }
    if (mode == 2 && (!Ex(truck) || !Ex(target) || !ENTITY::IS_ENTITY_ATTACHED_TO_ENTITY(target, truck))) Reset();

    if (!Allowed()) return;
    Vehicle cur = DrivenSlideTruck();
    if (!cur) return;
    std::string key = BindText(A_ACTION);
    if (mode == 0) {
        Vehicle t = FindBehind(cur);
        if (t && SpeedOf(cur) < 3.0f) Ui::Prompt("กด " + key + " เพื่อดึงรถคันหลังขึ้นถาด");
    } else if (mode == 2 && cur == truck && SpeedOf(cur) < 4.0f) {
        Ui::Prompt("กด " + key + " เพื่อปล่อยรถลงพื้น");
    }
}

static void OnAction() {
    if (!Allowed() || mode == 1 || mode == 3) return;
    Vehicle cur = DrivenSlideTruck();
    if (!cur) return;
    if (mode == 0) {
        Vehicle t = FindBehind(cur);
        if (!t) { Ui::Notify("ถอยท้ายรถสไลด์ให้อยู่ด้านหลังรถที่ต้องการก่อน"); return; }
        if (SpeedOf(cur) >= 3.0f) { Ui::Notify("หยุดรถก่อนดึงรถขึ้นถาด"); return; }
        truck = cur;
        target = t;
        winch.reset(new Winch(truck, target, true));
        mode = 1;
    } else if (mode == 2 && cur == truck) {
        if (SpeedOf(cur) >= 4.0f) { Ui::Notify("หยุดรถก่อนปล่อยรถลงพื้น"); return; }
        winch.reset(new Winch(truck, target, false));
        mode = 3;
    }
}

}  // namespace FreeSlide
