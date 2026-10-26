# ServicesJob

Freeroam service jobs for **GTA V single player**: tow/repo, taxi, tour bus, trucking, ambulance, police chase.
Thai pager UI (mouse + keyboard + steering wheel buttons), live job cards, depots, flatbed winch.
C++17 ASI on the Script Hook V SDK. Offline use only.

| | |
|---|---|
| Users | install package `ServicesJob-v1.1-install.zip` → `README-TH.txt`, `USER-MANUAL-TH.md` |
| Developers | this source package → `docs/DEVELOPER-GUIDE.md` |

## Quick start (developers)
1. Get the Script Hook V SDK (not included, see `sdk/README.md`).
2. Adjust `-Game` / `-VcVars` in `build.ps1` if your paths differ.
3. `powershell -ExecutionPolicy Bypass -File build.ps1` → builds and copies `ServicesJob.asi` into the game folder.
4. `powershell -ExecutionPolicy Bypass -File make-release.ps1` → builds both release packages into `dist/`.

## Layout
```
src/        C++ sources (single translation unit: main.cpp includes the headers)
docs/       user manual (TH), developer guide, installer script
sdk/        Script Hook V SDK goes here (not redistributed)
build.ps1   build + deploy        make-release.ps1   packaging
CHANGELOG.md
```

## Notes
- The embedded Thai font is TH Sarabun PSK (SIPA, free to use and redistribute with its notice: `src/FONT_NOTICE.txt`).
- Script Hook V and its SDK must be downloaded by the user from <http://www.dev-c.com/gtav/scripthookv/>; they are not redistributed here.
- ServicesJob by NakimiTV is **free to use**, no charge, provided as-is without warranty.
- The Thai font **is bundled** (embedded in `ServicesJob.asi` and in `src/font_data.h`): TH Sarabun PSK is a Thai national standard font released by SIPA for free use and redistribution; its notice ships with every package.
- Script Hook V is **not bundled**: players download it themselves.
- Not affiliated with Rockstar Games. Do not use in GTA Online.
