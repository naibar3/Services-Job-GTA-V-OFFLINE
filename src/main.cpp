// ServicesJob: freeroam service jobs for GTA V single player (tow/repo, taxi, tour bus, trucking, ambulance, police chase)
// ScriptHookV C++ ASI. Thai text via embedded TH Sarabun PSK. Build with build.ps1.
#include "common.h"

#include "ui.h"
#include "jobcore.h"
#include "slide.h"
#include "towrepo.h"
#include "taxi.h"
#include "tour.h"
#include "truck.h"
#include "ambulance.h"
#include "police.h"
#include "input.h"
#include "pager.h"

// ---------------------------------------------------------------- keyboard
static void OnKey(DWORD key, WORD, BYTE, BOOL, BOOL, BOOL wasDownBefore, BOOL isUpNow) {
    if (isUpNow || wasDownBefore) return;
    Input::PushKey((int)key);
}

// Ctrl+Alt+F7: which known bone names does the nearby vehicle have (for slide-bed models)
// Ctrl+Alt+F8: log the current position/heading in depots.txt format
static void DebugKey(int vk) {
    Ped pl = PlayerPed();
    if (vk == VK_F8) {
        Entity e = PED::IS_PED_IN_ANY_VEHICLE(pl, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(pl, FALSE) : pl;
        Vector3 p = Pos(e);
        std::string s = Fmt("ชื่อจุด|%.1f|%.1f|%.1f|%.0f|1", p.x, p.y, p.z, Hdg(e));
        Log("POS %s", s.c_str());
        Ui::Notify("บันทึกพิกัดลง log.txt: " + s);
    } else if (vk == VK_F7) {
        Vehicle v = PED::IS_PED_IN_ANY_VEHICLE(pl, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(pl, FALSE) : VEHICLE::GET_CLOSEST_VEHICLE(Pos(pl), 8.0f, 0, 70);
        if (!v || !Ex(v)) { Ui::Notify("Ctrl+Alt+F7: ไม่พบรถใกล้ตัว"); return; }
        static const char *names[] = {"scoop", "misc_a", "misc_b", "misc_c", "misc_d", "misc_e", "misc_f", "chassis", "bodyshell", "boot", "bonnet",
                                      "extra_1", "extra_2", "extra_3", "extra_4", "extra_5", "extra_6", "extra_7", "extra_8"};
        std::string found;
        for (auto n : names) if (ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(v, n) != -1) found += std::string(n) + " ";
        Log("BONES model=0x%08X found=[%s]", (unsigned)ENTITY::GET_ENTITY_MODEL(v), found.c_str());
        Ui::Notify("bone ของรถ: " + found);
    }
}

// what each bound action does (keyboard key or wheel button, same thing)
static void Fire(int action) {
    switch (action) {
        case A_PAGER: Pager::Toggle(); break;
        case A_ACCEPT: if (Pager::isOpen) Pager::Accept(); else Pager::Live::Accept(); break;
        case A_DISMISS: if (Pager::isOpen) Pager::DeleteSelected(); else Pager::Live::Dismiss(); break;
        case A_ACTION:
            if (JobManager::active) JobManager::active->OnAction();
            FreeSlide::OnAction();
            break;
    }
}

static void HandleInput() {
    Input::Poll();
    for (const Input::Ev &ev : Input::Take()) {
        bool ctrlAlt = (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000);
        if (!ev.joy && cfg.debugKeys && ctrlAlt && (ev.vk == VK_F7 || ev.vk == VK_F8)) { DebugKey(ev.vk); continue; }
        if (Input::Capturing()) { Input::Capture(ev); continue; }
        bool handled = false;
        for (int a = 0; a < A_COUNT; a++) if (Input::Match(cfg.binds[a], ev)) { Fire(a); handled = true; }
        if (!handled && !ev.joy && Pager::isOpen) Pager::Key(ev.vk);   // arrows / Enter / Del / Esc inside the pager
    }
}
// ---------------------------------------------------------------- script
static DWORD g_sehCode = 0;
static void *g_sehAddr = nullptr;
static int SehFilter(EXCEPTION_POINTERS *ep) {
    g_sehCode = ep->ExceptionRecord->ExceptionCode;
    g_sehAddr = ep->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;
}
// no C++ objects with destructors in here, so __try/__except is allowed
static bool SafeRun(void (*fn)()) {
    __try { fn(); return true; }
    __except (SehFilter(GetExceptionInformation())) { return false; }
}

static void TickBody() {
    try {
        HandleInput();
        RouteTick();
        Depots::Tick();
        Pager::Update();
        FreeSlide::Tick();
        JobManager::Tick();
        Pager::Draw();
        Ui::DrawOverlay();
    } catch (const std::exception &e) {
        Log("C++ exception in main loop: %s", e.what());
    } catch (...) {
        Log("unknown C++ exception in main loop");
    }
}

static void ScriptMain() {
    try {
        CreateDirectoryA(g_data.c_str(), nullptr);
        Log("ServicesJob %s starting", SJ_VERSION);
        LoadConfig();
        StatsLoad();
        Depots::Load();
        LoadTours();
        Ui::Init();
    } catch (...) { Log("init exception"); }

    int sehCount = 0;
    while (true) {
        if (!SafeRun(TickBody)) {
            // hardware exception (access violation, or a fault inside another module's exception handler) caught instead of
            // letting it reach ScriptHookV, which would kill this script. Logged with code + address for diagnosis.
            if (sehCount++ < 20) Log("SEH exception %08X at %p (thrown %d times)", (unsigned)g_sehCode, g_sehAddr, sehCount);
            if (JobManager::active) { try { JobManager::Cancel(); } catch (...) {} }
        }
        Ui::Flush();
        WAIT(0);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            char path[MAX_PATH];
            GetModuleFileNameA(hModule, path, MAX_PATH);
            g_dir = path;
            g_dir = g_dir.substr(0, g_dir.find_last_of("\\/") + 1);
            g_data = g_dir + "ServicesJob\\";
            scriptRegister(hModule, ScriptMain);
            keyboardHandlerRegister(OnKey);
            presentCallbackRegister(Ui::OnPresent);
            break;
        }
        case DLL_PROCESS_DETACH:
            presentCallbackUnregister(Ui::OnPresent);
            keyboardHandlerUnregister(OnKey);
            scriptUnregister(hModule);
            break;
    }
    return TRUE;
}
