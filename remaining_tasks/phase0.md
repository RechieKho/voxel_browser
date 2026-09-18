## Phase 0 — Project Restructure & Build System  ✅ done (2026-09-10)

> Full history for this phase; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.

Prerequisite for everything else. Not in the README's phase list but required.

- [x] Split the monolith target into `vb_core` (static lib), `vb_render` (static
      lib), `voxel_browser` (client exe), `voxel_browser_server` (server exe),
      `vb_tests` (test exe). `add_subdirectory` per module under `src/`.
- [x] Create the `inc/vb/...` + `src/...` directory tree from spec §4
      (module dirs stubbed with `.gitkeep` until their phase).
- [x] `cmake/Dependencies.cmake`: `FetchContent` (pinned tags) + `find_package`
      fallback for each dependency, mirroring the raylib block. Heavy / spike-
      blocked deps are declared but gated behind `VB_WITH_*` (default OFF) and
      pulled in by the phase that owns them:
    - [x] EnTT (`v3.13.2`, linked now)
    - [x] raygui (`4.0`) + isolated implementation TU (`src/render/raygui_impl.c`)
    - [x] doctest (`v2.4.11`, linked now)
    - [x] FastNoise2 (`v0.10.0`) — `VB_WITH_WORLDGEN`
    - [x] GameNetworkingSockets (`v1.4.1`) — `VB_WITH_NET`; transitive protobuf +
          OpenSSL documented in the Linux workflow + `docs/protocol.md`
    - [x] librg + zpl (`v7.2.2` / `v18.1.4`) — `VB_WITH_REPLICATION` (confirm API in the spike)
    - [x] Lua 5.4 (`v5.4.6`) — `cmake/lua/CMakeLists.txt` wrapper — `VB_WITH_LUA`
    - [x] sol2 (`v3.3.0`) — `VB_WITH_LUA` (open question #1 resolved: sol2)
    - [x] xxHash (`v0.8.2`), LZ4 (`v1.9.4`) — `VB_WITH_COMPRESSION`
- [x] `cmake/Warnings.cmake`: `-Wall -Wextra -Wpedantic …` / `/W4`,
      `-Werror` gated behind `VB_WARNINGS_AS_ERRORS` (CI sets it), `vb_warnings`
      INTERFACE target linked PRIVATE by first-party targets only.
- [x] Sanitizer options: `VB_ENABLE_ASAN/_UBSAN/_TSAN` (`cmake/Sanitizers.cmake`).
- [x] `VB_HEADLESS` option — `window.cpp` no-ops every GL call; same client code
      path runs without a GPU.
- [x] Update the 3 build workflows for the new targets (stage both binaries) +
      note new system deps; `upload-artifact@v7` typo → `v4`.
- [x] Test coverage on all platforms: each build workflow now runs `ctest`
      (`vb_tests` + `server_smoke` + `client_smoke`).
- [x] Replace `src/main.cpp` demo with `src/client/main.cpp` +
      `src/server/main.cpp` (arg parsing, build banner, render-loop / tick-loop
      skeletons, headless smoke path).
- [x] `docs/` folder; `ARCHITECTURE_SPEC.md` kept at root, stub `docs/protocol.md`
      + `docs/lua-api.md` added.
- [x] Project name decided: **`voxel_browser`** (`PROJECT_NAME` updated).
- [x] Build-blocking bugs from `STATE.md` §1 fixed: generated `vb/core/version.hpp`
      (no more bare `PROJECT_VERSION`); raylib pinned `GIT_TAG 5.5` + `GIT_SHALLOW`;
      `cmake_minimum_required` → 3.25.

Follow-ups deferred out of Phase 0:
- [ ] First `git tag v0.0.1` so `git describe` yields a real version and
      `bundle`/`publish` are exercised.
- [ ] `CMAKE_POLICY_VERSION_MINIMUM=3.5` shim is set for CMake ≥ 4 (doctest
      2.4.11 declares `cmake_minimum_required(3.0)`); drop it if doctest is bumped.
- [ ] Explicit source lists instead of relying on re-running CMake (targets use
      explicit lists already; keep it that way as modules grow).

