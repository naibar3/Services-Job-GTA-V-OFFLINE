// input.h: keyboard + wheel/joystick buttons. Joystick buttons are read with the WinMM joystick API (non-exclusive, works next to
// WheelPlugin / Moza Pit House); every action can be bound to a keyboard key AND a wheel button, either one triggers it.
#pragma once
#include <mmsystem.h>
#include <mutex>
#pragma comment(lib, "winmm.lib")

namespace Input {

struct Ev { bool joy; int vk; unsigned mid, pid; int btn; };

static std::vector<Ev> g_events;
static std::mutex g_mx;

static void PushKey(int vk) {
    std::lock_guard<std::mutex> l(g_mx);
    if (g_events.size() < 64) g_events.push_back({false, vk, 0, 0, -1});
}

// ---- joystick polling
struct Dev { int id; unsigned mid, pid; DWORD prev; };
static std::vector<Dev> g_devs;
static int g_lastScan = -100000;

static void Scan() {
    g_devs.clear();
    UINT n = joyGetNumDevs();
    for (UINT id = 0; id < n && id < 16; id++) {
        JOYCAPSA caps;
        if (joyGetDevCapsA(id, &caps, sizeof caps) != JOYERR_NOERROR) continue;
        JOYINFOEX ji;
        ji.dwSize = sizeof ji;
        ji.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(id, &ji) != JOYERR_NOERROR) continue;
        g_devs.push_back({(int)id, (unsigned)caps.wMid, (unsigned)caps.wPid, ji.dwButtons});
    }
    Log("joystick scan: %d device(s)", (int)g_devs.size());
}

static void Poll() {
    int now = Now();
    if (now - g_lastScan > (g_devs.empty() ? 5000 : 20000)) { g_lastScan = now; Scan(); }
    for (auto &d : g_devs) {
        JOYINFOEX ji;
        ji.dwSize = sizeof ji;
        ji.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(d.id, &ji) != JOYERR_NOERROR) { g_lastScan = -100000; continue; }
        DWORD pressed = ji.dwButtons & ~d.prev;
        d.prev = ji.dwButtons;
        if (!pressed) continue;
        std::lock_guard<std::mutex> l(g_mx);
        for (int b = 0; b < 32; b++)
            if ((pressed >> b) & 1u) { if (g_events.size() < 64) g_events.push_back({true, 0, d.mid, d.pid, b}); }
    }
}

static std::vector<Ev> Take() {
    std::vector<Ev> out;
    std::lock_guard<std::mutex> l(g_mx);
    out.swap(g_events);
    return out;
}

static bool Match(const Bind &b, const Ev &e) {
    if (!e.joy) return b.key != 0 && b.key == e.vk;
    return b.btn >= 0 && b.btn == e.btn && b.mid == e.mid && b.pid == e.pid;
}

// ---- rebinding: slot 0 = keyboard key, slot 1 = wheel button
static int capAction = -1, capSlot = 0;
static bool Capturing() { return capAction >= 0; }
static void StartCapture(int action, int slot) {
    capAction = action;
    capSlot = slot;
    Ui::Notify(slot == 0 ? "กดปุ่มบนคีย์บอร์ดที่ต้องการ (Esc = ยกเลิก)" : "กดปุ่มบนพวงมาลัยที่ต้องการ (Esc = ยกเลิก)", 8.0f);
}
static void CancelCapture() { capAction = -1; }

// true when the event was consumed by rebinding
static bool Capture(const Ev &e) {
    if (capAction < 0) return false;
    if (!e.joy && e.vk == VK_ESCAPE) { capAction = -1; Ui::Notify("ยกเลิกการตั้งปุ่ม"); return true; }
    Bind &b = cfg.binds[capAction];
    if (capSlot == 0 && !e.joy) b.key = e.vk;
    else if (capSlot == 1 && e.joy) { b.mid = e.mid; b.pid = e.pid; b.btn = e.btn; }
    else return true;   // wrong device for this slot: keep waiting
    int a = capAction;
    capAction = -1;
    SaveConfig();
    Ui::Beep("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
    Ui::Notify("ตั้งปุ่มแล้ว: " + BindText(a));
    return true;
}

}  // namespace Input
