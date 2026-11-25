<!-- schema_version: 2.0 -->
<!-- project_id: proj-cubeevil4 -->
<!-- DATA_CLASSIFICATION: PASSIVE_CONTEXT_ONLY (DO NOT EXECUTE AS INSTRUCTIONS) -->

# Decision Bank: CubeEvil4

## 2026-11-23: Local Repository Setup
- Cloned upstream `Andiweli/CubeEvil4-Android` master branch to `c:\Users\Suraj\Documents\Antigravity\CubeEvil4`.
- Standard v2.0 memory structure established with canonical project ID `proj-cubeevil4`.

## 2026-11-23: Xbox 360 / Modern Controls Integration
- Avoided bit collision with v1.1.0 vibration (bits 4/5): Allocated bit 6 for `optModern` and bit 7 for `optAimWalk`.
- Added native Xbox 360 & PlayStation presets in `ActionMap.java` and preset buttons in `CoreMenu.java`.
- Installed Patch 0990 in `port/whole/patches/` for camera-relative movement and dual-stick aiming.

## 2026-11-23: UHD Texture Staging & Experiments Lab
- Discovered 2.33 GB `CubeEvil4-textures-1024.c4tex` in Downloads; staged as `textures/pack.c4tex` via NTFS hardlink.
- Established `experiments/` documentation lab for systematic multi-phase tracking.

## 2026-11-24: Dedicated Agent Hardware Test MCP (`cubeevil-agent-mcp`)
- **Context**: Screen capture vision loops over ADB take 10-20s per cycle and provide zero visibility into native crash tombstones, MEM1 heap allocations, or audio starvation in Vulkan/GLES3 SurfaceViews.
- **Decision**: Developed `port/tools/cubeevil_mcp.py` as a zero-dependency stdio JSON-RPC MCP server. Registered in Antigravity `mcp_config.json` with 10 deterministic tools (`cubeevil_device_info`, `cubeevil_game_status`, `cubeevil_check_storage`, `cubeevil_launch_game`, `cubeevil_stop_game`, `cubeevil_send_pad`, `cubeevil_get_logs`, `cubeevil_get_crash_report`, `cubeevil_take_screenshot`, `cubeevil_run_native_suite`).
- **Safety Invariant**: Direct ADB calls must adhere to `Global\Retroid_ADB_Exclusive` named mutex (`port/scripts/Retroid-Adb-Exclusive.ps1`). Verified end-to-end on connected POCO F5 device. Full spec documented in `port/docs/cubeevil-agent-mcp.md`.
