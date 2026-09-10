# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.

Last updated: 2026-09-10 (Phase 0 complete)

---

## 0. Repo snapshot

- `674d88f Initial commit` + the Phase 0 restructure (uncommitted at time of
  writing). Still **no tags**. Remote: `https://github.com/RechieKho/voxel_browser.git`.
- Source tree now matches spec §4: `vb_core` (`src/core/`), `vb_render`
  (`src/render/`), `voxel_browser` (`src/client/`), `voxel_browser_server`
  (`src/server/`), `vb_tests` (`tests/`). Other module dirs are `.gitkeep` stubs.
- Builds green on Windows/clang with `-DVB_WARNINGS_AS_ERRORS=ON`; all 3 ctest
  cases pass (`vb_tests`, `server_smoke`, `client_smoke`).
- `ARCHITECTURE_SPEC.md` / `REMAINING_TASKS.md` are still design intent for
  Phase 1+.

---

## 1. Build-blocking bugs — ✅ ALL FIXED in Phase 0

### 1.1 bare `PROJECT_VERSION` in `main.cpp` — FIXED
`src/main.cpp` is gone. Version info is now a generated header
`vb/core/version.hpp` (from `cmake/version.hpp.in`, written to
`${CMAKE_BINARY_DIR}/generated/include`), exposing `vb::kVersionString`,
`vb::kEngineProtocolVersion`, etc. `vb::core::describe_build()` formats the banner.

### 1.2 raylib not pinned — FIXED
`cmake/Dependencies.cmake` uses `GIT_TAG 5.5 GIT_SHALLOW TRUE`. **Note: 5.5, not
6.0** — 6.0 was not a real upstream tag at the time; README/spec prose still say
"6.0". raygui pinned to matching `4.0`. Bump both together.

### 1.3 `cmake_minimum_required` — FIXED
Now `VERSION 3.25` (matches the `block()` usage and README).

### 1.4 CMake ≥ 4.0 rejects doctest 2.4.11 — worked around
doctest 2.4.11 declares `cmake_minimum_required(VERSION 3.0)`, which CMake 4.x
errors on. `cmake/Dependencies.cmake` sets `CMAKE_POLICY_VERSION_MINIMUM 3.5`
when `CMAKE_VERSION >= 4.0`. GitHub runners are still on CMake 3.3x so CI never
hit it. Remove the shim when doctest is bumped past a release that fixes this.

---

## 2. Naming — ✅ RESOLVED

`PROJECT_NAME` is now **`voxel_browser`**. Executables: `voxel_browser` (client)
and `voxel_browser_server`. CI artifacts are
`voxel_browser-<target>-<arch>-<build_type>` and each now contains a directory
with *both* binaries (bundle/publish still merge by `voxel_browser-*` pattern).

---

## 3. CI landmines

- **No tags exist.** `setup_metadata.yml` runs `git describe --tags --abbrev=0`
  for the `version` output — this **errors** with no tags in history. `bundle`
  and `publish` depend on `setup_metadata`. First `git tag v0.0.1` will unblock;
  until then anything past `build_*` is untested / likely red.
- `publish.yml` triggers only on `v*.*.*` tags and pulls artifacts from
  `runner.yml` by name `${project_name}` (note trailing space in the YAML on
  `name:` line 26 — `action-download-artifact` may or may not trim it).
- `lint.yml` installs clang-format via `pip install` then runs it via
  `pipx run clang-format` (two different mechanisms; the pip install is dead
  weight). Lints `src/**` with `--Werror` — every new file under `src/` must be
  clang-format-clean or CI fails.
- Build workflows use `actions/checkout@v4` with `submodules: recursive`, but
  **there are no submodules** — deps are `FetchContent`. Harmless now; README's
  "pull submodules" / `git clone --recursive` language is misleading, deps are
  fetched at configure time.
- `build_linux.yml` installs X11/GL dev packages for raylib. When GNS / other
  deps land, each build workflow's apt/brew/choco step needs the new system
  deps (OpenSSL, protobuf for GameNetworkingSockets especially).
- `runner.yml` fires on push to `main` and `*-workflow` branches, plus PRs.

---

## 4. Config / style quirks

- `.clang-format` is **Godot's** clang-format file, verbatim — includes
  `Language: Java` blocks with `JavaImportGroups: ['org.godotengine', ...]` and
  Godot TODO comments. Cosmetic; the C++ rules are what matter: **tabs**
  (`UseTab: Always`, `TabWidth 4`), `ColumnLimit: 0` (no auto-wrap),
  `AccessModifierOffset: -4`, pointers right-aligned, `Cpp11BracedListStyle: false`.
- `.clang-format` sets `Standard: c++17`; CMake sets `CMAKE_CXX_STANDARD 20`.
  Formatter parses as C++17 — fine unless C++20-only syntax confuses it; bump to
  `c++20` if that happens.
- `.gitignore` ignores `build`, `*.local`, `compile_commands.json`,
  `.vscode/*` (except `extensions.json`). `CMAKE_EXPORT_COMPILE_COMMANDS ON` is
  set — symlink/copy `build/compile_commands.json` to root for tooling.
- `CMakeLists.txt:54` uses `file(GLOB_RECURSE SOURCE_FILES ...)` — the usual
  CMake caveat: adding a `.cpp` doesn't trigger reconfigure. Either keep
  `cmake` re-runs in the loop or switch to explicit source lists in Phase 0.
- `libfantastic` (the lib target) is `add_library(... INTERFACE)` — assumes
  header-only. The Phase 0 split makes `vb_core` a real STATIC lib; don't carry
  the INTERFACE assumption forward.

---

## 5. Dependency notes / unknowns

- **Cellulose** (`github.com/RechieKho/cellulose`) is the maintainer's own repo.
  API is unknown — **inspect it directly** before writing any meshing code
  (`REMAINING_TASKS.md` Phase 2 spike). Does it emit vertex data or own GPU
  buffers? What chunk size does it expect? The 32³ choice in the spec is an
  assumption pending this.
- **FastNoise2**: README links `electronicarts/fastnoise`. Upstream is
  `Auburn/FastNoise2`; the EA repo is a fork. Confirm which one, and that it
  ships a usable `CMakeLists` / `FastNoise2Config.cmake` (it does export a
  target, but SIMD level detection can be fiddly on CI).
- **GameNetworkingSockets** is the heavy dependency: needs protobuf + a crypto
  backend (OpenSSL or libsodium/bcrypt). This dominates build setup work in
  Phase 0/1. Consider whether to vendor via `FetchContent` (slow, pulls
  protobuf) or require a system install.
- **librg** is `zpl-c/librg` — single-header, v0.x, API has churned across
  versions. Pin a specific commit and write down which (`docs/protocol.md`).
- **Lua**: PUC-Lua has no upstream CMake; needs a wrapper `CMakeLists.txt`.
  sol2 vs. raw C API is `ARCHITECTURE_SPEC.md` open question #1 — undecided.
- **raygui** is header-only (`raysan5/raygui`), needs exactly one TU with
  `#define RAYGUI_IMPLEMENTATION`. Ships with raylib's org; version-match to
  the raylib tag chosen in 1.2.

---

## 6. Design decisions still open

Tracked in `ARCHITECTURE_SPEC.md` §19, repeated here for visibility:

1. Lua binding layer: `sol2` vs. raw C API.
2. Cellulose meshing API shape (blocking spike, see §5).
3. librg version + whether we use its serialization or only its interest culling.
4. Chunk compression: LZ4 (spec's starting choice) vs. zstd vs. palette-only.
5. World persistence / region file format — deferred, but don't design the chunk
   store as in-memory-forever.
6. Auth: `auth_mode = none | token` — handshake reserves the field, no service.

Other undecided-but-not-yet-in-spec:
- Test framework: doctest vs. Catch2 (`REMAINING_TASKS.md` Phase 0 lists both).
- Final project name (§2 above).
- Whether the client embeds the server for singleplayer as a library or spawns a
  child process (spec says in-process library).

---

## 7. Conventions to follow (once code exists)

- Namespaces `vb::<module>` (`vb::net`, `vb::world`, ...); headers under
  `inc/vb/<module>/`, mirrored by `src/<module>/`.
- No exceptions on hot paths; `Result<T,E>` for fallible ops (spec §1/§14).
- Wire structs live in `inc/vb/protocol/`; every one gets a round-trip test and
  bumps `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` when changed.
- Keep both binaries buildable/runnable with `--headless` (CI + integration
  tests depend on it).
- Match `.clang-format`: tabs, no column limit, run clang-format before commit
  (CI `--Werror` on `src/**`).

---

## 8. Done / resolved

_(Move items here with a date + commit when fixed, so the history is visible.)_

- **2026-09-10 — Phase 0 restructure** (uncommitted). Modular CMake
  (`vb_core`/`vb_render`/`voxel_browser`/`voxel_browser_server`/`vb_tests`),
  `cmake/{Dependencies,Warnings,Sanitizers}.cmake` + `cmake/version.hpp.in` +
  `cmake/lua/CMakeLists.txt`, runnable client/server skeletons with `--headless`,
  generated version header, CI updated (multi-binary staging, ctest on all 3 OSes,
  `upload-artifact` v7→v4 typo, lint via `find`, `git describe` no-tag fallback),
  `docs/{protocol,lua-api}.md` stubs, `.clang-format` → c++20, `PROJECT_NAME` →
  `voxel_browser`. Build-blocking bugs §1.1–1.3 fixed; §1.4 worked around.
  Decisions locked: sol2 (§19 Q1), doctest, LZ4, explicit source lists.

- **2026-09-10 — Phase 1.1/1.2/1.3 (partial)** (uncommitted). `vb_core`:
  `result.hpp` (`Result<T,E>`/`Status`), `error.hpp` (`CoreError`/`ProtocolError`
  + total `message()` switches), `math.hpp`, `ids.hpp`, `log.{hpp,cpp}`.
  `vb/protocol/`: `byte_buffer.hpp` (`ByteReader`/`ByteWriter` + LEB128 varints,
  error-accumulating), `message.hpp` (envelope, `MessageType`, `lane_for`),
  `handshake.hpp`+`handshake.cpp` (7 handshake message structs, encode/decode).
  18 unit test cases / 113 assertions, all green under `-Werror`. `docs/protocol.md`
  filled in. **Still open in Phase 1:** TOML config loader, GNS transport,
  connection FSMs, librg spike, client shell (camera/overlay).

- **2026-09-10 — Phase 1.2/1.3 transport + handshake** (uncommitted).
  `vb/net/`: `transport.hpp` (`Transport` interface, `TransportEvent`,
  `lane_for`/`send_mode_for_lane`, `send_message` helper), `loopback.{hpp,cpp}`
  (`LoopbackNetwork` in-process backend), `handshake.{hpp,cpp}` (`ServerHandshake`
  + `ClientHandshake` FSMs with host callback hooks). `NetError` enum added to
  `core/error.hpp`. `tests/unit/net_test.cpp` drives the whole handshake over
  loopback (26 cases / 185 assertions total, green under `-Werror`).
  **Next:** `GnsTransport` behind `VB_WITH_NET`, a `Session` object gluing
  Transport+FSM+tick loop into `voxel_browser_server`/`voxel_browser`, then the
  librg spike (1.4) and client shell (1.5).

### Gotchas learned this pass
- Heavy deps (GNS, librg, Lua/sol2, FastNoise2, LZ4/xxHash, Cellulose) are
  declared in `Dependencies.cmake` but **gated behind `VB_WITH_*` (default OFF)**.
  Phase 1 flips `VB_WITH_NET` / `VB_WITH_REPLICATION` on — expect the GNS build
  (protobuf + OpenSSL) to be the real work there, and the CI `apt`/`brew`/`choco`
  steps to need new packages then.
- `raylib` + `raygui` are only fetched when `VB_BUILD_CLIENT=ON`.
- raygui's implementation must be its own target (`vb_raygui_impl`) so project
  `-Werror` never touches it.
- `src/**` in the old lint job didn't recurse; lint now uses `find`. Every file
  under `src/` (incl. `.c`) must be clang-format-clean or CI fails.
- doctest + MSVC STL: comparing/streaming a `std::string_view` in a `CHECK`
  instantiates `toString<string_view>` which needs `<ostream>` complete —
  add `#include <ostream>` to any test TU that does this (see `core_test.cpp`).
- `Result<T,E>`: never call `.error()` when it holds a value (union UB). In
  tests use `REQUIRE(result)` then deref, not `REQUIRE_MESSAGE(result, msg(err))`.
- Sub-modules attach sources to `vb_core` via `target_sources()` from their own
  `src/<mod>/CMakeLists.txt`, added after `core` in `src/CMakeLists.txt`.
- Local dev on this Windows box: `clang`/`clang++` (LLVM 21) work; no `gcc`/`cl`
  on PATH in git-bash. `CC=clang CXX=clang++ cmake -G Ninja` configures fine.
  First configure is slow (~130s) — raylib + deep git clones.
