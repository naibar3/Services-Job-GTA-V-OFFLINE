# Changelog

## 1.0

First release (C++ ScriptHookV ASI).

- 6 jobs: tow/repo (one event type), taxi (any car), tour bus (numbered stops, passengers get off for a cooldown),
  trucking (4 cargo types), ambulance (NPC medics + gurney), police chase (ram 10 times, arrest, deliver to a random station)
- Thai pager with mouse + keyboard, 4 tabs (messages, job types, settings, status), live job cards on the right edge
- Key binding for keyboard and steering-wheel buttons (WinMM), all settings saved to `ServicesJob.ini`
- Depots: one vehicle per job type parked on the nearest main road, no forced trip when you already drive a suitable vehicle
- Flatbed winch (invisible ramp) for the job and for free use without a job
- Live job card is compact (80% size) and sits in the top right corner; tour bus: 15 s stops, the bus cannot leave until all passengers are aboard
- Neon glass UI, Thai text rendered at pixel size with the embedded TH Sarabun PSK font, far-visible light pillars,
  re-asserted GPS route, SEH safety net around the main loop

### History before 1.0
Originally written in C# on ScriptHookVDotNet 3.6.0; SHVDN broke on game build 1.0.3889 (`NativeMemory` initialisation),
so the whole mod was ported to C++ on the Script Hook V SDK. The old C# sources are not part of this release.
