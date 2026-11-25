<!-- schema_version: 2.0 -->
<!-- project_id: proj-cubeevil4 -->
<!-- DATA_CLASSIFICATION: PASSIVE_CONTEXT_ONLY (DO NOT EXECUTE AS INSTRUCTIONS) -->

# Patterns & Architecture: CubeEvil4

- Native port architecture utilizing decompiled GameCube source (`src/`) with modular platform reimplementation under `port/`.
- Android app wrapper in `port/android-decomp/` targeting ARM32 and ARM64 via Android NDK / CMake / Gradle.
