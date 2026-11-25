# Project-Specific Agent Protocol: CubeEvil4

Inherits from global `C:\Users\Suraj\.gemini\config\AGENTS.md`.

- **Canonical Project ID**: `proj-cubeevil4`
- **Root Directory**: `c:\Users\Suraj\Documents\Antigravity\CubeEvil4`
- **Upstream Repository**: `https://github.com/Andiweli/CubeEvil4-Android.git`

## Architectural Invariants
1. **Core Philosophy**: Native port of GameCube Resident Evil 4 (G4BE08 / retail) to Android (`armeabi-v7a` and `arm64-v8a`). Runs original game code compiled directly from `src/` where possible.
2. **Whole Program Build**: Platform layer (GX, VI, OS, DVD, PAD, AX, CARD, ARAM, CRI-Video) is replaced in `port/`. Original `src/` and `include/` are preserved to maintain matching accuracy.
3. **App Shell**: Android application located in `port/android-decomp/` (Package: `com.ast.cubeevil4`). Handles disc import, GLES3/Vulkan rendering, touch/gamepad controls, and audio output.
4. **Platform Note (Windows NTFS)**: Repository contains case-colliding files (`src/Tools/` vs `src/tools/`). Be mindful when editing tools or running builds on case-insensitive filesystems.
5. **Hardware Testing & Telemetry Protocol (`cubeevil-agent-mcp`)**: When testing on physical Android devices, agents MUST prioritize `cubeevil-agent-mcp` tools (`cubeevil_game_status`, `cubeevil_send_pad`, `cubeevil_get_logs`, `cubeevil_run_native_suite`, `cubeevil_get_crash_report`). Unrestricted screencap polling loops are banned. Direct ADB access must respect the `Global\Retroid_ADB_Exclusive` mutex via `port/scripts/Retroid-Adb-Exclusive.ps1`. Full docs: [cubeevil-agent-mcp.md](file:///c:/Users/Suraj/Documents/Antigravity/CubeEvil4/port/docs/cubeevil-agent-mcp.md).
