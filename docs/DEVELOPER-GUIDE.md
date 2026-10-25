# ServicesJob — คู่มือนักพัฒนา

มอด GTA V โหมดเนื้อเรื่อง เขียนด้วย **C++17** เป็นไฟล์ `.asi` บน **Script Hook V SDK** (ไม่ใช้ ScriptHookVDotNet)
เอกสารนี้อธิบายโครงสร้าง วิธี build วิธีเพิ่มงานใหม่ และกับดักที่เจอระหว่างพัฒนา

> ภาษา: โค้ดและคอมเมนต์เป็นอังกฤษ ข้อความที่แสดงในเกมเป็นไทย (UTF-8) คอมไพล์ด้วย `/utf-8`

---

## 1. สิ่งที่ต้องมี

| รายการ | หมายเหตุ |
|---|---|
| Visual Studio Build Tools (C++ x64) | `cl.exe` + `vcvars64.bat` (ตัวอย่างพาธอยู่ใน `build.ps1`) |
| Script Hook V SDK | ดาวน์โหลดจาก <http://www.dev-c.com/gtav/scripthookv/> แล้ววางที่ `sdk\inc\*.h` และ `sdk\lib\ScriptHookV.lib` **ห้ามแจกจ่ายต่อ** (ดู `sdk\README.md`) |
| GTA V + Script Hook V runtime | สำหรับทดสอบ |
| ฟอนต์ที่ฝัง | `src\font_data.h` = TH Sarabun PSK (ปกติ + ตัวหนา) เป็นอาร์เรย์ไบต์ (`FONT_REG`, `FONT_BOLD`) สร้างใหม่ได้ถ้าจะเปลี่ยนฟอนต์ (ดูหัวข้อ 9) |

## 2. Build และ deploy

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1              # build + คัดลอก ServicesJob.asi เข้าเกม
powershell -ExecutionPolicy Bypass -File build.ps1 -NoDeploy    # build อย่างเดียว -> bin\ServicesJob.asi
powershell -ExecutionPolicy Bypass -File build.ps1 -Remove      # ลบ .asi ออกจากเกม
powershell -ExecutionPolicy Bypass -File make-release.ps1       # build + สร้างแพ็กแจกจ่าย (dist\*.zip)
```
แก้พาธเกมที่พารามิเตอร์ `-Game` และพาธ `vcvars64.bat` ที่ `-VcVars` (ค่าเริ่มต้นอยู่บนหัว `build.ps1`)

คำสั่งคอมไพล์จริง: `cl /LD /O2 /MT /EHsc /std:c++17 /utf-8 /I sdk /I src src\main.cpp ... /link ScriptHookV.lib gdiplus.lib user32.lib ole32.lib` (`d3d11 d3dcompiler dxgi winmm` เรียกผ่าน `#pragma comment(lib)`)

- `/MT` = ลิงก์ runtime แบบ static ผู้ใช้ไม่ต้องติดตั้ง VC++ Redistributable
- เกมเปิดอยู่จะเขียนทับ `.asi` ไม่ได้ (ไฟล์ถูกล็อก) ให้เปลี่ยนชื่อไฟล์เก่าก่อน (Windows ยอมให้เปลี่ยนชื่อไฟล์ที่ถูกโหลดอยู่) แล้วค่อยคัดลอกตัวใหม่ รุ่นใหม่โหลดตอนเปิดเกมครั้งถัดไป

## 3. โครงสร้างไฟล์

```
src\
  main.cpp      จุดเข้า (DllMain, ScriptMain), ลูปหลัก, ตัวจัดการ input -> action, ปุ่มดีบัก, SEH safety net
  common.h      คณิตศาสตร์ Vector3, log, ini/config, ปุ่ม/Bind, helper ของเกม (spawn, blip, road, route), สถิติ
  ui.h          Thai text + overlay Direct3D11 + ชุด UI นีออน (Panel/Glass/Bar/Badge/Icon/Cursor), Zone marker, toast
  input.h       คิวเหตุการณ์ปุ่ม (คีย์บอร์ด + พวงมาลัยผ่าน WinMM) และโหมดตั้งปุ่ม (capture)
  jobcore.h     Job, Offer, Depots (จุดรับงาน), VehicleGate, JobManager + HUD
  slide.h       Winch (วินช์ถาดสไลด์), FreeSlide (โหมดเล่นอิสระ)
  towrepo.h taxi.h tour.h truck.h ambulance.h police.h   งานแต่ละประเภท
  pager.h       Offers::Make, Pager (หน้าต่าง 4 แท็บ), Live card
  font_data.h   ฟอนต์ที่ฝัง (สร้างอัตโนมัติ ห้ามแก้มือ)
sdk\            Script Hook V SDK (ไม่แจก)
docs\           คู่มือ
build.ps1  make-release.ps1
```

**Single translation unit:** `main.cpp` include header ตามลำดับ `common → ui → jobcore → slide → งาน → input → pager` ฟังก์ชันและตัวแปรเป็น `static` ลำดับ include สำคัญ (ตัวที่ใช้ทีหลังต้องอยู่หลังตัวที่ถูกใช้)

## 4. สถาปัตยกรรมและเธรด

```
ScriptHookV
 ├─ script thread  : ScriptMain()  — ลูป WAIT(0) เรียก natives ทั้งหมดที่นี่
 │     SafeRun(TickBody): HandleInput → RouteTick → Depots::Tick → Pager::Update
 │                        → FreeSlide::Tick → JobManager::Tick → Pager::Draw → Ui::DrawOverlay ; Ui::Flush
 ├─ keyboard hook  : OnKey() — ส่งเข้า Input::PushKey (mutex) เท่านั้น
 └─ present callback (render thread): Ui::OnPresent() — วาด overlay ด้วย D3D11 ห้ามเรียก natives
```
- **ห้ามเรียก native นอก script thread** ข้อมูลข้ามเธรดมีแค่ `g_events` (คีย์), `g_ready` (คำสั่งวาด) และขนาด back buffer (`atomic`) ทั้งหมดป้องกันด้วย mutex/atomic
- `SafeRun` ครอบ `TickBody` ด้วย `__try/__except` เพื่อจับ exception ระดับฮาร์ดแวร์ บันทึกรหัส/แอดเดรสลง `log.txt` แทนที่จะให้ Script Hook V ฆ่าสคริปต์ (ฟังก์ชันนี้ห้ามมีออบเจ็กต์ที่ต้อง destruct)
- exception แบบ C++ ถูกจับใน `TickBody` และ `JobManager::Tick` (ยกเลิกงานปัจจุบัน + log)

## 5. ระบบ UI

### 5.1 ข้อความไทย
เกมไม่มีกลิฟไทย จึง: GDI+ (`PrivateFontCollection` จากหน่วยความจำ) เรนเดอร์ข้อความที่ **ขนาดพิกเซลจริงบนหน้าจอ** (`AntiAliasGridFit`) เป็นบิตแมป → สร้าง texture ใน render thread → วาด 1 texel ต่อ 1 พิกเซลด้วยแซมเพลอร์ POINT
- พิกัด UI ทั้งหมดอยู่บนผ้าใบ **1280×720** `Ui::Text(s, x, y, h, color, align, bold)` โดย `y` = ขอบบนของกรอบบรรทัด, `h` = ความสูงตัวอักษรโดยประมาณ (14–24) บรรทัดจริงสูง `h * 1.5`
- แคช texture ตามคีย์ `(ตัวหนา, ความสูงพิกเซล, ข้อความ)` เรนเดอร์ใหม่ได้สูงสุด 90 ชิ้น/เฟรม ล้างของเก่า (ไม่ได้ใช้ >900 เฟรม) เมื่อแคช >700 และหน่วงปล่อย SRV 5 วินาทีกันเรนเดอร์เธรดใช้อยู่ ลดจำนวนข้อความไม่ซ้ำ (เช่น `KmStr` ปัดระยะเป็นทีละ 10 ม.)
- `Ui::TextW(s, h, bold)` คืนความกว้างบนผ้าใบ 1280 สำหรับจัดเลย์เอาต์

### 5.2 รูปทรง (ชุดนีออน)
ทุกอย่างวาดผ่าน overlay เดียวกัน ลำดับวาด = ลำดับที่เรียกฟังก์ชัน (เรียกพื้นหลังก่อนข้อความ)
- `Ui::Rect` สี่เหลี่ยมทึบ (quad ยืดจาก texture 1×1)
- `Ui::Panel` แผงกระจกกรอบไล่สี+glow / `Ui::Glass` กล่องมนมี glow 0–2 / `Ui::Button` / `Ui::Toggle` / `Ui::Badge` (ปุ่มคีย์) / `Ui::Bar` (แถบเวลา ส่วนเติมวาดเป็นแผ่นบางทุกเฟรมให้ลื่น) / `Ui::Icon` (หมุด, เตือน, เพจเจอร์) / `Ui::Cursor` (เคอร์เซอร์วาดเองเพราะ overlay ทับของเกม)
- รูปทรงสร้างด้วย GDI+ (`RenderShape`) ตามคีย์ `ShapeSpec` (ชนิด ขนาดพิกเซล สี รัศมี glow) แคชรวมกับข้อความ มี `pad` ขอบ glow ที่ยื่นออกนอกกรอบ

### 5.3 Marker โซน
`Ui::Zone(pos, radius, color, label, beam)` วงแหวนพื้น + กระบอกโปร่ง + เสาแสง (DRAW_MARKER) ถ้าไกลเกิน 90 ม. เปลี่ยนเป็นเสาแสงโพลิกอน (DRAW_POLY ไขว้ 2 ระนาบ ทั้งสองหน้า) เพราะเกมหยุดวาด marker ไกล + ป้ายชื่อ/ระยะบนจอ เปลี่ยนเป็นเขียวเมื่อผู้เล่นอยู่ในวง

## 6. ระบบงาน

### 6.1 โครงหลัก (`jobcore.h`)
- `enum JobKind { K_TOW, K_TAXI, K_TOUR, K_TRUCK, K_AMB, K_POLICE }`
- `struct Offer { kind, title, line1, pay, pos, born, create() }` — ข้อมูลที่ขึ้นการ์ด/เพจเจอร์ `create` คืน `unique_ptr<Job>`
- `struct Job` — `Name()`, `Start()`, `Tick()` (เรียกทุกเฟรม), `OnAction()` (ปุ่มกระทำ), `Cleanup()`; มี `objective`/`info` (HUD), `Complete(pay,msg)` / `Fail(msg)`
- `JobManager::active` มีงานเดียวพร้อมกัน `Begin/Cancel/Tick` + วาด HUD มุมซ้ายบน; ผู้เล่นตายหรือเกิด exception = ยกเลิกงาน
- งานใหม่มาจาก `Pager::Update()` ทุก `OfferMin..OfferMax` วินาที สุ่มจากประเภทที่เปิดอยู่ (`cfg.duty`) แล้วเรียก `Offers::Make(kind, playerPos)` → `XxxJob::MakeOffer`

### 6.2 รถของงาน: `VehicleGate`
`VehicleGate(kind, label, models, accept, onSpawn)` — `Update()` คืน true เมื่อได้รถ
1. ผู้เล่นขับรถที่ `accept(v)` อยู่ (ที่นั่งคนขับ) → ใช้เลย (`Depots::Claim(v)`)
2. ถ้ามีจุดรับงานของงานนี้ → ชี้ไปจุดที่ใกล้สุด (blip+วง) ยกเว้นแท็กซี่ที่เป็นแค่ blip ตัวช่วย
3. ถ้าไม่มีจุดรับงานเลย → สร้างรถข้างตัวผู้เล่น (`onSpawn` ใช้ต่อพ่วงให้รถบรรทุก)

### 6.3 จุดรับงาน: `Depots`
- โหลดจาก `depots.txt` ตอนเริ่ม (สร้างไฟล์เริ่มต้นถ้าไม่มี) ทุกจุดถูก **ดึงเข้าถนนหลักใกล้สุด** (`GET_CLOSEST_VEHICLE_NODE_WITH_HEADING`, `nodeType 0`, ภายใน 150 ม.) แล้วใช้ทิศของถนน
- รถหลายคันเรียงตามแนวถนนย้อนหลัง (`Spacing(kind)` — รถบรรทุก 24 ม.)
- สร้างรถเมื่อผู้เล่นเข้าใกล้ <220 ม. ลบเมื่อไกล >520 ม. (เฉพาะรถที่ยังอยู่ที่ช่อง/ว่าง) รถที่ผู้เล่นเอาไป = `Claim` → ช่องเป็น `used` เติมใหม่ได้เมื่อผู้เล่นห่าง >80 ม. จุดว่าง (`IS_ANY_VEHICLE_NEAR_POINT`) และผ่าน 15 วินาที (ป้องกันรถใหม่เกิดซ้อนรถเดิม)

### 6.4 เส้นนำทาง
`RouteSet(blip)` — มี route ได้ทีละ blip เปิดซ้ำทุก 1.5 วินาทีใน `RouteTick` (เกม/มอดอื่นล้างเองได้) ใช้ `BlipPos(..., route=true)` หรือ `RouteSet` โดยตรง **ห้าม** เรียก `SET_BLIP_ROUTE` เอง

### 6.5 ถาดสไลด์ (`slide.h`)
- ถาดของ `flatbed` เป็นส่วนหนึ่งของ `chassis` (ตรวจจาก `flatbed.yft` ไม่มี bone `scoop`/`misc_*`) จึงเอียงไม่ได้ → ใช้ **ทางลาดจำลอง**: ล็อกรถ (`FREEZE_ENTITY_POSITION`), ปิดการชนกับรถสไลด์ (`SET_ENTITY_NO_COLLISION_ENTITY` ทุกเฟรม), เลื่อนรถไปที่พื้นท้ายรถ (30%) แล้วไต่เส้นเอียงขึ้นถาด (70%) ตั้งมุมก้มเงยตามองศาทางลาด จบด้วย `ATTACH_ENTITY_TO_ENTITY` แบบปิดการชน
- ข้อมูลถาดที่วัดจากโมเดล: พื้นสูง z≈0.58, ศูนย์กลาง y≈-3.4 (พิกัดท้องถิ่นของ `flatbed`) ใน `Slide::ProfileOf` รุ่นอื่นใช้ค่า `[Slide] Z` + ขนาดโมเดลจาก `GET_MODEL_DIMENSIONS`
- `FreeSlide` ทำให้ใช้ได้โดยไม่ต้องรับงาน (ถอยท้ายใส่รถคันใดก็ได้ แล้วกดปุ่มกระทำ)
- ถ้ามีรถมอดที่มี bone ถาดจริง (`scoop`, `misc_a`, `misc_b`) อาจเขียนตัวควบคุมถาดเอียงเพิ่มได้ (ยังไม่ทำ; ใช้ปุ่มดีบัก Ctrl+Alt+F7 ดูว่ารถมี bone ใดบ้าง)

## 7. Input และการตั้งปุ่ม
- ทุกการกระทำ (`A_PAGER`, `A_ACCEPT`, `A_DISMISS`, `A_ACTION`) มี `Bind { key, mid, pid, btn }` — คีย์บอร์ดและพวงมาลัยตั้งคู่กันได้
- คีย์บอร์ด: keyboard hook ของ Script Hook V (กดครั้งเดียว ไม่นับ repeat) → `Input::PushKey`
- พวงมาลัย: `Input::Poll()` ทุกเฟรม ใช้ `joyGetPosEx` (WinMM) อ่านปุ่มสูงสุด 32 ตัว/อุปกรณ์ จับขอบขาขึ้น สแกนอุปกรณ์ใหม่ทุก 20 วินาที (5 วินาทีถ้ายังไม่เจอ) เก็บ `wMid/wPid` เพื่อไม่ผูกกับลำดับอุปกรณ์
- `HandleInput` (main.cpp): ปุ่มดีบัก → โหมด capture → จับคู่ `Bind` → ปุ่มนำทางในเพจเจอร์ (ลูกศร/Enter/Del/Esc)
- การตั้งค่าที่ผู้เล่นเปลี่ยนในเกม (ปุ่ม, เปิด/ปิดงาน, การ์ดสด) เรียก `SaveConfig()` เขียนกลับ ini ทันที (`WritePrivateProfileString`)

## 8. ไฟล์ข้อมูล
ดูรูปแบบเต็มในคู่มือผู้ใช้ หัวข้อ 9 สรุปสำหรับนักพัฒนา:
- `ServicesJob\ServicesJob.ini` อ่านด้วย `GetPrivateProfileString` (`LoadConfig`) รองรับค่าเก่าในหมวด `[Keys]`
- `depots.txt`, `tours.txt` — UTF-8 มี/ไม่มี BOM ก็ได้ แยกด้วย `|` บรรทัดขึ้นต้น `#` คือคอมเมนต์ หมวดคือ `[ชื่อ]`
- `save.txt` — `earned=` และ `<kindIndex>=<count>`
- ข้อความไทยใน `log.txt` ไม่แสดงถูกต้องใน Notepad บางตัวเพราะ `Log` เขียนไบต์ UTF-8 ดิบ (ไม่ใส่ BOM)

## 9. เพิ่มงานใหม่ (ขั้นตอน)
1. เพิ่มค่า enum ใน `common.h` ก่อน `K_COUNT` และเพิ่มชื่อใน `KindName` (`jobcore.h`), สี `KindCol` (`pager.h`), ชื่อ ini ใน `LoadConfig/SaveConfig` (`dutyNames`), `Depots::KindFromName/Models/Spacing/Sprite/BlipLabel`
2. สร้าง `xxx.h`:
   ```cpp
   struct XxxJob : Job {
       std::unique_ptr<VehicleGate> gate; int stage = 0;
       std::string Name() override { return "ชื่องาน"; }
       static std::unique_ptr<Offer> MakeOffer(Vector3 playerPos);   // หา pos บนถนน (FindRoad) + คำนวณค่าจ้าง
       void Start() override { gate.reset(new VehicleGate(K_XXX, "รถ...", {"model"}, [](Vehicle v){ return ...; })); objective = gate->Objective(); }
       void Tick() override { if (stage == 0) { if (!gate->Update()) { objective = gate->Objective(); return; } /* ได้รถแล้ว */ stage = 1; } ...; }
       void Cleanup() override { /* ลบ blip: DelBlip; ปล่อย entity: Rel(...) / Del(...) */ }
   };
   ```
3. `#include "xxx.h"` ใน `main.cpp` (ก่อน `input.h`) และเพิ่ม `case K_XXX:` ใน `Offers::Make` (`pager.h`)
4. กฎการเขียน `Tick`: ใช้ `Ui::Zone` วาดจุดหมายทุกเฟรม, `Complete()`/`Fail()` เมื่อจบ, ลบทุก blip/entity ใน `Cleanup()` (ถูกเรียกทั้งตอนจบปกติ ยกเลิก ผู้เล่นตาย และ exception)
5. จุดเกิดเหตุให้ใช้ `FindRoad` (ถนนหลักเท่านั้น) และสร้าง entity จริงเมื่อผู้เล่นเข้าใกล้ (<230–300 ม.) ไม่สร้างไกลๆ (โมเดล/คอลลิชันยังไม่โหลด)

## 10. กับดักและบทเรียน
| เรื่อง | รายละเอียด |
|---|---|
| ScriptHookVDotNet 3.6.0 พังบนเกมรุ่นใหม่ | `SHVDN.NativeMemory..cctor` NullReference ทำให้ native ที่มี string, `Model(string)`, `CustomSprite`, `Entity` property พัง จึงย้ายมา C++ ล้วน มอด SHVDN ที่ค้างอยู่ในเกมยังทำให้ `clr.dll` ดักข้อยกเว้นผิดพลาดจนสคริปต์ C++ ถูกฆ่า |
| ชนิดโหนดถนน | `nodeType` ของ `GET_CLOSEST_VEHICLE_NODE*`: **0 = ถนนหลัก**, 1 = ทางใดๆ (ทางเข้าบ้าน ลานจอด ซอย) ใช้ 1 ทำให้รถไปติดในลานจอด |
| `DRAW_RECT` | SDK รุ่นนี้รับ `Vector2` ตัวแรก และ `BOOL p8` ตัวท้าย (ไม่ใช่ float x,y) ตรวจลายเซ็นใน `sdk\inc\natives.h` เสมอ |
| marker ไกล | `DRAW_MARKER` ไม่วาดเมื่อไกล ต้องใช้ `DRAW_POLY` |
| Overlay ทับของเกม | overlay D3D วาดตอน Present จึงอยู่เหนือทุกอย่างรวมเคอร์เซอร์เมาส์ของเกม ต้องวาดเคอร์เซอร์เอง และเพราะ `Ui::Rect` ก็เป็น overlay แล้ว ห้ามผสม `DRAW_RECT` กับรูปทรง overlay (ลำดับชั้นจะผิด) |
| รถซ้อนที่จุดรับงาน | เติมช่องจอดทันทีหลังผู้เล่นขึ้นรถ → รถใหม่เกิดซ้อนรถผู้เล่น แก้ด้วย `Slot.used` + `SlotClear` |
| แท็กซี่จำรถผิดคัน | งานเก็บรถคันแรกที่ผู้เล่นขับ ผู้เล่นสลับรถแล้วระยะไม่เปลี่ยน แก้ให้ตามรถที่ขับอยู่ก่อนรับผู้โดยสาร |
| ค่าใน ini | ใช้เฉพาะค่า ASCII (ชื่อปุ่ม ตัวเลข ชื่อโมเดล) ข้อความไทยเก็บใน `depots.txt` / `tours.txt` (UTF-8) ซึ่งอ่านเองด้วย `ReadAll` ไม่ผ่าน API ของ ini |
| ขนาดฟอนต์ไทย | `kLine = 1.5` เท่าของ `h` (ต้องเผื่อสระ/วรรณยุกต์) ฟอนต์ PSK บางที่ขนาดเล็ก เรนเดอร์ตัวธรรมดาซ้ำเยื้อง 0.4px ให้หนาขึ้น |

## 11. ดีบักและทดสอบ
- `ServicesJob\log.txt` (เวลา + ข้อความ), `ScriptHookV.log` (โหลดสคริปต์, exception)
- ปุ่มดีบัก (Ctrl+Alt ค้าง): **F7** = บันทึกชื่อ bone ของรถ (จากรายการที่รู้จัก), **F8** = พิกัด/ทิศ ในรูปแบบ `depots.txt`
- เกมเปิดอยู่: เปลี่ยนชื่อ `.asi` เก่า แล้ววางตัวใหม่ รีสตาร์ตเกมเพื่อโหลด (ไม่มี hot reload; สร้างไฟล์ `ScriptHookV.dev` ว่างๆ ไว้ใช้ระบบ reload ของ Script Hook V ได้ ตามเอกสาร SDK แต่ไม่ได้ทดสอบกับมอดนี้)

### เช็กลิสต์ทดสอบก่อนปล่อยรุ่น
- [ ] เปิดเกม F9 ขึ้น ข้อความไทยคม ไม่มี exception ใน `log.txt`
- [ ] ตั้งปุ่มคีย์บอร์ดและพวงมาลัย บันทึกแล้วปิด-เปิดเกมยังอยู่
- [ ] การ์ดงานเด้ง รับ/ปฏิเสธ/หมดเวลา → เข้ากล่องข้อความ
- [ ] ทุกงานทำจนจบได้ ได้เงิน และยกเลิกกลางคัน blip/entity ถูกลบหมด
- [ ] เริ่มงานจากรถที่ขับอยู่แล้ว (ไม่ต้องไปจุดรับงาน) และจากจุดรับงาน
- [ ] จุดรับงานทุกจุด: รถจอดบนถนน ตั้งตรง ขับออกได้ ไม่เกิดซ้อน
- [ ] ถาดสไลด์: ขึ้น-ลง ไม่ดีด รถค้างไม่ทะลุ
- [ ] ไม่มีข้อผิดพลาดตอนเปลี่ยนตัวละคร/โหลดเซฟ/ตาย

## 12. ใบอนุญาตและเครดิตสำหรับการแจกจ่าย
- ห้ามแจก Script Hook V SDK/DLL ในแพ็ก (ลิงก์ให้ผู้ใช้ดาวน์โหลดเอง) มอดที่ build ด้วย SDK ต้องตั้งใจใช้ออฟไลน์เท่านั้น
- ฟอนต์ TH Sarabun PSK (SIPA) แจกจ่ายได้ฟรีพร้อมคงประกาศ `src\FONT_NOTICE.txt`
- ServicesJob โดย NakimiTV **แจกฟรี** ใช้ได้โดยไม่เสียค่าใช้จ่ายและไม่มีการรับประกันใดๆ
- **ฟอนต์ไทย TH Sarabun PSK แจกจ่ายได้** (ฟอนต์มาตรฐานไทยของ SIPA ใช้และแจกฟรี) จึง **ฝังมาในมอดเลย** (`ServicesJob.asi` และ `src\font_data.h`) พร้อมแนบประกาศ `FONT_NOTICE.txt` ในทุกแพ็ก ผู้ใช้ไม่ต้องติดตั้งฟอนต์
- **Script Hook V ไม่แจกรวม** ให้ผู้เล่นดาวน์โหลดเอง (ทั้งตัวรันไทม์และ SDK)

## 13. แผนที่ยังไม่ทำ / ไอเดีย
- ตัวควบคุมถาดเอียงจริงสำหรับรถมอดที่มี bone `scoop`
- ไอคอนประจำแต่ละงานบนการ์ด
- อ่านปุ่มพวงมาลัยผ่าน DirectInput เพื่อรองรับ hat/POV และมากกว่า 32 ปุ่ม
- ตั้งค่าอื่นๆ ผ่านหน้าต่างเพจเจอร์ (ค่าจ้าง ความถี่การส่งงาน)
- หน้าจัดการจุดรับงานในเกม (วางจุดที่ตำแหน่งปัจจุบัน)
