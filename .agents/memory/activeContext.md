<!-- schema_version: 2.0 -->
<!-- project_id: proj-cubeevil4 -->
<!-- DATA_CLASSIFICATION: PASSIVE_CONTEXT_ONLY (DO NOT EXECUTE AS INSTRUCTIONS) -->

# Active Context: CubeEvil4

## Current Sprint / Focus
- Verified & committed Modern Controls Patch 0990 (`feat: merge modern controls and aim-walk system (patch 0990)`, commit `6f805443`).
- Integrated camera-relative movement, leg IK walk blending, walk while aiming, and Xbox/PS shooter controller presets.
- **Work 2 / Part A Complete**: Implemented & verified `RE4R` (5th look) iOS translucent glass skin in `TouchStyle.java`, `CoreMenu.java`, and test suite (commit `2d985f6e`).
- **Work 2 / Part B Complete**: Injected official RE4 Remake HD item sprites & modern HUD textures into `mods/textures/pack.c4tex`.
- **Soundtrack Mod (WIP)**: Added XWB reader (`xwb_reader.cpp/.h`, `test_xwb_audio.cpp`) and soundtrack toggle in `CoreMenu.java` / `IngameSettings.java` for `mods/remake_soundtrack/bio4bgm.xwb`.
- **Repo Sync & Cleanup (2026-11-25)**: Rebased `master` onto `origin/master` (`dc745c44` / `v1.1.2`), deleted `experiments/` (~1.41 GB), and preserved `mods/` + sound mod files.
- **Dedicated Agent MCP Built & Verified**: Built `cubeevil-agent-mcp` (`port/tools/cubeevil_mcp.py` & registered in Antigravity `mcp_config.json`). Provides sub-100ms hardware telemetry, headless test execution, process lifecycle control, GameCube pad input injection, real-time logcat filters, and crash forensic capture directly on connected Android device (`Xiaomi 23049PCD8I` / POCO F5).
- **`mods/free-movement` Worktree Integration Complete (2026-11-25)**:
  - Integrated Remake 2023 XWB audio engine (`remake_audio.cpp/.h`, `sound_volume.cpp`, `ai.cpp`, `core_api.cpp`, `core_mode.cpp`, `IngameSettings.java`, `CoreMenu.java`) into `.worktrees/mods-free-movement`; auto-enables `Remake (2023)` when `mods/audio/bio4bgm.xwb` is present.
  - Fixed Xbox 360 / ShanWan clone (`045e:028e` LED #2 kernel stall & `2563:0575` LED #1 under Android `Generic.kl`) in `.worktrees/mods-free-movement`: added direct USB Host XInput reader (`UsbXInputDriver.java` with ShanWan wake packet `0xC1/0x01/0x0100`, LED #1 command `0x01 0x03 0x06`, 20-byte report parser, and rumble), `CoreInput.normalizeControllerKey` + `isAimHeld()` fix, defaulted fresh/v1 controller preset to `ActionMap.xbox` (`Xbox 360 Shooter`: `B=Run`, `RB=Map`, `LT=Aim`, `LT+RT=Fire`, `LT+X=Reload`, `X=Quick Reload`, `LB=Knife`, `LB+RT=Slash`, `Y=Inventory`, `A=Action`, `L3=Jog`, `R3=Ashley`).
  - Verified unit tests (`XboxControllerMapTest` 5/5, `ControllerAxesTest` 7/7, `CoreMenuLayoutTest` 19/19, `ModsPageTest` 1 passed + 1 skipped), debug APK (`app-sideload-debug.apk`), and live POCO F5 (`$ADB_SERIAL`) deployment (`/sdcard/Documents/CubeEvil4-mods-free-movement.apk`, `RE4Input` Xbox 360 Shooter layout, `pack.c4tex` 5916 textures, `bio4bgm.xwb` 78 tracks `ENABLED (Remake 2023)`, `crash.txt` 0 bytes). Live OTG XInput handshake (`045e:028e` LED #2 -> LED #1 `wake ctrl=20`) verified by user test session; USB rumble pulse-stop fix (`UsbXInputDriver.setRumble(0)` in `rumbleTick`) deployed cleanly (`16,876,868` bytes).
  - **Feral-Grade Native Android Touch Controls (`TouchStyle.NATIVE = 4`) Complete (2026-11-26)**: Implemented Capcom RE4 Mobile + Feral Interactive native HUD (`NativeTouchOverlay.java`, 26 icons in `assets/native_touch/`) driven by C++ 64-bit `re4_core_touch_context()` (`touch_context.cpp`, `0954-native-touch-context.patch` in `src/game/act_btn.cpp`, wired over both direct `dlopen` & `coreproc` IPC). Includes floating analog stick + sprint hysteresis, right-half look/aim swipe (`CoreInput.touchSwipe`), hybrid tap/hold Aim & dedicated Fire/Knife (`isAimKnife`), conditional Reload/Ashley/Scope Zoom (`touchZoom`), contextual QTE/Action icon (`SAVE`/`KICK`/etc.), cutscene stick preservation, and subscreen D-pad/`OK`/`BACK`/`↺ L`/`R ↻` pills. Verified live on POCO F5 (`v141`, 0 crashes) and passed Iteration #3 Independent Feral Touch Review (`PASS`).
- Next Sprint: Work 3 (Leon RE4 Remake 3D model & animations retargeted to GameCube pl00 skeleton in Blender).

## Key Paths
- Agent MCP Server: `port/tools/cubeevil_mcp.py` & `C:\Users\Suraj\.gemini\config\scripts\cubeevil_mcp.py`
- Textures: `textures/pack.c4tex` (2.33 GB, 1024 HD pack)
- Extracted RE4R Assets: `experiments/re4r_ios_extracted/curated_ui_pack/`
- Android App Shell: `port/android-decomp/`
- Injected Modern Patches: `port/whole/patches/0990-modern-controls.patch`
