# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.

Last updated: 2026-09-16 (Phase 5.1 — content/base pack: registration content +
the load_content_pack wiring that was missing from both binaries; see §8's
second 2026-09-16 entry)

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
- `build_linux.yml` installs X11/GL dev packages for raylib, plus (since Phase
  1.2) `libssl-dev libprotobuf-dev protobuf-compiler` for `VB_WITH_NET`.
- `runner.yml` fires on push to `main` and `*-workflow` branches, plus PRs.
- **`VB_WITH_NET` CI coverage is uneven — check before assuming it's tested
  everywhere:** Linux (apt) and Windows (vcpkg, pre-installed on
  `windows-latest`) build it; **macOS does not** — see §5's GNS entry for why
  (single-arch Homebrew protobuf vs. the universal arm64+x86_64 build). If
  macOS ever gets it, `build_macos.yml`'s configure step is where to add it.
- **Never call `find_package(Protobuf REQUIRED)` a second time anywhere in the
  tree.** GameNetworkingSockets' own `src/CMakeLists.txt` already calls it;
  calling it again (even just as a "fail fast with a clearer message"
  convenience, which is what caused this) fatal-errors on some protobuf
  installs — hit on Homebrew's, not on vcpkg's — with "Some (but not all)
  targets in this export set were already defined." Fixed 2026-09-11 (§8) by
  deleting the redundant call; don't reintroduce it.
- **macOS `VB_WITH_NET=ON` builds and passes tests locally (confirmed
  2026-09-15)**, even though CI still doesn't build it there (see above —
  that's unchanged, still a universal-binary-vs-single-arch-Homebrew-protobuf
  problem, not a build-correctness one). A pre-existing local `build-net/`
  tree on this Mac (Homebrew protobuf + OpenSSL, real `GameNetworkingSockets`
  linked) configured, built `vb_core`/`vb_tests`/both binaries, and ran the
  full suite clean under `-Werror` while investigating the §8 chunk-gap bug.
  One test fails there —
  `GnsTransport: connect, exchange a message, and disconnect over real UDP`
  (`server.listen(0)` returns false) — but it reproduces identically on
  unmodified HEAD too; looks like a local sandbox/environment UDP-bind
  restriction (this agent's Bash tool runs sandboxed), not a real regression.
  Don't assume that failure means something's broken; do treat any *other*
  `VB_WITH_NET` test failure on this box as real.

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
- **`std::erase`/`std::remove` on a `std::vector<ChunkCoord>` (or any small
  trivially-copyable struct of that size/alignment shape) fails to compile**
  with this repo's toolchain (clang targeting the MSVC STL): `<xutility>`'s
  `_Find_vectorized`/`_Remove_vectorized` hit `static_assert(false, "unexpected
  size")`. Seen 2026-09-11 in world_replicator.cpp. Workaround: a manual
  `for (it...) if (*it == x) { v.erase(it); break; }` loop instead of
  `std::erase(v, x)`. Haven't checked whether this is specific to 12-byte
  structs, this exact clang/MSVC-STL version pairing, or something else —
  just avoid `std::erase`/`std::remove` on small POD-struct vectors here.
- **Local `clang-format --dry-run --Werror` on this Mac (brew's, v22.1.8) is
  not trustworthy as-is** — it flags dozens of violations even on files
  freshly checked out from `HEAD` with no edits at all (confirmed 2026-09-15:
  ran it against an untouched `git show HEAD:...` copy of
  `gns_transport.cpp` and got the same wall of "code should be
  clang-formatted" noise as the edited version). This repo's `.clang-format`
  was presumably validated against whatever version CI's `pip`/`pipx`
  installs (§3), which isn't pinned to match Homebrew's latest — the two
  disagree on enough column-wrap/brace decisions that a local diff full of
  violations doesn't mean *your* edit broke formatting. To actually check
  whether *your change* introduced a real violation: run it against the
  unmodified file first (`git show HEAD:path | clang-format --dry-run
  --Werror -`) and compare, don't trust a bare pass/fail on the edited file.
- **`doctest`'s `--test-case=` filter is a glob pattern, not a substring
  match** — `--test-case="GnsTransport: connect, exchange a message..."`
  (the literal, full test name) matches *zero* cases silently (`0 passed | N
  skipped`, no error) unless it's the exact full string with no drift at all;
  wrap it in `*...*` (e.g. `--test-case='*UDP*'`) to match by substring. Also
  quote it — zsh glob-expands an unquoted `*UDP*` itself before doctest ever
  sees it, and errors with "no matches found" if nothing in the CWD happens
  to match.

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
- **GameNetworkingSockets** — **resolved (Phase 1.2, 2026-09-11), was the
  single hardest dependency in the project.** Protobuf cannot be
  `FetchContent`-ed (its CMake config is only generated by an `install()`
  step, and the library doesn't exist yet at FetchContent's configure time) —
  it must be a real package-manager install: vcpkg (Windows), apt (Linux),
  brew (macOS, but not yet wired into CI — see §3). GNS pin is `v1.6.0`, not
  `v1.4.1` — the older tag doesn't compile under a modern stdlib
  (`std::string_view::c_str()`, not a real API). ICE/WebRTC disabled
  (`ENABLE_ICE=OFF`), which also lets `GIT_SUBMODULES` skip the huge `webrtc`
  submodule — only `abseil` + `vjson` are needed (note: v1.4.x used `picojson`
  for the same purpose; v1.6.0 renamed it to `vjson` — check `.gitmodules`
  again if the pin ever moves). Windows crypto is BCrypt (no OpenSSL needed
  there); Linux/macOS use system OpenSSL. Linked statically
  (`GameNetworkingSockets::static`) like everything else here. Full story,
  including the local-verification toolchain quirks and the Homebrew
  double-`find_package` crash, is in §8's Phase 1.2 entries — read those
  before touching `cmake/Dependencies.cmake`'s GNS block again.
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
- **TODO, not urgent:** `WorldReplicator` streams a whole player's view box in
  one uncapped burst (every `diff.entered` chunk queued in a single
  `broadcast_world()` call, no pacing across ticks) — see §8's 2026-09-15
  entry. Raising GNS's send buffer to 32 MiB fixes it for the *shipped
  default* `view_distance`/`vertical_view` (8/3, ~2023 chunks, ~545 KB
  measured), but doesn't add real backpressure: a larger view distance, a
  denser/less-compressible world, or several players joining at once sharing
  one connection's budget could still overflow it. Proper fix is a
  per-connection byte-budget-per-tick on the `diff.entered` send loop instead
  of relying on a bigger fixed buffer. Revisit before ever raising the
  shipped default view distance.

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

- **2026-09-15 — "Heap corruption after normal block" root-caused to the
  NVIDIA OpenGL driver itself, not this codebase (no code fix; investigation
  closed, uncommitted).** Reported by the user (Windows, `build-net`, MSVC
  Debug config, real `voxel_browser.exe`, NVIDIA GeForce RTX 5060 Laptop GPU,
  driver 32.0.15.9595 / 2026-03-15): Event Viewer showed `voxel_browser.exe`
  faulting in `ucrtbased.dll` with exception `0x80000003`
  (`STATUS_BREAKPOINT` — the Debug CRT's own heap-corruption detector
  hitting `DebugBreak()`), reported as happening after breaking a block.
  **Investigation path, each step confirmed by an actual before/after repro,
  not just code reading** (see `cmake/Sanitizers.cmake` below for the ASan
  build setup this all rode on):
  - Ruled out `VB_WITH_NET`/GNS: reproduces in plain `--singleplayer`
    (`GnsTransport` is never even constructed there).
  - Ruled out a data race in `ChunkMeshWorkerPool` *and* `WorldGenWorkerPool`:
    reproduces identically with **both** forced to `kSynchronous`
    (single-threaded) — a stack trace from that run showed the corruption
    detected inside `main()` itself, single-threaded top to bottom.
  - Ruled out block-editing as such: reproduces from ordinary chunk
    streaming/meshing on join alone, no block needs to be broken — the
    original "after breaking a block" was misleading; breaking blocks just
    triggers a lot of re-meshing (`bump_all_neighbor_revisions`/
    `bump_neighbour` cascades), which surfaces it faster, especially at a
    small `render_distance` where most/all loaded chunks border each other.
  - `mesh_chunk_from_snapshot`'s indexing and `model_from_mesh`'s raylib
    `Mesh` buffer sizes were checked byte-for-byte against the vendored
    raylib 5.5 source (`_deps/raylib-src/src/rmodels.c`) and are correct.
  - **Built a minimal isolated repro** (`--mesh-stress-test` diagnostic mode,
    temporarily added to `voxel_browser.exe` then removed once the finding
    was confirmed — see below to recreate it): a real window, a handful of
    directly-generated/relit chunks fed into a plain `ClientChunkStore` via
    the real `encode_chunk_payload`/`apply_add` wire path (no networking, no
    `WorldGenWorkerPool`, no `WorldReplicator`), then a tight loop of random
    `ClientChunkStore::edit_block()` calls + `ChunkRenderer::sync()`/`draw()`
    every iteration. **Reproduced the exact same ASan failure in under 2000
    iterations with as few as 3 chunks** — proving the bug needs zero
    networking/worldgen/lighting-cascade machinery and lives entirely in
    `ChunkRenderer` + raylib's GPU mesh upload/unload path. With a *single*
    chunk (repeatedly re-edited, re-meshed, re-uploaded 5000×) it never
    reproduced — the trigger is specifically **many distinct chunks'
    GPU meshes cycling through create/destroy**, not raw repetition.
  - **Instrumented `chunk_renderer.cpp` directly** (temporary, removed after
    diagnosis): recorded every uploaded `Mesh`'s buffer pointers
    (`vertices`/`texcoords`/`normals`/`colors`/`indices`) and re-validated
    them immediately before every subsequent `UnloadModel`, and separately
    asserted every field we never set (`tangents`/`texcoords2`/
    `animVertices`/`animNormals`/`boneIds`/`boneWeights`/`boneMatrices`/
    `boneCount`) stays null/zero at three points: right after `Mesh mesh{}`,
    right before `UploadMesh`, and right before `UnloadModel`. **None of
    these ever fired** — every pointer our own C++ code sets or expects to
    stay null was still exactly correct one call away from the crash.
  - **The actual crash, caught with this instrumentation active, was
    `AddressSanitizer: attempting free on address which was not malloc()-ed`
    with the stack trace entirely inside `nvoglv64.dll` (NVIDIA's OpenGL
    driver), on a driver-spawned thread (`T5`) — not `T0` (ours), not any
    thread this codebase creates.** Confirmed independent of `FLAG_MSAA_4X_HINT`
    (still reproduces with MSAA off).
  **Conclusion:** this is heap corruption originating *inside the NVIDIA
  OpenGL driver's own internal memory management*, triggered by rapid
  VAO/VBO create/destroy churn (`UnloadModel`+`UploadMesh` on many distinct
  chunks in quick succession — exactly what `ChunkRenderer::upload()`'s
  full-recreate-every-revision-change approach produces, especially at small
  `render_distance`s where border-neighbour re-mesh cascades touch most of
  the loaded set on almost every edit). ASan's global allocator interception
  catches the driver's own internal `free()` calls process-wide, which is
  how this became visible at all; the plain Debug-CRT crash the user
  originally saw is presumably the same underlying driver-side corruption,
  just caught later/differently by `ucrtbased.dll`'s heap validator instead.
  **This is not a bug in `voxel_browser`'s code** — exhaustive review (this
  session) and instrumentation found nothing wrong in
  `ChunkRenderer`/`model_from_mesh`/`mesh_chunk_from_snapshot`/
  `ClientChunkStore`/`LightEngine`, and the crash's own stack trace is
  entirely inside the vendor driver DLL.
  **Recommended for the user:** update the NVIDIA driver first (32.0.15.9595
  was current as of 2026-03-15 — check for anything newer) as the highest-
  leverage fix; this class of driver-internal VBO-lifecycle bug is the kind
  vendors do fix over time, and no code change here can guarantee avoiding
  it outright.
  **Follow-up hardening — implemented in a later session (2026-09-15,
  `src/render/chunk_renderer.cpp`):** `ChunkRenderer::upload()` used to do a
  full `UnloadModel`+`model_from_mesh`(fresh `UploadMesh`) on *every* revision
  change, for every affected chunk — real VAO/VBO churn on essentially every
  edit/re-mesh. It now over-allocates each chunk's GPU buffers with 25%
  headroom (`capacity_for()`), and a re-mesh whose new vertex/index count
  still fits reuses the existing VAO/VBOs in place via `UpdateMeshBuffer()`
  (`update_gpu_mesh()`) instead of recreating them; only a re-mesh that
  outgrows its current capacity still does a full unload/recreate. Confirmed
  by the user: the crash (this section's whole investigation) has not
  recurred since. Doesn't change the fact that occasional full recreation is
  still necessary (some re-meshes do outgrow capacity), so this reduces the
  odds of hitting the driver bug rather than eliminating the trigger
  outright — the driver-level root cause means no application-level change
  can be a guaranteed fix.
  **Toolchain note, needed for ANY future ASan work on this project on
  MSVC (kept, `cmake/Sanitizers.cmake`):** `VB_ENABLE_ASAN=ON` alone fails to
  link `voxel_browser`/`voxel_browser_server`/`vb_tests` against the
  (non-ASan-instrumented, FetchContent-built) `GameNetworkingSockets` static
  lib with `LNK2038` ("mismatch detected for 'annotate_vector'/
  'annotate_string'/'annotate_optional'"). Fixed by adding
  `_DISABLE_VECTOR_ANNOTATION`/`_DISABLE_STRING_ANNOTATION`/
  `_DISABLE_OPTIONAL_ANNOTATION` compile definitions to `vb_sanitizers` when
  `MSVC AND VB_ENABLE_ASAN` — trades away ASan's container-slack-overflow
  checks (`vec[vec.size()]` while still under `capacity()`) for the ability
  to link against non-instrumented static libs at all; every other ASan
  check (real heap-buffer-overflow, use-after-free, bad-free — everything
  this investigation actually used) is unaffected. Also: `clang_rt.asan_
  dynamic-x86_64.dll` (found under `VC\Tools\MSVC\<ver>\bin\Hostx64\x64\`)
  is not on `PATH` by default and must be copied next to the built `.exe`s
  or the ASan build won't even launch (`error while loading shared
  libraries`).
  **To reproduce the isolated repro again** (e.g. to verify a future driver
  update, or test the buffer-reuse mitigation above): the `--mesh-stress-test`
  diagnostic mode described above was removed from `main.cpp` after use, not
  committed — recreate it from this writeup if needed (real `Window`, a few
  `WorldGenerator`-generated + `LightEngine`-relit chunks fed through
  `encode_chunk_payload`/`ClientChunkStore::apply_add`, then a loop of random
  `edit_block()` + `ChunkRenderer::sync()`/`draw()` calls — reproduced with
  as few as 3 chunks and ~2000 iterations).

- **2026-09-15 — FPS dip while chunk-streaming/moving, fixed
  (`src/world/chunk_mesh_snapshot.cpp`).** Reported by the user after the
  driver-corruption crash above stopped reproducing: still a noticeable FPS
  dip specifically while chunks streamed in during movement, even with
  `ChunkMeshWorkerPool`'s background threads doing the actual meshing.
  Root cause: `build_chunk_mesh_snapshot()` — the per-chunk copy step that
  *must* run on the main thread (it's the only thread allowed to touch
  `ClientChunkStore`, see `chunk_mesh_snapshot.hpp`) — was calling
  `ClientChunkStore::block_at()`/`light_at()` per padded voxel. Each of those
  does a `ChunkCoord` → `Chunk*` unordered_map lookup from scratch, so one
  chunk's snapshot (`kMeshSnapshotVolume` ≈ 39k padded voxels) cost ~78k
  hashmap lookups, done synchronously on the main thread, up to
  `submit_budget` (8) chunks per frame — squarely on the frame already busy
  with movement/streaming. Fixed by resolving each of the (up to) 27
  neighbour chunks once per snapshot via a single `find()` call, then
  indexing straight into each chunk's block/light arrays (O(1)) for every
  voxel instead. Verified byte-identical snapshot output against the
  existing `mesh_chunk()` direct-path test
  (`mesher_test.cpp`'s "matches mesh_chunk"-style cases). User confirmed:
  "buttery smooth" after this fix, in both singleplayer and a real
  server+client multiplayer smoke test (two headless clients joined over
  real GNS/UDP, streamed chunks, and disconnected cleanly with no crash).

- **2026-09-15 — Cross-chunk sky-light bug fixed: false-bright band every 32
  blocks while mining (uncommitted).** Reported by the user: "as I mine
  deeper, on coordinate when vertical displacement is 32, the block suddenly
  becomes bright."
  Root cause: `LightEngine::relight_chunk` (`src/world/lighting.cpp`)
  computed sky light **per chunk in total isolation** — it always seeded full
  brightness at every non-opaque voxel in the chunk's own top layer (local
  y = 31), with no way to know whether another chunk was loaded directly
  above it blocking real sunlight. Both call sites relied on this in
  isolation: `ChunkLifecycleSystem` (on generation) and
  `WorldReplicator::apply_block_edit` (on edit) each called
  `relight_chunk(*chunk)` with no neighbour context at all.
  This generator has **no caves** (`WorldGenerator` fills solid below its
  heightmap unconditionally, `src/worldgen/generator.cpp`), so a chunk fully
  below the natural surface is 100% opaque stone including its own top
  layer — the isolation bug can't actually manifest from generation alone
  with this generator (transmittance is 0 there regardless of what's above).
  The real, common trigger is **mining**: breaking a block at the very top
  of a chunk (world y ≡ 31 mod 32) that's buried under another loaded, solid
  chunk creates an opening exactly where the old code assumed open sky —
  `WorldReplicator::apply_block_edit`'s call to a bare `relight_chunk`
  matches this exactly.
  **Fix:**
  - `LightEngine::relight_chunk(Chunk &chunk, const Chunk *above = nullptr)`
    — `above`'s bottom (local y = 0) sky-light row seeds this chunk's top
    layer instead of assuming open sky, attenuated the same way a normal
    interior BFS step is. `above == nullptr` (the default) keeps the exact
    old unattenuated-kMaxLight behaviour, so every existing direct caller/test
    is unaffected — only actually correct for the topmost chunk in a loaded
    column, which relight_column() (next) accounts for.
  - `relight_column(engine, coord, find, on_relit)` (`lighting.hpp`, header-
    only template): relights `coord` with its real neighbour above, then
    cascades the same relight downward through **every** consecutively
    loaded chunk below it, unconditionally, to the bottom of the loaded
    stack — bumping each chunk's revision iff its light output actually
    changed, and invoking `on_relit(coord, light_before, chunk_after)` once
    per chunk visited so callers can build protocol deltas / track
    newly-ready chunks without lighting.hpp knowing about the wire format.
    **Deliberately unconditional, not early-exit-on-"unchanged"**: a first
    attempt compared each chunk's light output to what it was immediately
    before that same call to decide whether to keep cascading, but a chunk
    that had never been lit before starts all-zero by construction, which is
    indistinguishable from "correctly recomputed to all-zero" (e.g. under a
    solid roof) — the early-exit stopped the cascade before reaching chunks
    that still needed it. Caught by a test that asserted the exact chunks
    visited (`relight_column cascades a solid roof's shadow down through
    every loaded chunk below it`, `lighting_test.cpp`) before trusting the
    logic. Loaded columns are shallow (a handful of chunks per the server's
    vertical view distance) so the extra unconditional relights are cheap.
  - `ChunkLifecycleSystem::update()`'s `ingest()` (`chunk_lifecycle.cpp`):
    now inserts every finished chunk from a batch into `world_` **first**,
    then cascades each via `relight_column` — not relight-then-insert like
    before. Matters because chunk generation order across worker threads has
    no relation to column position; the chunk below can easily finish before
    the chunk above exists yet, and inserting the whole batch before any
    cascading means a same-tick sibling is already visible to `find_chunk`
    when its turn comes.
  - `WorldReplicator::apply_block_edit` (`world_replicator.cpp`): uses
    `relight_column` instead of a bare `relight_chunk` call, and now builds +
    fans out **one delta per chunk the cascade actually touches** (the
    primary edited chunk always carries the block change; any chunk below it
    only carries light changes), each delta sent only to the players who
    mirror that *specific* chunk (not necessarily the same watcher set as
    the primary chunk).
  - `ClientChunkStore::edit_block` (`client_chunk_store.cpp`): same swap, for
    the client's local optimistic prediction. Needed a new non-const
    `Chunk *find(ChunkCoord)` overload (previously const-only) so
    `relight_column`'s lookup callback can hand back a mutable chunk for the
    cascade to relight in place.
  - **New, and probably the least obvious part of this fix:**
    `WorldReplicator` previously only noticed a chunk if it entered or left a
    player's view (`diff_chunk_sets` on presence alone) — never an
    already-visible chunk changing in place. A chunk generated and sent
    *before* its real neighbour above finished loading (arbitrarily many
    ticks later, since worker threads process the queue out of column
    order) gets its light corrected server-side by that neighbour's own
    cascade, but an already-connected player who received the wrong version
    would never see the correction otherwise. Added
    `last_sent_revision_: NetId -> ChunkCoord -> revision` and changed
    `tick()` to compare it against `world_`'s current revision for every
    *visible* chunk (not just newly-entered ones), re-sending a full
    `S2C_ChunkAdd` whenever they differ. `apply_block_edit`'s own delta path
    updates the same map so tick() doesn't redundantly re-send what an edit
    already just delta'd.
  **Scoped out, known follow-up:** no horizontal/diagonal propagation — if a
  cascaded chunk's new light affects a sideways neighbour's border AO, that
  neighbour isn't re-flagged here. Much smaller-magnitude than the
  whole-layer band this fixes; not attempted.
  **Tests:** `lighting_test.cpp` — `relight_chunk` with/without `above`
  (including "straight down through open air, no falloff" and "solid
  `above` stays dark, not falsely sky-lit"), `relight_column` cascade depth
  and the always-report-the-starting-chunk contract, using a small in-memory
  `FakeStore` test harness. `world_replication_test.cpp` — two end-to-end
  tests through the real `World`/`ChunkLifecycleSystem`/`WorldReplicator`
  stack: breaking a block at the top of a chunk buried under another solid,
  loaded chunk stays dark (exercises the real mining-trigger scenario, using
  the fact that any chunk with `top <= world y 31` is guaranteed 100% solid
  stone regardless of seed, since `WorldGenParams{}`'s default
  `base_height`/`amplitude` never puts the real surface below y=36); and a
  focused test of the new `last_sent_revision_` resend path (bump a
  loaded chunk's revision directly, confirm the next `tick()` re-sends it
  with no view change). All new tests confirmed to fail without their
  corresponding fix before being trusted (the flawed early-exit cascade
  design above was caught this way, not just theorized). Full `vb_tests`
  green on both `build/` and `build-net/` (`VB_WITH_NET=ON`, real GNS) — only
  the one pre-existing, unrelated sandbox UDP-bind failure noted elsewhere in
  this file.
  **Not yet done:** not visually verified against a live client (same
  limitation as the meshing-threading entry below — no GL context available
  here); rests on the test suite + code review.

- **2026-09-15 — Near-black flash on predicted block breaks fixed
  (uncommitted).** Reported by the user: "for a split second, I see a black
  block appear on the block to be destroyed before the cell becomes empty."
  Root cause: `ClientChunkStore::edit_block()` (`src/world/client_chunk_
  store.cpp`) only ever mutated the block volume, never the light volume.
  The voxel that just became air keeps whatever light value it had *while it
  was still solid* — typically 0, since a buried block was never reached by
  the sky-light flood. `mesh_chunk`/`mesh_chunk_from_snapshot` read exactly
  that stale value as the "outside" light for the newly-exposed neighbouring
  face (`light_at(outside)` in the mesher), so the face right where the
  block used to be renders at ~15% brightness (`base_light` floor from the
  `* 0.85 + 0.15` formula) — reads as a near-black block — until the server's
  authoritative `S2C_ChunkDelta` (which *does* carry a relit value; the
  server calls the same `relight_chunk` on every edit, per 5.2) arrives and
  overwrites it. This bug predates the threading work above and isn't caused
  by it, but the extra pool round-trip that change added between the edit
  landing and its mesh being collected likely made the flash last longer /
  more consistently land on an actually-rendered frame instead of being
  skipped over within the same tick.
  **Fix:** `edit_block()` now calls `LightEngine(registry_).relight_chunk(chunk)`
  immediately after a successful `chunk.set()`, mirroring what the server
  already does for the same edit. Because it's the exact same per-chunk
  algorithm run against the same (now-identical) block data, the predicted
  light typically matches the eventual authoritative delta exactly, not just
  "less stale" — no correction pop, not just a shorter flash. Doesn't change
  border-neighbour revision bumping (unaffected, still correct).
  **Known limitation, unchanged by this fix:** `relight_chunk` is
  per-chunk-only (assumes open sky directly above *this* chunk, no
  cross-chunk propagation) — same limitation the server's own relight has
  (`REMAINING_TASKS.md` 2.3/5.2 already track cross-chunk relight as
  separately TODO). An edit near a chunk boundary where the correct light
  actually depends on a neighbouring chunk's shadow can still show a brief
  mismatch until the server's delta corrects it; this fix only closes the
  common buried-block-with-no-cross-chunk-dependency case, which is what was
  reported.
  **Test** (`tests/unit/mesher_test.cpp`, "edit_block relights immediately"):
  a fully-solid, never-relit chunk (light stays at its all-zero default, the
  worst case) with one top-layer block broken — asserts the light there is
  full sky brightness (`kMaxLight`) immediately after `edit_block()`, not the
  stale 0 the bug would leave. Verified failing without the fix (the assert
  is exactly what the old code couldn't produce, since it never touched
  light at all). Full `vb_tests` green on both `build/` and `build-net/`
  (same one pre-existing sandbox UDP-bind failure noted above, unrelated).

- **2026-09-15 — Chunk meshing moved off the main thread (uncommitted)**,
  fixing framerate drops while chunks stream in. Reported by the user: the
  client stutters when receiving new chunks, suspected single-threaded
  meshing (`REMAINING_TASKS.md` 2.5 already flagged "mesh worker pool" as a
  follow-up).
  Root cause confirmed before touching anything: `ChunkRenderer::sync()`
  (`src/render/chunk_renderer.cpp`) ran `mesh_chunk()` (CPU face-culling + AO,
  the expensive part) and the GPU upload back-to-back on the main/render
  thread, budgeted to 8 chunks/frame (`src/client/main.cpp`) — a burst (join,
  teleport, or the still-unpaced `WorldReplicator` burst noted below) pays
  that full CPU cost synchronously across however many frames it takes to
  drain the budget.
  **Fix, split across new files, all in `vb/world` (not `vb_render`) so
  they're unit-testable without raylib/GL:**
  - `inc/vb/world/chunk_mesh_snapshot.hpp` + `.cpp`: `ChunkMeshSnapshot` — a
    copy of one chunk's voxels/light plus a **1-voxel border shell** from its
    neighbours (proved sufficient, not just assumed: `mesh_chunk`'s AO corner
    samples add at most one tangent-axis offset on top of the face normal's
    one-voxel offset, and normal/tangent axes are always distinct, so no
    sample ever reaches 2 voxels out on any single axis). `build_chunk_mesh_
    snapshot(store, coord)` copies it out (must run on the store's own
    thread); `mesh_chunk_from_snapshot(snapshot, registry)` is the actual
    face-culling/AO algorithm, moved here verbatim from the old
    `chunk_mesher.cpp`, now a pure function of the snapshot with zero store
    access — safe on any thread. `chunk_mesher.cpp`'s `mesh_chunk(store,
    coord)` is now a 2-line wrapper (`build` + `mesh_from_snapshot`), kept
    byte-identical in signature/behaviour so every existing `mesher_test.cpp`
    case (and this session's new "meshing from a snapshot matches meshing
    straight off the store" case, which diffs the two paths' `MeshData`
    directly) passes unchanged.
  - `inc/vb/world/chunk_mesh_worker_pool.hpp` + `.cpp`: `ChunkMeshWorkerPool`,
    deliberately mirroring `WorldGenWorkerPool`'s shape (mutex+CV queue,
    dedup-by-coord, `poll_completed()`, `kSynchronous` for tests) since that
    pattern was already established and tested in this codebase. One
    difference from `WorldGenWorkerPool`'s default thread count
    (`hw_concurrency - 1`): this pool defaults to **half** of
    `hardware_concurrency` — `--singleplayer` runs both pools in the same
    process, both bursts tend to correlate (both triggered by player
    movement), so an `hw-1` default here would oversubscribe every core
    ~2x on top of main/render/net. Also added `in_flight_or_queued(coord)` (not
    on `WorldGenWorkerPool`) so `ChunkRenderer` can skip building a snapshot
    for a chunk that's already got a job in flight, instead of wasting the
    copy only to have `submit()` reject it.
  - `ChunkRenderer::sync()` (`chunk_renderer.{hpp,cpp}`) restructured into
    submit/collect halves: drain `pool_.poll_completed()` and GPU-upload each
    result **unless the chunk's live revision has moved past the result's
    `revision`** (a neighbour or the chunk itself changed again after the
    snapshot was taken — discard silently, it self-heals: the coord is no
    longer in-flight once the stale result is polled, so the submit loop
    below requeues it against the current revision next call); then submit
    up to `submit_budget` (still 8, same call site in `main.cpp`, unchanged
    signature) new/changed chunks, skipping any `in_flight_or_queued`. GPU
    upload itself (`model_from_mesh` — `MemAlloc` + `UploadMesh`) is
    unavoidably still main-thread (raylib/GL requirement) but is the cheap
    half of the old per-chunk cost, not the one causing the stutter.
  **Why a snapshot copy and not a live store reference on the worker
  thread:** `ClientChunkStore` is mutated by `apply_add`/`apply_delta`/
  `apply_remove` as net messages decode, on the same thread that would need
  to hand work to the pool — handing a worker a live `ClientChunkStore&`
  while that continues is a data race. The padded-shell copy
  (34×34×34 = 39,304 `block_at`/`light_at` calls, once, on the main thread)
  is comparatively cheap: the *old* `mesh_chunk` already made far more such
  calls per chunk internally (up to 78 world-space lookups per solid voxel,
  each going through `ClientChunkStore`'s coordinate→chunk hashmap), so the
  snapshot's dense single pass is a net win even before counting that the
  actual AO/culling work moves off-thread entirely.
  **Tests** (`tests/unit/mesher_test.cpp`): snapshot-vs-live-store equality,
  unloaded-chunk snapshot behaviour, worker-pool dedup (see the note in that
  test about why it uses a full solid chunk as the target job rather than a
  single block — a trivial job races the OS waking the worker thread within
  the same handful of nanoseconds as the immediate duplicate-submit call on
  the main thread; this was caught as an actual flaky failure while writing
  the test, not theorized), `kSynchronous` mode, and distinct-coord
  concurrent drain. Full `vb_tests` green on both `build/` (`VB_WITH_NET=OFF`)
  and `build-net/` (`VB_WITH_NET=ON`, real GNS) — the only failure on
  `build-net` is the pre-existing sandbox UDP-bind one noted below, confirmed
  unrelated (same failure, same test, on unmodified HEAD).
  **Not yet done / worth knowing before touching this again:**
  - **Not visually verified against the actual reported stutter** — no
    before/after frame-time capture exists yet (`ChunkRenderer` isn't
    unit-testable without a real GL context, so this rests on the reasoning
    above + the passing test suite, not an eyeballed FPS counter). If the
    stutter persists after this, the next suspect is GPU upload cost itself
    (`model_from_mesh`'s `MemAlloc`/`UploadMesh` calls, still synchronous and
    unbudgeted per collected result each frame) or `submit_budget=8` still
    being too high for the per-snapshot copy cost on a slower machine — try
    capping collected uploads per frame too, not just submissions.
  - Doesn't touch the unpaced `WorldReplicator` burst noted below — that's
    server→client wire volume, orthogonal to this (client-side CPU meshing).
  - Cellulose's `greedy_mesh` (`VB_WITH_MESHING`, still not wired) is meant to
    drop into exactly this seam (`mesh_chunk_from_snapshot`'s call site
    inside `ChunkMeshWorkerPool::mesh`) — this change doesn't block that,
    it's the same shape.

- **2026-09-15 — ACTUAL root cause of the invisible-but-walkable chunk gap
  found and fixed: GNS's default 512 KiB send buffer, silently overflowed.**
  Follow-up report after 2026-09-11's investigation below: "the gap is still
  there" + new evidence — **the invisible blocks can't be broken either**.
  That second fact was the key: `raycast_voxel()` (client `main.cpp`) queries
  `ClientChunkStore::solid_at()`, which is a *pure local read* — it never
  touches the mesher. If breaking also fails, the client's own replicated
  chunk data is missing/wrong at that spot, not just its mesh — ruling out
  every mesh/AO-only theory from the 09-11 pass in one shot and pointing
  straight at chunk streaming (encode → send → receive → decode) instead of
  meshing.
  Reread every step of that pipeline; `WorldReplicator::tick()`,
  `chunk_codec.cpp`'s encode/decode, and `ClientChunkStore::apply_add` all
  checked out (and 09-11 already added loud `VB_ERROR` logging to the decode/
  apply and the `find_chunk()`-returned-null paths — if either fired, the
  report would have said so). That left the one hop with **zero error
  handling at all**: `GnsTransport::send()` (`src/net/gns_transport.cpp`)
  called `SendMessageToConnection(...)` and discarded its return value
  outright — not even captured into a variable. GNS's own header
  (`steamnetworkingtypes.h`) documents exactly this failure mode:
  `k_ESteamNetworkingConfig_SendBufferSize` (upper limit of buffered pending
  bytes) **defaults to 512 KiB (524288 bytes)**, and once hit,
  `SendMessageToConnection` returns `k_EResultLimitExceeded` **instead of
  queuing the message** — reliable-lane or not, a message GNS never accepted
  is never retried by GNS's own reliability machinery, since that machinery
  only covers messages it already has.
  **Measured, not assumed:** wrote a throwaway probe (real `WorldGenerator`,
  seed 1, `encode_chunk_payload` over the *actual default production view
  box* — `view_distance=8`/`vertical_view=3` from `server.toml.example`, i.e.
  chunks_in_view = 17×17×7 = **2023 chunks**, matching the original report's
  "~1811 chunks loaded" order of magnitude) and summed the encoded chunk
  payload bytes: **545,300 bytes — already past the 524,288-byte default
  limit from chunk data alone**, before the per-message protocol framing
  overhead (`frame_message`'s envelope/length-prefix) or any entity-snapshot
  traffic sharing the same connection is even counted. The whole box streams
  in one synchronous `WorldReplicator::tick()` → `ServerSession::
  broadcast_world()` → `send_frames()` burst on join (nothing paces it across
  ticks), so the buffer fills almost immediately after join, mid-burst.
  Whichever chunks fall after the fill point in `chunks_in_view`'s iteration
  order (`for dy { for dz { for dx {...} } }`, y outermost) land as a
  **contiguous rectangular run of coordinates** — exactly "a big rectangular
  region" — and are silently never queued. `WorldReplicator::last_sent_`
  already recorded them as sent (it has no way to know `send()` failed), so
  they're never retried; the server's authoritative `World` has them
  (walkable, matches physics reading `World` directly); the client's
  `ClientChunkStore` never received them (invisible *and* unbreakable, matches
  both raycast and mesher reading the same missing data); deterministic
  per-session because the same view box streams in the same order every join
  (explains "persists across relog" *and* "a fresh client reproduces it too"
  from 09-11 — every fresh join hits the identical buffer-fill point).
  Also explains why no test caught it: every existing chunk-streaming test
  uses `LoopbackNetwork` (in-process `std::vector` queue, no byte-budget
  concept at all) — this is a `GnsTransport`-only failure mode, and
  `VB_WITH_NET` real-network coverage is exactly the thin/uneven part of the
  test suite noted in §3.
  **Fix** (`src/net/gns_transport.cpp`): (1) raise
  `k_ESteamNetworkingConfig_SendBufferSize` to 32 MiB via
  `SteamNetworkingUtils()->SetGlobalConfigValueInt32(...)` once in
  `GnsRuntime::acquire()`, process-wide, before any listen/connect — gives a
  full default view box (and meaningfully larger ones) comfortable headroom;
  (2) `GnsTransport::send()` now captures `SendMessageToConnection`'s
  `EResult` and `VB_ERROR`-logs a failure instead of discarding it, so if a
  future world/view-distance config *does* exceed even the raised buffer,
  it's loud instead of silent, consistent with 09-11's decode/apply logging.
  **Not done, worth doing if view distances grow much further:** this raises
  the ceiling, it doesn't add real backpressure — an unbounded view distance
  or a much larger world could still overflow even 32 MiB in one synchronous
  burst. A proper fix would pace `WorldReplicator`'s initial `diff.entered`
  burst across multiple ticks (a per-connection byte budget per tick) instead
  of relying on a bigger fixed buffer; not done here since the measured
  numbers show the raised buffer comfortably covers the shipped default
  config with a large margin.
  **Verification:** full `vb_tests` suite green on both `build/`
  (`VB_WITH_NET=OFF`, 125/125 cases) and `build-net/`
  (`VB_WITH_NET=ON`, real GNS linked, 124/125 — the 1 failure,
  `GnsTransport: connect, exchange a message, and disconnect over real UDP`,
  is a pre-existing local-sandbox UDP-bind limitation, confirmed by
  reproducing it identically on unmodified HEAD before this fix). Smoke tests
  (`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass on both configs.
  **Not yet verified against a live repro** — the original report was a real
  dedicated server + separate client session; this fix rests on the measured
  byte-count + GNS's documented buffer-overflow behaviour, not a captured
  `SendMessageToConnection failed (result=...)` log line from an actual
  overflow. If the gap somehow recurs after this, the new logging in
  `GnsTransport::send()` will name the exact `EResult` and message size —
  check the server's stderr around join time first.

- **2026-09-11 — investigated "big rectangular gap, doesn't render but I can
  walk on it" (dedicated server + separate client, ~1811 chunks loaded,
  persists across relog).** This turned out to be an incomplete fix — see the
  2026-09-15 entry above for the actual root cause found later. What I
  confirmed from the report + code at the time:
  - Walkable ⇒ the *server's* authoritative `World` genuinely has solid block
    data there (physics reads directly from it). Persists across a full
    client restart ⇒ it's not a client-side stale-mesh-cache issue (a fresh
    `ClientChunkStore`/`ChunkRenderer` reproduces it too). So the bug is
    either (a) something server-side deterministically not sending/encoding
    that chunk's data correctly for this client, or (b) `mesh_chunk`
    deterministically producing an empty/wrong mesh from otherwise-correct
    data.
  - Ruled out empirically, not just by inspection: the `kMaxMeshVertices`
    65532 cap added for the water-culling fix (chunk_mesher.cpp) was the
    prime suspect, but a probe across 40 seeds of real `WorldGenerator`
    terrain (base_height=64, amplitude=28, sea level 62, 3×3×5 chunk
    neighbourhoods so `mesh_chunk` has full context) found a worst case of
    only ~6260 vertices — nowhere near the cap. Smooth heightmap terrain
    structurally can't produce enough exposed faces per chunk to hit it.
  - Ruled out by code reading: `ChunkLifecycleSystem::update()`'s `ingest()`
    silently discards a `pool_.poll_completed()` chunk if it's no longer in
    `wanted` (player moved away between request and completion) — a real
    wasted-work bug, but it can't produce a *permanent* gap for a chunk the
    player is currently standing on/in view of, since the very next
    `update()` call re-requests it (not walkable if the data were truly
    missing anyway — see above). Left unfixed; not the reported symptom.
  - Ruled out by code reading: `WorldReplicator::forget_player()` is called
    on leave (session.cpp), so `last_sent_` doesn't go stale across a
    reconnect with a reused `NetId`. `kWorld` lane (chunk add/delta/remove)
    is `kReliableOrdered`, so a plain dropped UDP packet can't be the cause
    either — GNS retransmits.
  - Ruled out by code reading: `PalettedChunkStore`/`chunk_codec.cpp`
    round-trip is self-consistent by construction — `get()` can only ever
    return a value present in `palette()`, so the encoder's `index_in()`
    can't silently mis-encode a block as palette index 0. No corruption path
    found there.
  - **Found and fixed (real bug, but unconfirmed as THE cause):**
    `ClientChunkStore::bump_all_neighbor_revisions()` only bumped the 6
    face-adjacent chunks. AO samples 3 voxels diagonally around each face
    corner (chunk_mesher.cpp's `a`/`bpt`/`d`), which for a voxel on a chunk's
    edge/corner lands in an edge- or corner-adjacent chunk — one of the other
    20 in the 26-neighbourhood. Those chunks arriving/changing/leaving never
    triggered a re-mesh, leaving AO permanently stale at chunk
    edges/corners. Fixed by bumping the full 26-neighbourhood instead of just
    the 6 faces. This only affects per-vertex *lighting*, not face culling —
    it cannot by itself make a chunk render as fully empty, so it's very
    unlikely to be the reported bug, but it's a real fix regardless
    (regression test: "apply_add bumps a diagonally-adjacent neighbour's
    revision too" in mesher_test.cpp, verified to fail without the fix).
  - **Found and fixed (real code-quality bug, most promising lead for next
    repro):** `ClientSession::apply_gameplay_frame()` discarded the
    `Result<void, ProtocolError>` from `chunks_.apply_add()`/`apply_delta()`
    with `(void)` — any decode/apply failure was completely silent. If a
    chunk *does* ever fail to decode/apply for some data-dependent reason I
    haven't found, this is exactly how it'd manifest: never rendered (mesher
    sees it as unloaded), never retried (server's `last_sent_` already marks
    it sent), zero trace. Now logs via `VB_ERROR("net", ...)` with the coord
    and the `ProtocolError` message on both a malformed frame and a rejected
    apply. **If this bug recurs, check the client's stderr/log output for a
    `chunk (...) add rejected: ...` or `malformed S2C_ChunkAdd: ...` line
    around the time/place it happens** — that will point straight at the
    actual cause (or rule out this whole class of failure if nothing logs,
    which would point back toward the server not sending it at all — check
    `WorldReplicator::tick()`'s per-player diff.entered / `world_.find_chunk`
    lookup next).
  - **Also found and fixed the same session** (the "not yet checked" item
    below was stale — this was actually done here too, just not called out
    in this writeup at the time): `WorldReplicator::tick()`'s `diff.entered`
    loop silently `continue`d if `world_.find_chunk(c)` returned null for a
    chunk `visible` said was loaded moments earlier — "should be unreachable
    single-threaded" but was silent and would wedge the coord into
    `last_sent_` as sent when it wasn't. Now `VB_ERROR`-logs and drops it
    from `visible` so it retries next tick. (Neither this nor the client-side
    logging above ever fired on the 2026-09-15 repro — see that entry above
    for what the actual cause was: a transport-layer silent failure neither
    of these two code paths could see, since the message never even reached
    `find_chunk`/decode on either side.)

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
- **2026-09-10 — Phase 1 session layer + integrated singleplayer** (uncommitted).
  `vb/net/session.{hpp,cpp}`: `ServerSession` (per-conn handshake driver,
  timeouts, monotonic net-id alloc, `take_joins`/`take_leaves`), `ClientSession`.
  `vb/net/integrated.{hpp,cpp}`: `IntegratedGame` — loopback server+client in one
  object. `voxel_browser --singleplayer` runs it and completes the join
  (headless too); `singleplayer_smoke` CTest. `ServerHandshake::grant()` exposes
  the `JoinGrant`. `IntegratedGame` needs ~4 `tick()`s to settle (server-polls-
  then-client-polls each tick = one message hop per tick).
  **Next:** `GnsTransport` behind `VB_WITH_NET`.

- **2026-09-11 — Fixed spawn Y: was embedded in terrain by construction for
  ~half of all seeds** (uncommitted). Follow-up report after the fall-through
  fix below: "the player stuck inside blocks on spawn" -- **no falling
  involved this time**, a separate bug. Root cause: `JoinGrant::spawn_pos`
  defaults to a fixed `{0, 64, 0}` (`inc/vb/net/handshake.hpp`), and neither
  `--singleplayer` nor the dedicated server ever overrode it with a real
  terrain height -- singleplayer's hardcoded seed 7 happens to have
  `surface_height(0,0) == 56` (safely below 64, purely by luck), but the
  standalone server now picks a **random** seed by default
  (`server/main.cpp`'s `random_seed()`), and `base_height=64` /
  `amplitude=28` means the real surface ranges roughly `[36, 92]` — probed 30
  seeds, **19 of 30 (63%)** had `surface_height(0,0) >= 64`, i.e. the fixed
  spawn point was at or below ground. Seed 1 is dramatic: surface height 78,
  fourteen blocks above where the player would spawn — fully entombed in
  stone from the instant they joined, zero fall.
  Fixed by adding `worldgen::default_spawn_position(generator, x, z)`
  (`vb/worldgen/generator.{hpp,cpp}`) — feet one voxel above the real
  `surface_height` at the spawn column — and wiring it into a
  `HandshakeServerHost::on_ready` in both `--singleplayer` (`sp_server_host()`
  in client `main.cpp`) and the dedicated server (`server/main.cpp`), neither
  of which supplied a custom host before. `ServerSession`'s constructor
  already fills in `net_id`/`world_seed` on top of whatever `on_ready`
  returns (existing wrapper logic, unchanged) — the host only needs to set
  `spawn_pos`. Tests: `tests/unit/worldgen_test.cpp` — asserts the computed
  spawn sits exactly one voxel above `surface_height` for 3 seeds (including
  1, the dramatic case) and for a non-origin spawn column.
  **This is a different bug from the one below and doesn't supersede it** —
  a correct spawn *point* still needs the *chunk at that point* to be loaded
  before physics starts touching it; both fixes matter together. Not
  discovered together because singleplayer's fixed seed 7 masked this one
  during all of Phase 3-5's development and testing.

- **2026-09-11 — Join-time fall-through-world / embedding bug fixed**
  (`69ea41c`). Reported: "when the player joins, the player immediately
  falls outside the world first, which leads to player get embedded inside
  the terrain." Root cause: physics is entirely input-driven
  (`ServerSession::handle_input_batch` only runs `step_movement` when a
  `C2SInputBatch` arrives) and starts the instant the client sends its first
  input, which can easily be *before* the spawn chunk's async worldgen has
  finished (`WorldGenWorkerPool` runs on real background threads outside
  `kSynchronous` mode — `--singleplayer`'s pool included). An unloaded chunk's
  `solid_at`/`has_chunk` reads as plain air, so the player free-falls with
  **zero collision** for however many ticks generation takes; swept collision
  only ever prevents *new* penetration during a move, it never resolves a
  pre-existing one, so once the chunk finally loads with the player's Y
  already below the real surface, they're just stuck inside solid terrain —
  nothing ever pushes them back out.
  Fix: `physics::ground_area_loaded(feet, has_chunk)` (new, header-only
  template in `movement.hpp` so it works against both `World::has_chunk` and
  `ClientChunkStore::has` without a shared interface) checks the player's own
  chunk plus two below it; `ServerSession::handle_input_batch` and
  `ClientSession::push_input` both skip `step_movement` entirely (freezing
  position/velocity, not just zeroing gravity) whenever it's false. The
  server's freeze is what actually matters for correctness (it's
  authoritative); the client-side one is belt-and-suspenders so the local
  camera doesn't show a premature fall before the first snapshot arrives —
  verified this doesn't fight `reconcile()` (unguarded on purpose: replaying
  unacked inputs against *current*, likely-by-then-loaded chunk data is
  correct) by working through why `netcode_test.cpp`'s fly-mode tests (no
  `WorldReplicator` attached at all, so `chunks_` never receives any chunks)
  still pass: `reconcile()` snaps `predicted_` straight to the server's
  authoritative position every tick regardless of what `push_input` did
  locally, so the client-side freeze only ever affects the sub-tick gap
  before the next snapshot, never overall convergence.
  Tests: `tests/unit/physics_test.cpp` — 5 direct cases against
  `ground_area_loaded()` (nothing loaded, own chunk only, own+2 below, one
  gap in the middle, negative-Y `floor_div` correctness).
  **Known test-coverage gap, stated plainly:** there is *no* automated
  integration test reproducing the actual async join race end-to-end.
  `WorldGenWorkerPool::kSynchronous` (used by most existing integration
  tests) generates instantly inside `submit()`, so it structurally cannot
  exhibit the gap this bug lived in; the real async pool's timing isn't
  controllable enough for a deterministic CI test without adding test-only
  hooks, which felt like over-engineering for this fix. Confidence rests on
  the unit-tested decision function + code-inspection of the two call sites,
  not an end-to-end repro — say so if a future change to the join sequence
  needs that confirmed differently.

- **2026-09-11 — Chunk-neighbour remesh bug fixed** (`c3393be`). Reported by
  the user: "very minor AO error, perhaps arises from receiving data from the
  server" + "square chunks visible under water." Root cause:
  `ChunkRenderer::sync()` only re-`mesh_chunk()`s a chunk when
  `chunk->revision()` changes (chunk_renderer.cpp) — `mesh_chunk()` itself is
  a pure function of current store state and was always correct, but **three
  of the four `ClientChunkStore` mutators never bumped a *neighbour's*
  revision**, only `edit_block()` (the client's own optimistic edits) did:
  - `apply_add` — a chunk arriving after an already-loaded neighbour left
    that neighbour's border permanently meshed as if this chunk were still
    unloaded (air) — wrong culling *and* wrong AO along that seam, forever,
    unless something else happened to touch the neighbour later. Order-
    dependent on server streaming order — explains the "square chunks" report
    exactly (water chunks are flat/homogeneous, so an unculled border face is
    a very visible full 32×32 quad) and is entirely about *timing of data
    arriving from the server*, matching the other half of the report too.
  - `apply_delta` — the authoritative block-edit broadcast (anyone's edit,
    not just yours) never bumped the neighbour whose culling/AO depends on
    a border voxel it just changed.
  - `apply_remove` — a chunk unloading never told the neighbour that had been
    culling a face against it to re-expose that face.
  Fixed by extracting `bump_all_neighbor_revisions(coord)` (bump all 6
  face-adjacent loaded neighbours, unconditionally) and calling it from all
  three; `edit_block()` keeps its own tighter border-only variant since it
  only ever touches one voxel and knows exactly which single neighbour (if
  any) is affected. Regression tests in `tests/unit/mesher_test.cpp` assert
  the neighbour's revision actually changes (not just that `mesh_chunk()`'s
  output would be correct if called again — the bug was entirely about
  *whether* it gets called again) — verified they fail without the fix
  (reverted the source, reran, confirmed all 3 fail with `1 > 1`) before
  trusting them.
  **Lesson for next time a chunk-border bug shows up:** any code path that
  changes what a chunk looks like from the outside (new data, an edit
  anywhere in it, unloading) needs to bump *every loaded neighbour's*
  revision, not just its own — the renderer has no other signal to re-mesh a
  chunk whose own data didn't change but whose correct mesh output did.

- **2026-09-11 — macOS Homebrew protobuf configure crash fixed** (`f86aeec`).
  Reported by the user building `-DVB_WITH_NET=ON` locally on macOS with
  `brew install protobuf`:
  ```
  CMake Error at .../protobuf-targets.cmake:42 (message):
    Some (but not all) targets in this export set were already defined.
    Targets Defined: protobuf::libprotobuf-lite, protobuf::libprotobuf, ...
    Targets not yet defined: protobuf::libupb, protobuf::protoc-gen-upb, ...
  ```
  Root cause: `cmake/Dependencies.cmake` called `find_package(Protobuf
  REQUIRED)` itself (added purely so a missing protobuf would fail with a
  clearer message) *and then* `FetchContent_MakeAvailable(gamenetworkingsockets)`
  triggered GNS's own `src/CMakeLists.txt:12 find_package(Protobuf REQUIRED)`
  — two `find_package(Protobuf)` calls in one configure run. Homebrew's
  generated `protobuf-targets.cmake` has a fatal-error guard against exactly
  this (the two calls end up requesting slightly different component sets, so
  the second sees a partially-already-defined target list and refuses rather
  than silently re-defining). vcpkg's protobuf on Windows didn't trip the same
  guard, which is why this wasn't caught during Phase 1.2's own local
  verification (Windows-only, see below) — **a dependency behaving on one
  platform's package manager doesn't mean it behaves on another's; the
  `VB_WITH_NET` local verification story only ever covered Windows before this
  bug report.** Fix: deleted our own `find_package(Protobuf REQUIRED)` call
  entirely; GNS's internal one is now the sole call and already produces a
  clear "Could NOT find Protobuf" error on its own if it's missing, so nothing
  was lost. **Do not add `find_package(Protobuf ...)` back to
  `Dependencies.cmake`** — see §3 and §5 above.
  Verified only that the *configure* step now succeeds on Windows with the fix
  in place (reconfigured with the vcpkg toolchain file, same as Phase 1.2's
  original verification) — the macOS report itself is unconfirmed-fixed
  pending the user re-running it; if it still fails on macOS after this, the
  next suspect is the OpenSSL linkage (`brew install openssl` — macOS uses
  GNS's default OpenSSL crypto backend, not BCrypt), not protobuf.

- **2026-09-11 — Phase 1.2: `GnsTransport`, real UDP networking** (`cc2241a`).
  `vb/net/gns_transport.{hpp,cpp}` — a real `Transport` backend over
  GameNetworkingSockets, same "always-present header, `kBackendUnavailable`
  stub without the flag" pattern as `vb::script::Vm`. One process-wide
  `GnsRuntime` (refcounted `GameNetworkingSockets_Init`/`_Kill`; GNS exposes
  exactly ONE global connection-status callback for the whole process, not
  one per interface) routes each status-change event to the owning
  `GnsTransport` via listen-socket/connection-handle registries — needed so a
  server + several clients can share one process (tests) without cross-talk.
  `voxel_browser_server` now actually runs a game (`World` +
  `WorldGenWorkerPool` + `ServerSession` + `WorldReplicator` over a real
  listen socket) instead of an empty sleep loop. `voxel_browser`'s
  non-singleplayer path connects for real (`RemoteConnection` in `main.cpp`);
  `main.cpp` was refactored to drive singleplayer and remote play through one
  `ClientSession*` instead of duplicating the per-frame logic.
  `tests/unit/gns_transport_test.cpp`: real UDP connect/send/disconnect over
  127.0.0.1 (`#if VB_WITH_NET`, mirrors the `vb::script::Vm` test-gating style).

  **This was the single hardest dependency in the project so far — details
  matter if it needs touching again:**
  - **Protobuf cannot be `FetchContent`-ed for this.** GameNetworkingSockets'
    CMakeLists does a bare `find_package(Protobuf REQUIRED)`. Protobuf's own
    CMake only *generates* a discoverable config (`protobuf-config.cmake`) as
    part of an `install()` step, and its library doesn't exist yet at
    FetchContent's configure time either way — there is no clean way to
    satisfy `find_package(Protobuf)` from a bare source checkout. Protobuf
    must come from a real package manager: **vcpkg on Windows**
    (`vcpkg install protobuf:x64-windows` +
    `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` — this
    box now has vcpkg cloned+bootstrapped at `D:\vcpkg`), **apt on Linux**
    (`libprotobuf-dev protobuf-compiler`), **brew on macOS** (`protobuf`).
    `cmake/Dependencies.cmake` has the full write-up.
  - **sol2's pattern does not repeat here**: unlike sol2 (bump the pin, done),
    this is a "the dependency's own build system requires a pre-installed
    sub-dependency" problem — no version bump fixes it.
  - **GNS pin bumped v1.4.1 → v1.6.0**: v1.4.1 doesn't compile at all under a
    strict/modern stdlib — `std::string_view::c_str()` (not a real API) in
    `csteamnetworkingsockets.cpp` and two other files. Fixed upstream by v1.6.0.
  - **ICE/WebRTC disabled** (`ENABLE_ICE=OFF`, `USE_STEAMWEBRTC=OFF`) — we only
    ever dial a known dedicated-server address, never P2P/NAT-punched. Also
    restricted `GIT_SUBMODULES` on the FetchContent_Declare to skip the
    multi-hundred-MB `webrtc` submodule (**note**: v1.6.0 renamed the other
    required submodule from `picojson` to `vjson` — check `.gitmodules` again
    if the pin moves).
  - **Crypto backend**: `USE_CRYPTO=BCrypt` on Windows (built into the OS, no
    extra dependency); Linux/macOS use GNS's default, system OpenSSL
    (`libssl-dev` / `brew install openssl`).
  - **Link statically** (`GameNetworkingSockets::static`, `BUILD_SHARED_LIB
    OFF`) — every other dependency here is static/header-only; a shared GNS
    lib would need copying next to the .exe on Windows (no RPATH equivalent).
  - **GNS's public headers need the SYSTEM-include treatment**: same fix as
    doctest/raygui/toml++ (`get_target_property(... INTERFACE_INCLUDE_DIRECTORIES)`
    + re-add `SYSTEM`) — its headers trip `-Wold-style-cast`/`-Wsign-conversion`/
    `-Wlanguage-extension-token` under our `-Werror` set. Done in
    `src/core/CMakeLists.txt`.
  - **macOS CI does NOT build `VB_WITH_NET` yet** — it's a universal
    (arm64+x86_64) build, and a brew-installed protobuf is single-arch, which
    breaks linking the slice that doesn't match the runner's host arch. Needs
    a universal protobuf (vcpkg triplet, or building protobuf from source for
    both arches) before enabling.
  - **`connect()` only accepts numeric IP literals** — `SteamNetworkingIPAddr::
    ParseString()` doesn't resolve DNS. "localhost" / hostnames need
    `getaddrinfo` added to `GnsTransport::connect`; not done.
  - **Local-box-only linker quirk (not a CI concern)**: plain `clang++.exe`
    (GNU driver) targeting the MSVC ABI makes CMake's `MSVC` variable true, so
    GNS's *vendored* abseil submodule (still built even with ICE off — some
    non-ICE code apparently uses it) applies MSVC-style linker flags
    (`-ignore:4221`) that only a Microsoft-syntax-aware driver understands.
    Plain `clang++` rejects it ("unknown argument"); **`clang-cl.exe`** (same
    LLVM install, MSVC-compatible driver) accepts it fine. Real CI is
    unaffected — Windows CI uses actual `cl.exe` via `msvc-dev-cmd`,
    Linux/macOS CI never sets `MSVC` true. Local verification builds on this
    box needing `VB_WITH_NET` should use `clang-cl`/`clang-cl++`, not
    `clang`/`clang++`.
  - Full local validation trail: GNS built standalone under both plain
    `clang++` (compiles; only the unrelated abseil link flag fails) and is
    expected clean under `clang-cl` (build was in flight when this was
    written — check `voxel_browser`/`voxel_browser_server`/`vb_tests` actually
    linked before trusting this note blindly next time).
  - **Smoke-test regression caught and fixed**: `server_smoke`/`client_smoke`
    (default build, `VB_WITH_NET` off) broke the moment the binaries started
    actually calling `listen()`/`connect()` instead of being no-ops. Fixed by
    (a) making the server treat `kBackendUnavailable` specifically as
    non-fatal (see above) and (b) flipping `client_smoke`'s CTest expectation
    — with no `--singleplayer`/`--server` override it now legitimately tries
    to reach `127.0.0.1:27015` with nothing listening, so the smoke test
    became "fails gracefully with a clear message," not "succeeds";
    `PASS_REGULAR_EXPRESSION` alone (not `WILL_FAIL` — the two combine by
    inverting an already-passing regex match into a reported failure, don't
    stack them) asserts that. Lesson: wiring a previously-inert code path for
    real changes what "the binary just starts up" tests actually exercise —
    re-check smoke tests whenever a stub becomes real.

- **2026-09-11 — Entity billboard-sprite placeholder implemented** (Phase 3.5,
  `7f64581`). `inc/vb/render/entity_visual.hpp` (header-only, no raylib,
  unit-tested like `camera.hpp`): `resolve_anim_clip` (priority state machine),
  `bearing_degrees`/`direction_bucket`/`select_pose` (direction math, mirroring
  for the far half of the sectors), `DirectionBucketTracker` (flicker
  hysteresis), `EntityPresentationState` (per-entity orchestration + clip-time
  tracking). `vb/render/entity_renderer.{hpp,cpp}`: raylib-backed, pImpl (no
  raylib types in the header, matching `ChunkRenderer`); one `DrawBillboardPro`
  per tracked entity against a 1×1 white texture tinted by a deterministic
  `NetId` hash (stand-in art, so simultaneous entities are at least visually
  distinguishable). Wired into `main.cpp` next to `chunk_renderer`, same
  headless guard, overlay now shows an `entities N` count.
  `tests/unit/entity_visual_test.cpp` — 14 cases. 113/119 total (non-Lua/Lua
  builds) test cases green.
  - **Gotcha:** `resolve_anim_clip`'s horizontal-speed calc needs both
    operands explicitly cast to `double` before multiplying
    (`static_cast<double>(vel.x) * vel.x` still promotes the second operand
    implicitly) — `-Wdouble-promotion` catches this, same class of trap noted
    for Phase 1.5's camera code.
  - **Confirmed, not assumed:** raylib 5.5's `DrawBillboardPro` with a fixed
    `up = {0,1,0}` gives exactly Y-axis (cylindrical) billboarding for free —
    read `rmodels.c` directly rather than trusting the header comment; its
    `right` vector comes from the camera view matrix but is always horizontal
    regardless of pitch (cross of any forward vector with world-up has zero Y
    component).
  - **Not yet visually verified**: no second connected player exists to look
    at (needs `GnsTransport`, or an in-process 2-client test harness with a
    window) — correctness rests on the unit tests + code review, not eyes on
    screen. Flag if a real two-player session shows different behaviour than
    the math predicts.
  - **Still open:** `SpriteVisual` ECS component (waits on 4.2 having a real
    per-kind visual def to populate it with); the `dead`/`hurt_pulse`/`acting`
    flag bits are defined but nothing sends them yet (server only ever sets
    bit 0).

- **2026-09-11 — Entity visual presentation decided: billboard sprites**
  (committed `adfc68e`).
  (design only, no code). Players/entities are Don't Starve-style 2D
  billboards, not 3D blocky models — `ARCHITECTURE_SPEC.md` §11.3, open
  question §19 Q7, backlog `REMAINING_TASKS.md` Phase 3.5. Closes a real,
  previously-silent gap: nothing has ever drawn a remote player client-side —
  `remote_entities()`/`interpolated_pos()` (Phase 3.4) are correct but nothing
  visible used them. Key technical finding baked into the design: raylib's
  `DrawBillboardPro(..., up={0,1,0}, ...)` gives exactly the wanted Y-axis
  (cylindrical) billboard for free — checked directly against `rmodels.c`
  (5.5): its `right` vector comes from the camera view matrix but is always
  horizontal regardless of pitch (cross of any forward vector with world-up has
  zero Y component), so no custom quad math is needed. Plan: Phase 3 ships a
  hardcoded single-frame placeholder (no Lua/pack dependency, mirrors how
  Phase 2 shipped a mesher ahead of Cellulose); real art + the
  `vb.register_entity{ visual = {...} }` surface land with 4.2/4.4/5.1.

- **2026-09-11 — Bugfixes from the first manual smoke test** (committed `009195e`).
  Three real bugs found by testing on Windows + macOS:
  1. **`sol::nil` doesn't exist on Apple platforms** — sol2 disables its `nil`
     alias by default whenever `__MAC_OS_X_VERSION_MAX_ALLOWED`/`__OBJC__`/a
     `nil` macro is visible (avoids clashing with Objective-C `nil`); see
     `sol/version.hpp` `SOL_NIL_I_`. Fixed by using `sol::lua_nil` (the
     always-defined underlying constant) instead of `sol::nil` in `vm.cpp`.
     **Lesson: never use `sol::nil`/`sol::type` names that shadow platform
     macros — prefer the `lua_`-prefixed sol2 spellings.**
  2. **Sprint had no effect (and walking was already ~17% under `walk_speed`)**
     — `step_movement` applied ground friction *every* tick regardless of
     input, fighting the acceleration step every frame. The fixed point of
     that tug-of-war is `accel/friction` (≈3.75 m/s with the shipped
     constants) **independent of the wish speed**, so both walk (4.5) and
     sprint (7.0) converged to the same capped speed. Fixed: friction now only
     applies when there's no active wish direction (skid-to-a-stop on
     release); acceleration alone drives velocity toward `wish_vel` while
     moving. Regression tests added (`physics_test.cpp`): sustained-movement
     reaches `walk_speed`/`sprint_speed`, and releasing input decelerates via
     friction. **Lesson: an FPS-style accel/friction model needs friction
     gated on "no input", not unconditional — the two tests that existed
     before only checked qualitative behaviour (stops at a wall, settles on a
     floor), never a steady-state speed value, so this shipped unnoticed.**
  3. **Mesh corruption near water ("holes"/wrong AO, worst on beaches)** —
     `chunk_mesher` only culled faces against `is_opaque` neighbours; water is
     non-opaque, so nothing culled water-against-water internal faces. A
     submerged region could emit enough vertices to overflow raylib's
     `Mesh.indices` (`unsigned short*`, 65535 max), silently wrapping the index
     and corrupting the chunk's whole mesh — exactly the beach-adjacent
     glitches reported. Fixed: face culling (and AO sampling) now also treats
     `is_liquid` neighbours as face-blocking, so water meshes as a solid-looking
     box (proper transparency stays Phase 4.3). Added a defensive
     `kMaxMeshVertices` cap as a backstop against any future case that still
     overflows — degrades to a truncated mesh instead of corrupted geometry.
     Regression tests added (`mesher_test.cpp`): a full water chunk only meshes
     its outer shell; water-on-stone culls the shared face both ways.
     **Lesson: `raylib::Mesh` is a hard 16-bit-index format — any per-chunk
     mesh generator needs either a face-count safety margin or an explicit cap;
     there wasn't one.**

  **Follow-up (same session):** the water-culling fix didn't fully address
  "AO is wrong" — it was two separate bugs. The real AO bug: a single shared
  `kCornerUV` 4-entry `{su,sv}` sign table was reused for all 6 mesher faces,
  but each face's `kFaceCorners` winding sits differently relative to its own
  `kFaceTangents` axes; only `+X` happened to match the table by coincidence
  (`-X`/`+Z` had `u` fully inverted, `+Y` was rotated by one corner, `-Y` was
  mirrored) — AO landed on a *different* corner than the one actually
  occluded, on 5 of 6 face orientations. Fixed by computing each corner's
  `(u,v)` sign directly from its own coordinates dotted with that face's
  tangent axes (`corner_sign()`) instead of a hand-matched table — correct by
  construction, no per-face bookkeeping to get wrong. Verified the regression
  test actually catches it: reverting just the fix reproduces the bug (bright
  vertex where the occluder sits, a different vertex wrongly darkened).
  **Lesson: a shared lookup table indexed by "corner number" across 6 faces
  with independently-chosen winding + tangent-axis conventions is exactly the
  kind of thing that looks right for the first case you check (+X) and is
  wrong for the rest — derive from geometry instead of hand-deriving a table
  per case.**

  **Known, deliberately not fixed:** auto-step-up "jerks" the camera (the
  physics teleports the feet up ~1 block in a single tick — correct and
  robust, but visually abrupt). Fixing it well means smoothing the *rendered*
  eye height independently of the physics position (physics/prediction must
  stay exact; only the camera can lag), which is real work I didn't want to
  rush alongside the two correctness bugs above. Tracked in
  `REMAINING_TASKS.md` Phase 3.3 as a follow-up.

- **2026-09-11 — Phase 5.2 block breaking / placing over the network**
  (committed `e5e5640`). `C2S_BlockEdit` / `S2C_BlockEditResult` (`vb/protocol/world`) →
  **`kEngineProtocolVersion` 2 → 3**. `WorldReplicator::apply_block_edit` (reach
  ≤5.5, target validity, no-floating-placement, whole-chunk `relight_chunk`,
  builds an `S2C_ChunkDelta` with the block + diffed light bytes, fans out to
  every player whose `last_sent_` has that chunk). `ServerSession::handle_block_edit`
  routes it. `ClientSession::push_block_edit` (optimistic apply to `chunks_`,
  rollback on a `!accepted` result, drop pending on the authoritative delta) +
  `ClientChunkStore::edit_block` (bumps the chunk + bordering chunk revisions so
  the renderer re-meshes). Client `main.cpp`: Amanatides–Woo voxel raycast from
  the eye, wire-cube highlight, LMB break / RMB place stone (singleplayer path).
  Tests: `tests/unit/blockedit_test.cpp` (round-trip + 2-client fan-out + reject
  rollback).
  - **Lua veto seam:** `apply_block_edit` has a `[Phase 4.2]` comment where the
    `block_break`/`block_place` handler hooks in; region protection too.
  - **Not done:** cross-chunk light propagation on edit (relight is per-chunk),
    break progress / tool times (instant break), drops (needs items — Phase 5.1),
    `S2C_BlockEditResult` reason codes.

- **2026-09-11 — Phase 4.1 Lua VM** (committed `2e3d297`). `inc/vb/script/vm.hpp` +
  `src/script/vm.cpp`: `vb::script::Vm` — pImpl over one `sol::state`, sandboxed
  at construction (base/string/table/math/coroutine/utf8 only; os/io/load/require/
  package/collectgarbage nilled; `debug` trimmed to `traceback`), ceiling
  allocator, per-call `lua_sethook(LUA_MASKCOUNT)` instruction budget. `do_string`
  is source-only. `ScriptError` enum added to `core/error.hpp`. `tests/unit/
  script_test.cpp` (7 cases, `#if VB_WITH_LUA`). CI build workflows now pass
  `-DVB_WITH_LUA=ON` (Lua is pure C, no system deps; sol2 header-only).
  - **sol2 pin bumped v3.3.0 → v3.5.0**: 3.3.0's bundled "better optional"
    (`optional_implementation.hpp`) fails to compile under Clang ≥ 18
    ("no member named 'construct' in optional<T&>"). `docs/lua-api.md` +
    `Dependencies.cmake` note it.
  - **vm.cpp compiles in every build**: `#if !VB_WITH_LUA` gives a `kDisabled`
    stub, so `vb_core` always has the symbols and `script_test.cpp` links.
  - **Local dev:** `cmake -S . -B build-lua -DVB_WITH_LUA=ON -DVB_WITH_COMPRESSION=ON`
    — first configure re-fetches lua + sol2 (~45s). `build/` stays Lua-off.
  - **Not thread-safe:** the instruction hook assumes one `Vm` per thread (fine —
    the pack VM lives on the server tick thread).
  - **Next (4.2):** `vb.register_*` + event bus + `vb.world` API on top of `Vm`;
    (4.3) `S2C_BlockRegistry`; (4.4) asset sync + virtual FS `require`.

- **2026-09-11 — Phase 3 physics + netcode slice** (committed `37e39a3`). New
  `vb/physics/movement.{hpp,cpp}` (`step_movement` — shared server + client
  prediction; substepped swept-AABB w/ bisection snap, gravity/friction/jump/
  step-up/fly; `MoveParams`). `vb/protocol/input.{hpp,cpp}` (`InputCmd` /
  `C2SInputBatch`, lane 4). `S2C_EntitySnapshot` gained `has_local` + `local`
  record + `flags` bit0=on_ground → **`kEngineProtocolVersion` 1 → 2**.
  `ServerSession`: `handle_input_batch` (authoritative movement, dt clamp,
  seq dedup), per-`Conn` `MoveState`; snapshots now carry the recipient's own
  state. `ClientSession`: `push_input` (predict + history ring + resend),
  reconcile-on-snapshot, `remote_samples_` + `interpolated_pos`. Client
  `main.cpp` walks input-driven (collides with terrain) instead of free-fly.
  `inc/vb/ecs/components.hpp` (struct defs only). Tests: `physics_test.cpp`,
  `netcode_test.cpp`. ~99.5k assertions, green under `-Werror`.
  - **Gotcha:** step-up in a pure-voxel world needs `step_height > 1.0` to climb a
    full block (default is `1.05`); Quake-style 0.55 climbs nothing here.
  - **Gotcha:** `ServerSession` intercepts *all* post-join C2S frames now (input
    batch handled, others ignored) — it no longer forwards them to the handshake
    FSM. Fine today; revisit when C2S block-edit/chat land.
  - **Deferred:** EnTT registry + system runner (3.1), wall-clock server-time
    estimation + librg entity mapping (need real `GnsTransport`).

- **2026-09-11 — Phase 2 complete** (`ce7ee63`..HEAD). `vb/world`:
  `PalettedChunkStore`, `Chunk`, `World`, `BlockRegistry`, `LightEngine`
  (per-chunk flood fill), `chunk_codec` (RLE), `chunk_interest`,
  `ChunkLifecycleSystem`, `ClientChunkStore`, `chunk_mesher` (face-cull + AO).
  `vb/worldgen`: deterministic `vb/core/noise.hpp` (no trig, `-ffp-contract=off`
  project-wide), `WorldGenerator` (fBm heightmap), `WorldGenWorkerPool`
  (+`kSynchronous`). `vb/protocol`: `S2C_EntitySnapshot`, `S2C_Chunk*`.
  `net/world_replicator`: per-tick chunk streaming, wired into `ServerSession`
  (`set_world_replicator`) + `ClientSession`. `vb/render/chunk_renderer`: raylib
  GPU upload. `voxel_browser --singleplayer` = worldgen + streaming + meshing +
  rendering, one code path. Determinism golden `0x021BB3847413D8A5` green on
  all 3 platforms. ~99k test assertions.
- **GCC gotcha:** `uint64_t` (`unsigned long` on LP64) vs `...ULL` literals
  (`unsigned long long`) trips `-Wsign-conversion` — always name wide constants
  `constexpr std::uint64_t`.
- **Reference-lifetime gotcha:** don't store `const BlockRegistry&` — callers
  pass `BlockRegistry::base()` temporaries. `LightEngine` holds it by value.

- **2026-09-10 — Phase 1.4 replication** (uncommitted). librg spike done →
  `docs/replication.md` + `ARCHITECTURE_SPEC.md §19 Q3` (librg v7.4.0 is a
  self-contained header, zpl bundled; use it for culling + create/update/remove
  framing, our codec for payloads; not wired yet). `Dependencies.cmake` librg
  block fixed (`v7.4.0`, `vb_librg` INTERFACE target, no separate zpl).
  `inc/vb/replication/interest.hpp` (`InterestGrid` + `diff_interest`, header-
  only, linear scan). `vb/protocol/snapshot.{hpp,cpp}` (`S2CEntitySnapshot`).
  `ServerSession` broadcasts per-player snapshots each tick;
  `ClientSession::remote_entities()`; `ServerSession::set_player_state()`.
  `tests/unit/replication_test.cpp` — two-client visibility (Phase 1 exit
  criterion). 46 cases / 280 assertions.
- **CI gotcha (found on first push):** `doctest.h` trips MSVC `/W4 /WX`
  (`C2220` at doctest.h:539). Fix: `tests/CMakeLists.txt` re-adds
  `doctest::doctest`'s include dir as `SYSTEM PRIVATE`. clang/gcc never hit it,
  so it must be verified in CI. Same pattern already used for raygui/toml++.

- **2026-09-10 — Phase 1.1 TOML config** (uncommitted). `tomlplusplus` v3.4.0
  added to `Dependencies.cmake` (always-on, header-only, linked PRIVATE into
  `vb_core`). `vb/core/config.{hpp,cpp}`: `ServerConfig`/`ClientConfig` (spec
  §15 defaults), `parse_*`/`load_*` (missing file → defaults, malformed →
  `kParseError`), `apply_cli_overrides`. Both binaries load `--config` +
  overrides. `server.toml.example` / `client.toml.example` in repo root;
  `/server.toml` `/client.toml` gitignored. `tests/unit/config_test.cpp`.
  41 test cases / 242 assertions green.

- **2026-09-10 — Phase 1.5 client shell** (uncommitted). `vb/render/camera.hpp`
  — header-only `FirstPersonController` (double precision throughout; the strict
  `-Wdouble-promotion` warning makes mixed float/double painful, so everything
  angle/position is `double` and `LookMoveInput` holds `Vec2d`/`Vec3d`). Client
  `main.cpp` gained mouse-look + WASD + a debug overlay + spawn-from-JoinAccept.
  `tests/unit/render_test.cpp` (5 cases). Rule: no `1.0f` literals anywhere the
  value flows into a `double` — CI's `-Wdouble-promotion` is fatal.

- **2026-09-16 — Phase 4 complete (4.1–4.5), uncommitted: sandboxed server
  Lua pack API, block registry replication, asset sync protocol, and a
  second client-side Lua UI VM, all wired end-to-end over
  `LoopbackTransport` and verified in both `VB_WITH_LUA=ON`/`OFF` builds.**
  `kEngineProtocolVersion` went 3→4 (4.2, `S2C_Chat`) →5 (4.3,
  `S2C_BlockRegistry`) →6 (4.4, asset-sync messages) →7 (4.5, `C2S_UiEvent`).
  Mechanism only — **no `content/base` pack exists (Phase 5.1)**, so nothing
  calls `vb.register_block`/`ui.define`/etc. at real runtime today outside
  of tests; `docs/lua-api.md`/`docs/protocol.md`/`REMAINING_TASKS.md` all say
  this explicitly, don't let it read as "Phase 4 not done."
  **Gotchas/landmines for whoever touches this next:**
  - **xxHash/lz4 header collision, already fixed — don't undo the link
    order.** lz4's vendored source ships its own old `lib/xxhash.h` with no
    `XXH3_128bits`/`XXH128_hash_t`. `src/core/CMakeLists.txt`'s
    `VB_WITH_COMPRESSION` block links `xxHash::xxhash` **before**
    `LZ4::lz4` on purpose (comment left in place) so the real header wins
    `-I` search order. Relinking `LZ4::lz4` first silently breaks
    `assetsync/manifest.cpp`'s hashing with cryptic "unknown type name"
    errors from inside a *different* header than the one you'd suspect.
  - `std::vector<std::byte>` can't be built directly from
    `std::istreambuf_iterator<char>` (no implicit `char`→`std::byte`
    conversion) — `assetsync/cache.cpp` has a `read_whole_file()` helper
    (`ifstream` + `tellg`/`seekg`/`.read()` + `reinterpret_cast<char*>`) for
    this; reuse it rather than re-deriving the istreambuf_iterator pattern.
  - `vb::net::ClientSession::send_ui_event`/similar helpers inside
    `session.hpp` are already in `namespace vb::net` — call `send_message`
    unqualified there, not `net::send_message` (that resolves to a
    nonexistent `vb::net::net`).
  - **Known, documented gap, not a bug:** `vb.world.set_block()` (the pack
    API's direct world-mutation call) does **not** run the relight cascade
    that `C2S_BlockEdit` triggers — can desync lighting until something
    else touches the chunk. No test covers a pack calling this yet because
    no pack does. Fix when a real pack needs `vb.world.set_block` for
    something other than worldgen-time setup.
  - **UI layout is evaluated once at `open()`, never re-run.** A
    server-driven UI that wants to show different content mid-session must
    `ui.close()` then have the server call `player:open_ui()` again — there
    is no re-layout/refresh call. Documented in `docs/lua-api.md`, not
    fixed — if this becomes a real pain in 5.x, the fix point is
    `UiRuntime::Impl::open()` (`src/script/ui_runtime.cpp`).
  - **Widget set is missing "item grid"** (the one spec-named widget type
    not implemented) — deferred because it needs a real item system
    (5.1). Everything else (label/panel/button/textbox/list) is done.
  - **Asset-sync reconnect fast-path is in-session-only by explicit user
    choice** (not persisted across process restarts) — the on-disk
    content-addressed cache (`ClientAssetCache`, `<cache_dir>/<2-hex>/<32-hex>`)
    does still make repeat-connect data transfer zero-byte even across
    restarts (verified by test: "second connection transfers nothing"), but
    the manifest round-trip (listing what's needed) always happens fresh
    each connect — no skip-if-already-synced shortcut before that. If a
    future phase wants to skip the manifest listing too, that's new scope,
    not a bug.
  - `PackRuntime`'s Lua-visible player object is one merged usertype
    (player + would-be-entity) since no EnTT registry exists yet (Phase
    3.1 still open) — `vb.register_entity`'s `on_spawn`/`on_tick`/etc.
    callbacks are captured but **nothing ever calls them**.
    `vb.worldgen.set_pipeline` is bound but errors as a nil call — not
    implemented at all, out of scope for 4.x.
  - Test-design trick worth remembering: there's no event that hands a Lua
    script a `PlayerHandle` right at `player_join` time with a live net_id
    (join fires from inside `authenticate`, before a session exists) — both
    `pack_runtime_integration_test.cpp` and the UI round-trip test trigger
    `player:open_ui()`/inventory-style flows from inside a `block_break`
    handler instead, which does have a real handle. Reuse this pattern for
    any future test that needs a Lua-side player object outside of tick.
  - Full verification each phase: `ctest` green in `build-lua`
    (`VB_WITH_LUA=ON` + `VB_WITH_COMPRESSION=ON`, all cases) and
    `build-release` (`VB_WITH_LUA=OFF`/`VB_WITH_COMPRESSION=OFF`, confirms
    every disabled-stub class compiles/links and both binaries still run).
    The one recurring failure across both configs is the same
    pre-existing `gns_transport_test.cpp` real-UDP-socket sandbox
    limitation documented elsewhere in this file — not a regression from
    this work.
  - **Not done, deliberately out of scope for 4.x:** no real content pack
    (5.1); `require`-over-synced-virtual-pack-FS (the client's
    `virtual_pack_fs()` exists but nothing consumes it — no module loader
    wired to the synced asset cache yet); per-callback wall-clock budget
    enforcement (only instruction-count budget exists); a visually-verified
    live UI render (no GL context available in this environment — rests on
    `ui_runtime_test.cpp`/`pack_runtime_integration_test.cpp` + code
    review, same caveat as the rendering-related entries above).

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

- **2026-09-16 — Phase 5.1: `content/base` pack written, and a real gap
  found & fixed along the way: neither binary ever loaded a content pack at
  all (uncommitted at time of writing).**
  Before this: `src/server/main.cpp` constructed a `PackRuntime` and called
  `freeze()` immediately with nothing registered — `content_pack` in
  `server.toml` was read (for the asset manifest + storage path) but never
  actually pointed at anything Lua. Every Phase 4.2 test exercised
  `PackRuntime` directly (`load_pack_file` with inline source strings), so
  this never showed up as a test failure — it's the same category of gap as
  4.5's "nothing calls `ui.define` at real runtime today", just never
  written down explicitly for the server side.
  **Why a loader was needed instead of just `pack_runtime.load_pack_file(
  read_whole_file("init.lua"))`:** the spec's pack layout (§16) has
  `init.lua` `require` in `blocks/*.lua`/`entities/*.lua`, but `require` is
  one of the globals nil'd out by the sandbox (`vm.cpp`'s `strip_sandbox`)
  and was never reimplemented over the virtual pack FS (tracked as a
  REMAINING_TASKS.md 4.1 follow-up, still open). New
  `vb::script::load_content_pack` (`inc/vb/script/pack_loader.hpp` +
  `src/script/pack_loader.cpp`) works around this at the *host* level
  instead: walks `blocks/*.lua` → `entities/*.lua` → `biomes/*.lua` (each
  sorted for determinism) → `init.lua`, calling `load_pack_file` once per
  file. This is behaviourally identical to one concatenated script because
  every file shares the same Lua globals (`vb.register_block` etc. all live
  on one `PackRuntime`'s `sol::state`) — a later file (e.g.
  `blocks/grass.lua`) can read a plain global a sorted-earlier file set
  (`blocks/dirt.lua` sets `base_dirt_id`), which is how `content/base`
  itself demonstrates cross-file sharing without `require`.
  `src/server/main.cpp` now calls this before `freeze()`, and treats a real
  syntax/runtime error in any pack file as fatal server startup (same "a
  broken pack is a broken deployment" posture as a bad asset manifest);
  `core::ScriptError::kDisabled` (a `VB_WITH_LUA`-off build) is treated as
  non-fatal and logged once, matching every other `VB_WITH_*`-off
  graceful-degrade in this codebase.
  **The client had the identical gap for UI screens**, also fixed:
  `src/client/main.cpp` never called `UiRuntime::load_pack_file` for
  anything, so `ui.define` was never invoked outside tests either (4.5's
  exact words: "nothing calls `ui.define` at real runtime today"). Now,
  right after a real multiplayer join (`--server`, not `--singleplayer` —
  see below), the client iterates `ClientSession::virtual_pack_fs()`
  (Asset Sync's in-memory synced-files map, 4.4 — populated by the time
  join completes, since asset transfer sits between Auth and Ready in the
  handshake, §8.3) and loads every `ui/*.lua` entry into `UiRuntime`.
  **Known gap this does NOT close, worth knowing before assuming UI
  screens are reachable:** `player:open_ui(name, ctx)` is still the *only*
  way any screen opens (server-push only), and nothing calls it in real
  gameplay — there's no client keybind or C2S message requesting "open my
  inventory" or "pause". `content/base/ui/{pause,inventory}.lua` load and
  register cleanly (verified — see below) but currently can't be triggered
  by a player. That's a `REMAINING_TASKS.md` 5.1/5.4-shaped follow-up, not
  attempted here on purpose (adding an ad-hoc trigger, e.g. auto-opening
  the inventory on every block break, would be surprising unrequested
  gameplay behaviour, not a real fix).
  **`--singleplayer` still doesn't wire any of this** — it has no
  `PackRuntime`/asset manifest on its in-process `IntegratedGame` path at
  all (pre-existing gap, REMAINING_TASKS.md 4.3 already noted this for the
  block registry specifically; it applies equally to the pack loader and UI
  loading added here). Not attempted — wiring a `PackRuntime` into
  `Singleplayer`'s constructor (`src/client/main.cpp`) is a real, separate
  chunk of work (needs its own manifest/storage path, and the *client*
  reading Lua files straight off disk instead of through asset sync since
  there's no separate server process to sync from), closer in spirit to
  5.3's "integrated-server path for singleplayer" than to authoring pack
  content.
  **Content itself** (`content/base/{pack.toml,init.lua,blocks,entities,
  biomes,ui}/*.lua`): registers the Phase 2 base block set by name (ids
  unchanged, `add_or_get` is idempotent), wires `on_break` to actually give
  the broken block back via `player:give(...)` (a real drop — distinct
  from `on_break`'s *return value*, which `pack_runtime.cpp`'s
  `on_block_edit_after` still only logs, not materializes), and declares
  two biomes + one entity kind purely for the pack-format shape (neither
  has a consumer yet — worldgen is still the hardcoded Phase 2 pipeline,
  and generic entities still wait on 3.1's EnTT registry). `vb.storage` is
  used for real (a boot counter), not just declared, to prove the
  JSON-round-trip path actually persists across restarts (verified live,
  see below). Full accounting of what's real vs. still-declarative lives in
  `REMAINING_TASKS.md`'s Phase 5.1 section now — this entry is the *why*,
  that one's the checklist.
  **New test:** `tests/unit/content_pack_test.cpp` loads the real
  `content/base` files (not inline strings, unlike every existing
  `pack_runtime_test.cpp` case) via `load_content_pack`, asserting a clean
  load + unchanged block ids, plus a separate broken-pack-directory-is-
  fatal case. Needed a new `VB_PROJECT_SOURCE_DIR` compile definition on
  `vb_tests` (`tests/CMakeLists.txt`) since ctest's working directory is
  the build dir, not the source tree, and this is the first test that needs
  to find a real file under the repo root rather than a temp file it wrote
  itself.
  **Verified, not just built:** full `vb_tests` green on `build-lua/`
  (`VB_WITH_LUA=ON`, headless-only build, 180/180) and on `build-net/`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=OFF`, real GNS + real client, 147/148 —
  the 1 failure is the pre-existing sandbox UDP-bind one from §3, confirmed
  unrelated). Ran the actual dedicated server twice in a row against
  `content/base` (`build-lua/voxel_browser_server.exe --content-pack
  content/base`) and watched `vb.storage.boot_count` go 1 → 2 across the
  two separate process runs, proving the persistence path really works, not
  just that the code compiles. Ran a real two-process smoke test over
  `build-net`'s GNS build: `voxel_browser_server.exe --content-pack
  content/base --port 27099` + a real headless `voxel_browser.exe --server
  127.0.0.1 --port 27099` joined successfully and received an
  8-block registry (the unchanged Phase 2 base set, since this build has no
  Lua) — confirms `load_content_pack`'s `VB_WITH_LUA`-off degrade path
  doesn't break a live join. **First caught a stale-binary trap while doing
  this:** `build-net`'s `voxel_browser_server.exe` hadn't been rebuilt after
  editing `src/server/main.cpp` (only `voxel_browser`/`vb_tests` were
  explicitly targeted) — the old binary printed the *previous* commit's
  version banner and rejected the handshake outright (protocol mismatch, 3
  vs. 7); always rebuild every target that changed, not just the one you
  think you're testing, before trusting a live-process smoke test.
  **Not done / worth knowing before touching this again:** no full
  4-way combination (`VB_WITH_LUA=ON` *and* `VB_WITH_NET=ON` *and* a real
  client build) exists in this environment's build dirs, so the "a real
  Lua-driven block registry (more than 8 blocks) reaches a real
  GNS-connected client" path specifically is unverified live — it rests on
  `build-lua`'s server-side test coverage (Lua registration definitely
  works) plus `build-net`'s live-join coverage (the wire path definitely
  works) separately, not both at once. If content ever adds a genuinely
  new block (not just a re-declaration), that combination is worth a real
  live check before trusting it blind.
