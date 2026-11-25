# CubeEvil4 Custom Mods, Xbox 360 USB Driver & Upstream Sync Guide

Personal fork documentation for **naksh-07/CubeEvil4-Android**, tracking both:
- **Upstream (`upstream`)**: `https://github.com/Andiweli/CubeEvil4-Android.git` (core decomp & Android shell updates)
- **Mod Fork (`remaked-mods`)**: `https://github.com/necroxmar01-prog/CubeEvil4-Android-remaked-mods.git` (`mods/free-movement` — modern movement, over-the-shoulder aim/walk, weapon carry IK, Ashley follow/combat AI)

---

## 1. Branch Architecture (`naksh-07/CubeEvil4-Android`)

| Branch | Base | Purpose |
| :--- | :--- | :--- |
| `upstream-master` | `upstream/master` | Clean mirror of `Andiweli/CubeEvil4-Android` `master` for easy diffing & rebasing. |
| `mods/free-movement` | `remaked-mods/mods/free-movement` (`1102d168`) | **Primary daily-driver branch** (checked out at `.worktrees/mods-free-movement`): free-movement fork + RE4 Remake (2023) XWB soundtrack + Direct USB Host Xbox 360 driver + `Xbox 360 Shooter` preset + rumble fix. |
| `master` | `upstream/master` | Includes Modern Controls (`0990-modern-controls.patch`), RE4R translucent iOS glass HUD skin (`TouchStyle.java` 5th look), `cubeevil-agent-mcp` device testing tools, and `.agents/` tri-tier memory. |

---

## 2. Implemented Customizations & Bug Fixes

### A. Wired Xbox 360 / ShanWan Controller Bypass & Presets (`mods/free-movement`)
On Android 15 / HyperOS 2 (Linux `5.10` kernel without root), wired ShanWan-based Xbox 360 clones (`045e:028e`) stall on **LED #2** because stock kernel `xpad` skips the ShanWan wake handshake, while switching to **LED #1** (`2563:0575`) falls back to Android `Generic.kl` (no vendor `.kl`) and scrambles button labels.

- **Direct USB Host XInput Reader (`UsbXInputDriver.java`, `AndroidManifest.xml`, `CoreActivity.java`):**
  - Detects silent kernel `045e:028e` devices (`probeIfKernelSilent`), requests Android USB Host permission (`UsbManager`), claims interface `255/93/1` (`claimInterface(target, true)`), sends the ShanWan wake control transfer (`0xC1, 0x01, 0x0100, 0x0000`) + solid LED #1 (`{0x01, 0x03, 0x06}`), and decodes 20-byte XInput packets (`parseReport`, `USB_DEVICE_ID = -9001`).
  - Falls back with a user toast hint to hold **Home 3s -> LED #1** if a kernel/SELinux policy blocks interface claim.
- **USB XInput Rumble Pulse-Stop Fix (`UsbXInputDriver.java`, `CoreActivity.java`):**
  - Unlike Android's `VibrationEffect.createOneShot`, XInput USB motor packets (`{0x00, 0x08, 0x00, big, small, 0, 0, 0}`) stay latched until zeroed.
  - `rumbleTick` in `CoreActivity.java` now calls `usbXInput.setRumble(0)` as soon as a rumble pulse ends, and `UsbXInputDriver.setRumble` deduplicates states (`lastRumbleLevel`) and dispatches bulk transfers asynchronously off the UI thread (`RE4UsbRumble`) plus zeros motors on pause/disconnect.
- **ShanWan `2563:0575` Fallback Normalizer (`CoreInput.java`, `CoreActivity.java`, `CoreMenu.java`, `CheatDialog.java`):**
  - `CoreInput.normalizeControllerKey(0x2563, 0x0575, hasCzNoThumbs, keyCode)` normalizes `Generic.kl`-shifted scancodes (`A/C`, `Y/Z/L1/R1/L2/R2/SELECT/START`) back to true Xbox labels when LED #1 HID mode is used.
- **`Xbox 360 Shooter` Preset & Native `L2 + X` Reload (`ActionMap.java`, `CoreInput.java`, `CoreMenu.java`, `CoreActivity.java`):**
  - Auto-applied via `controllerMapVer = 2` (unless manually customized):
    - `A` = Action, `B` = Run / Quick Turn, `Y` = Inventory, `X` = Quick Reload
    - `LT` = Aim, `LT + RT` = Fire, `LT + X` = Native Reload (`CoreInput.isAimHeld()` suppresses Quick Reload hijack while LT is squeezed)
    - `LB` = Knife / Parry, `LB + RT` = Slash (knife-slash quick-reload collision fixed in `CoreInput.pad()`)
    - `RB` = Map, `L3` = Jog toggle, `R3` = Ashley command, `START` = Options, `SELECT` = App Menu
  - `RE4 Remake (Type A-1)` (`RB = Sprint`, `B = Map`) remains selectable in-game via `SELECT -> Controls`.
- **Unit Tests (`XboxControllerMapTest.java`):**
  - Verifies `2563:0575` scancode normalization, `ActionMap.xbox` (`B=Run`, `RB=Map`), native `L2+X` reload vs tap `X` quick reload, `ActionMap.remake` knife-slash behavior, and `UsbXInputDriver.parseReport` / `rumblePacket`.

### B. RE4 Remake (2023) Dynamic XWB Soundtrack Mod
- **Files:** `port/whole/runtime/remake_audio.cpp/.h`, `sound_volume.cpp`, `ai.cpp`, `core_api.cpp`, `re4_core.h`, `CMakeLists.txt`, `port/android-decomp/app/src/main/cpp/core_mode.cpp`, `IngameSettings.java`, `CoreMenu.java`, `CoreBridge.java`, `whole_link_plan.py`, `whole_prepare.py`.
- Auto-detects `mods/audio/bio4bgm.xwb` (78-track WaveBank) and switches BGM to `Remake (2023)` (`ENABLED (Remake 2023)`), with an in-game toggle in `IngameSettings` / `CoreMenu` (`Audio -> Soundtrack`).

### C. Large Mod Assets (Git-Ignored Locally & On Device)
Because GitHub rejects blobs > 100 MB, large asset packs are excluded in `.gitignore` (`*.c4tex`, `*.xwb`, `*.rar`, `mods/`) and kept on PC + Android external app storage:

| Asset | PC Path (Workspace Root) | Android Path (`/sdcard/Android/data/com.ast.cubeevil4/files/...`) | Notes |
| :--- | :--- | :--- | :--- |
| **HD Textures Pack** (1024px) | `mods/textures/pack.c4tex` | `textures/pack.c4tex` | 5916 images, budget 768 MB |
| **Remake 2023 Soundtrack** | `mods/remake_soundtrack/bio4bgm.xwb` | `mods/audio/bio4bgm.xwb` | 78 tracks (~1.77 GB) |

---

## 3. Build & Unit Test Quick Reference (Windows)

`port/android-decomp/gradlew.bat` inherits Windows `PATH`; if another tool on `PATH` contains an unmatched quote (e.g. `eSpeak NG"`), Gradle's test executor fails unless `PATH` is scoped to JDK + System32:

```powershell
$env:PATH = "C:\Program Files\Microsoft\jdk-17.0.20.101-hotspot\bin;C:\WINDOWS\system32;C:\WINDOWS"
Set-Location ".\.worktrees\mods-free-movement\port\android-decomp"
.\gradlew.bat testSideloadDebugUnitTest `
  --tests "com.ast.re4decomp.XboxControllerMapTest" `
  --tests "com.ast.re4decomp.ControllerAxesTest" `
  --tests "com.ast.re4decomp.CoreMenuLayoutTest" `
  --tests "com.ast.re4decomp.ModsPageTest" `
  assembleSideloadDebug "-Pre4.versionCode=131" "-Pre4.versionName=1.1.1-mods" --no-daemon
```

> Note: Building `assembleSideloadDebug` after editing files under `port/whole/` automatically triggers CMake + Ninja (`libre4_main.so`, `libre4_platform.so`). Keep `src/Tools/*` untouched (`--skip-worktree`) on Windows NTFS due to case-insensitive `src/Tools/` vs `src/tools/` collisions.

---

## 4. Upstream & Mod-Fork Sync Workflow

Remotes configured in local repository:
- `origin` -> `https://github.com/naksh-07/CubeEvil4-Android.git`
- `upstream` -> `https://github.com/Andiweli/CubeEvil4-Android.git`
- `remaked-mods` -> `https://github.com/necroxmar01-prog/CubeEvil4-Android-remaked-mods.git`

### Updating `upstream-master` when Andiweli pushes a new release:
```powershell
git fetch upstream
git checkout upstream-master
git reset --hard upstream/master
git push origin upstream-master
```

### Pulling future updates into `mods/free-movement` (recommended):
1. Check if `necroxmar01-prog` already rebased `mods/free-movement` onto the latest `Andiweli/master`:
   ```powershell
   git -C ".worktrees\mods-free-movement" fetch remaked-mods upstream
   git -C ".worktrees\mods-free-movement" log --oneline ..remaked-mods/mods/free-movement
   ```
2. Replay **only our custom commits** (Remake Audio + Direct USB XInput + Xbox Preset + Docs) onto the updated base:
   ```powershell
   git -C ".worktrees\mods-free-movement" rebase --onto remaked-mods/mods/free-movement 1102d168
   ```
   *(Or if pulling directly from `upstream/master` instead: rebase or cherry-pick the feature commits onto a new branch from `upstream/master` — `UsbXInputDriver.java`, `ActionMap.xbox`, and `XboxControllerMapTest.java` live cleanly in `port/android-decomp/` Java classes with minimal touch points.)*
3. Re-test & push updated fork branch:
   ```powershell
   git -C ".worktrees\mods-free-movement" push origin HEAD:mods/free-movement --force-with-lease
   ```
