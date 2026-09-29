# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here. Keep this file lean
> (~300-400 lines) — new verbose writeups belong in `state/*.md`, linked from
> here, not pasted inline. See "Detail files" at the bottom for the index.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.
>
> **If what you learned is tied to a specific physical machine** (a tool
> path, an agent-shell/Bash-vs-PowerShell quirk, which pre-built `build-*`
> dirs exist, a local toolchain oddity) **write it to `STATE.md.local`
> instead of here** — see that file's own header for why. This file (and
> `state/*.md`) is for gotchas that hold regardless of which machine an
> agent is running on.

---

## Standing priority: prefer engine (C++) work over content (Lua)

When picking what to work on next and multiple open items are roughly equally
ready (e.g. choosing among REMAINING_TASKS' several `[ ]` items with no other
constraint forcing one), **prioritize a task that changes the engine (C++
code under `src/`/`inc/`) over one that's purely `content/*.lua` policy/
content work.** Most remaining Lua-only gaps (PvP/mob damage policy, hunger,
more base-pack art/reskins, etc.) are content authors' problems the engine
already has the primitives for; the engine surface itself is where real
mechanism gaps still exist and where a wrong call is much more expensive to
unwind later. This is a standing preference for ambiguous "what next"
choices, not a rule to force an engine change where a task is inherently
content-only (e.g. the user explicitly asks for a content-pack feature).

---

## Current status (2026-09-28)

**Same-day follow-up #3: fixed the loading screen dismissing over a
still-empty world a third time** (user-reported regression, same symptom as
the two prior fixes noted below). Root cause this time: commit "Wire
--singleplayer's integrated server to a real RegionStore" gave
`Singleplayer` a real `RegionStore` for the first time, which put
`--singleplayer` through `ChunkLifecycleSystem::update()`'s step 2 disk-load
path (`src/world/chunk_lifecycle.cpp`) that "Same-day follow-up #2" below had
already budget-capped to `ingest_budget_` (default 32) per `update()` call --
but that cap counted *every* not-yet-requested coord against the budget,
disk hit or miss, before even checking which one it was. A brand new
singleplayer world (or any mostly-unsaved view box) is almost all misses,
and a miss costs nothing (a hash lookup into an already-decompressed
in-memory `Region` -- the real LZ4 decompression cost happens once per
*region file*, not per chunk, in `RegionStore::region_for()`), so gating
misses behind the same counter as real disk hits (whose `relight_column()`
cascade the cap was actually meant to bound) throttled a fresh world's
*entire* initial worldgen submission down to 32 new chunk requests per real
tick -- unbounded before this commit, since singleplayer never had a
`RegionStore` to trigger this branch at all. Measured with a temporary
repro test (real, non-synchronous `WorldGenWorkerPool`, `view_distance=4`'s
567-chunk box, ~20Hz tick pacing): 18 ticks / ~6.1s to fully populate
`world_` before the fix -- already past the 5s stall deadline on its own,
before the client's additional meshing/upload latency on top. Fixed by only
counting `disk_loads` on an actual hit (i.e. checking the budget *after*
`region_store_->load()` returns non-null, not before calling it) -- a miss
now always falls straight through to `pool_.submit()` unthrottled, same as
when `region_store_` is nullptr, while a real hit's insert+relight still
shares `ingest_budget_` exactly as "Same-day follow-up #2" intended. Same
repro after the fix: 9 ticks / ~4.3s (roughly 2x the per-tick throughput,
since submission is no longer the bottleneck). `vb_tests` 285/285 green on a
headless/no-net/no-lua build (the subset that builds without those deps);
see `src/world/chunk_lifecycle.cpp`'s step-2 comment for the updated
rationale.

Before that: full reverse-chronological detail for everything back
through 6.18 (discrete punch combat) moved to `state/changelog-part3.md`
to keep this section within budget -- see that file's own header for
the full topic list, or "Detail files" below.

---

## 0. Repo snapshot

- `674d88f Initial commit` + the Phase 0 restructure (uncommitted at time of
  writing). Still **no tags**. Remote: `https://github.com/RechieKho/voxel_browser.git`.
- Source tree matches spec §4: `vb_core` (`src/core/`), `vb_render`
  (`src/render/`), `voxel_browser` (`src/client/`), `voxel_browser_server`
  (`src/server/`), `vb_tests` (`tests/`). Other module dirs are `.gitkeep` stubs.
- Builds green on Windows/clang with `-DVB_WARNINGS_AS_ERRORS=ON`.
- `ARCHITECTURE_SPEC.md` / `REMAINING_TASKS.md` are still design intent for
  Phase 1+.

---

## 1. Build-blocking bugs — ✅ all fixed in Phase 0

All four were fixed during the Phase 0 restructure (2026-09-10):
version info moved to a generated `vb/core/version.hpp`; raylib pinned to
`5.5` (not `6.0` — that tag never existed) with raygui matching `4.0`;
`cmake_minimum_required` bumped to `3.25`; a `CMAKE_POLICY_VERSION_MINIMUM
3.5` shim in `cmake/Dependencies.cmake` works around CMake ≥ 4.0 rejecting
doctest 2.4.11's `cmake_minimum_required(VERSION 3.0)` (remove the shim
once doctest ships a fix). Full detail in `state/changelog-part1.md`'s
2026-09-10 entries.

## 2. Naming — ✅ resolved

`PROJECT_NAME` is `voxel_browser`. Executables: `voxel_browser` (client),
`voxel_browser_server`. CI artifacts are
`voxel_browser-<target>-<arch>-<build_type>`.

---

## 3. CI landmines

- **No tags exist.** `setup_metadata.yml` runs `git describe --tags
  --abbrev=0` for the `version` output — this **errors** with no tags in
  history. `bundle`/`publish` depend on `setup_metadata`; first `git tag
  v0.0.1` will unblock. Until then anything past `build_*` is untested /
  likely red.
- `publish.yml` triggers only on `v*.*.*` tags and pulls artifacts from
  `runner.yml` by name `${project_name}` (note trailing space in the YAML
  on `name:` line 26 — `action-download-artifact` may or may not trim it).
- `lint.yml` installs clang-format via `pip install` then runs it via
  `pipx run clang-format` (dead weight from the pip install). Lints `src/**`
  with `--Werror` — every new file under `src/` must be clang-format-clean.
- Build workflows use `actions/checkout@v4 submodules: recursive`, but
  **there are no submodules** — deps are `FetchContent`. Harmless.
- `build_linux.yml` installs X11/GL dev packages for raylib, plus
  `libssl-dev libprotobuf-dev protobuf-compiler` for `VB_WITH_NET`.
- **`VB_WITH_NET` CI coverage is uneven:** Linux (apt) and Windows (vcpkg)
  build it; **macOS does not** (single-arch Homebrew protobuf vs. the
  universal arm64+x86_64 build — see `state/dependencies-detail` pointer
  below). If macOS ever gets it, `build_macos.yml`'s configure step is
  where to add it.
- **Never call `find_package(Protobuf REQUIRED)` a second time anywhere in
  the tree.** GameNetworkingSockets' own `src/CMakeLists.txt` already calls
  it; a second call (even as a "fail fast" convenience) fatal-errors on
  some protobuf installs (Homebrew, not vcpkg) with "Some (but not all)
  targets in this export set were already defined." Fixed 2026-09-11 by
  deleting the redundant call — don't reintroduce it.
- **Machine-local build/toolchain/agent-shell gotchas (vcvars64.bat path,
  Bash-vs-PowerShell-tool quirks, which pre-built `build-*` dirs actually
  exist, macOS-specific findings) live in `STATE.md.local`, not here** —
  gitignored (`*.local`), specific to whatever physical machine an agent
  session runs on; check it first when setting up a build in an agent
  shell, and add to it rather than here when you hit a new one.

---

## 4. Config / style quirks

- `.clang-format` is **Godot's** clang-format file verbatim. The C++ rules
  that matter: **tabs** (`UseTab: Always`, `TabWidth 4`), `ColumnLimit: 0`
  (no auto-wrap), `AccessModifierOffset: -4`, pointers right-aligned,
  `Cpp11BracedListStyle: false`. `Standard: c++17` but CMake sets
  `CMAKE_CXX_STANDARD 20` — fine unless C++20-only syntax confuses the
  formatter; bump to `c++20` if that happens.
- `.gitignore` ignores `build`, `*.local`, `compile_commands.json`,
  `.vscode/*` (except `extensions.json`). `CMAKE_EXPORT_COMPILE_COMMANDS
  ON` is set — symlink/copy `build/compile_commands.json` to root for
  tooling.
- `CMakeLists.txt:54` uses `file(GLOB_RECURSE SOURCE_FILES ...)` — adding a
  `.cpp` doesn't trigger reconfigure. Keep `cmake` re-runs in the loop.
- `libfantastic` (the lib target) is `add_library(... INTERFACE)` — assumes
  header-only. `vb_core` is a real STATIC lib; don't carry the INTERFACE
  assumption forward.
- `.gitignore`/`tests/CMakeLists.txt` are checked in with CRLF endings
  (every other text file is LF-only) despite `core.autocrlf=input` locally
  — `git add` warns harmlessly today, but the first real edit through a
  CRLF-preserving tool will silently flip the whole file's line endings,
  burying the actual diff. No `.gitattributes` forces this repo-wide. If
  ever cleaned up, use `git add --renormalize <path>` after adding a
  `.gitattributes` rule, not a manual find/replace.

**Longer-form gotchas (compiler/toolchain traps, full detail in
`state/gotchas.md`):**
- `std::erase`/`std::remove` on a `std::vector<ChunkCoord>` (or any small
  trivially-copyable struct that size/alignment shape) fails to compile
  under this repo's clang-targeting-MSVC-STL toolchain
  (`static_assert(false, "unexpected size")` inside `<xutility>`). Use a
  manual erase loop instead.
- A local `clang-format --dry-run --Werror` binary may not be trustworthy
  as-is against this repo's `.clang-format` — verify by diffing against an
  unmodified `HEAD` file before trusting either a pass or a wall of
  violations.
- `doctest`'s `--test-case=` filter is a **glob pattern**, not a substring
  match — wrap in `*...*` and quote it (zsh glob-expands an unquoted
  pattern itself).
- MSVC (`cl.exe`) rejects a ternary between two different instantiations of
  a templated smart-pointer type with converting constructors (`C2445`) —
  use plain `if`/`else` instead. Clang/GCC not cross-checked.
- A sibling file's "isn't implemented yet" comment can go stale the moment
  a later phase lands it, without the comment ever being updated — grep the
  actual current binding (e.g. `src/script/pack_runtime.cpp`) before
  reusing a sibling's "this doesn't work yet" framing in new code.
- A client built without `VB_WITH_COMPRESSION` can never asset-sync
  against a server built with it on (`hash_bytes()` stubs to all-zero) —
  every real (non-`--singleplayer`) connection needs **matching**
  `VB_WITH_COMPRESSION` on both binaries; there's no runtime negotiation.
- A second, independent bug produces the identical `"asset transfer failed
  (hash mismatch or size cap)"` text even with matching compression flags:
  the server used to build the asset manifest *before* `vb.storage`'s
  deferred first-tick flush reached disk. Fixed by calling
  `pack_runtime.flush_storage()` before `build_manifest()` in
  `src/server/main.cpp`. **This error string has (at least) two unrelated
  root causes** — check both before assuming which one you've hit.

---

## 5. Dependency notes / unknowns

- **Cellulose** (`github.com/RechieKho/cellulose`) — evaluated as a greedy
  mesher, then reverted after it reproduced an NVIDIA-driver VAO/VBO
  heap-corruption crash (greedy-merged geometry blows past the renderer's
  buffer-reuse headroom far more often than the hand-rolled per-face
  mesher). Hand-rolled `vb::world::chunk_mesher` is the **permanent**
  meshing backend now, not a placeholder. `VB_WITH_MESHING` scaffolding was
  later removed outright at user request (2026-09-17). Full story in
  `state/changelog-part2.md`.
- **FastNoise2**: real upstream is `Auburn/FastNoise2` (the README's
  `electronicarts/fastnoise` link is a fork, don't use it). Pin is
  `v0.10.0-alpha`, not `v0.10.0` (that tag doesn't exist — a real,
  previously-unnoticed bug fixed in Phase 6.14, see `state/changelog-part1.md`).
- **GameNetworkingSockets** — resolved (Phase 1.2, 2026-09-11), the single
  hardest dependency in the project. Protobuf cannot be `FetchContent`-ed
  (must be a real package-manager install: vcpkg/apt/brew). GNS pinned to
  `v1.6.0` (not `v1.4.1`, which doesn't compile under a modern stdlib). ICE/
  WebRTC disabled. Windows crypto is BCrypt; Linux/macOS use system
  OpenSSL. Linked statically. **Full story, including local-verification
  toolchain quirks and the Homebrew double-`find_package` crash, is in
  `state/changelog-part1.md`'s Phase 1.2 entries — read those before
  touching `cmake/Dependencies.cmake`'s GNS block again.**
- **librg** is `zpl-c/librg`, pinned `v7.4.0`, wired in as the default
  entity-replication backend since 2026-09-17 (`VB_WITH_REPLICATION`
  default ON; the original hand-rolled linear scan remains as an explicit
  opt-out). Known limitation: chunk ids need `chunkamount.x*y*z` to fit
  signed int32 and each axis casts to `int16_t` — picked 1024 chunks/axis
  (±512×cell_size world units); an entity straying outside that range is
  silently excluded from interest until it re-enters. Full detail in
  `state/changelog-part2.md`.
- **Lua**: PUC-Lua has no upstream CMake; needs a wrapper `CMakeLists.txt`.
  sol2 is the chosen binding layer (pinned `v3.5.0`, not `v3.3.0` — that
  version's bundled optional impl fails under Clang ≥ 18).
- **raygui** is header-only (`raysan5/raygui`), needs exactly one TU with
  `#define RAYGUI_IMPLEMENTATION` in its own target (`vb_raygui_impl`) so
  project `-Werror` never touches vendored code — same pattern used for
  librg's `LIBRG_IMPL`.

---

## 6. Design decisions still open

Tracked in `ARCHITECTURE_SPEC.md` §18, repeated here for visibility:

1. ~~Lua binding layer: `sol2` vs. raw C API.~~ **Resolved: sol2.**
2. ~~Cellulose meshing API shape.~~ **Resolved and reverted** (2026-09-16)
   — hand-rolled meshing stays permanent.
3. librg version + whether we use its serialization or only its interest
   culling. **Resolved (2026-09-17): interest culling only**, own codec for
   payloads.
4. Chunk compression: LZ4 (spec's starting choice) vs. zstd vs.
   palette-only. Still open.
5. ~~World persistence / region file format.~~ **Resolved 2026-09-25:** flat
   per-region files (`vb::world::RegionStore`), not the previously-noted LMDB
   direction — reverted after review, see "Current status" above. Wired into
   `voxel_browser_server` only; `--singleplayer` still has no persistence.
6. Auth: `auth_mode = none | token` — handshake reserves the field, no
   service exists.

Other undecided:
- Test framework: doctest (in use) vs. Catch2 — never revisited since
  Phase 0's pick.
- Whether the client embeds the server for singleplayer as a library or
  spawns a child process — **currently: in-process library**
  (`Singleplayer` in `src/client/main.cpp` constructs `LoopbackNetwork`/
  `ServerSession`/`ClientSession` directly).
- **`WorldReplicator` streams a whole player's view box in one uncapped
  burst per connect**, no per-tick pacing. GNS's send buffer was raised to
  32 MiB (2026-09-15) which comfortably covers the shipped default view
  distance (8/3, ~2023 chunks, ~545 KB measured) but doesn't add real
  backpressure — a larger view distance, denser world, or several players
  joining at once could still overflow it. Proper fix: a per-connection
  byte-budget-per-tick on the `diff.entered` send loop. **Revisit before
  ever raising the shipped default view distance.** Still open as of the
  2026-09-25 disk-load-budget fix above — that fix bounds
  `ChunkLifecycleSystem::update()`'s *ingest* side (worldgen + region-store
  disk loads) per tick, not this *send* side; a large view box still leaves
  in one uncapped burst once ingested.

---

## 7. Conventions to follow

- Namespaces `vb::<module>` (`vb::net`, `vb::world`, ...); headers under
  `inc/vb/<module>/`, mirrored by `src/<module>/`.
- No exceptions on hot paths; `Result<T,E>` for fallible ops. Never call
  `.error()` when a `Result` holds a value (union UB) — `REQUIRE(result)`
  then deref in tests, not `REQUIRE_MESSAGE(result, msg(err))`.
- Wire structs live in `inc/vb/protocol/`; every one gets a round-trip
  test and bumps `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` when
  changed. Concrete step-by-step for adding one: struct → `MessageType` →
  round-trip test → version bump → `docs/protocol.md` entry → integration
  test (see `CONTRIBUTING.md`).
- Keep both binaries buildable/runnable with `--headless` (CI +
  integration tests depend on it).
- Match `.clang-format`: tabs, no column limit, run clang-format before
  commit (CI `--Werror` on `src/**`).
- New Lua bindings should be **generic primitives, not game-specific
  ones** — game rules belong in `content/*` packs (e.g. `player:take()` is
  a generic inventory primitive; the actual crafting recipe list lives
  entirely in `content/base/crafting.lua`, not in engine code). See
  `CONTRIBUTING.md` for the worked example.
- Heavy deps (GNS, librg, Lua/sol2, FastNoise2, LZ4/xxHash) are declared in
  `Dependencies.cmake` but gated behind `VB_WITH_*` (several now default
  ON — check `CMakeLists.txt` for current defaults, don't assume OFF).
- doctest + MSVC STL: comparing/streaming a `std::string_view` in a
  `CHECK` needs `#include <ostream>` in that test TU (instantiates
  `toString<string_view>`).
- GCC gotcha: `uint64_t` (`unsigned long` on LP64) vs. `...ULL` literals
  trips `-Wsign-conversion` — always name wide constants `constexpr
  std::uint64_t`.
- Don't store `const BlockRegistry&` — callers pass `BlockRegistry::base()`
  temporaries; hold it by value.
- **xxHash/lz4 link order matters**: lz4's vendored source ships its own
  old `lib/xxhash.h` with no `XXH3_128bits`. `VB_WITH_COMPRESSION` links
  `xxHash::xxhash` **before** `LZ4::lz4` on purpose so the real header
  wins `-I` search order — don't reorder this.
- `std::vector<std::byte>` can't be built directly from
  `std::istreambuf_iterator<char>` — use the `read_whole_file()` helper in
  `assetsync/cache.cpp` instead of re-deriving it.
- **Clang: a nested class's `Foo() = default;` can't be used as a default
  argument value (`= {}`) on a member function of the *enclosing* class
  declared before that enclosing class is complete** — Clang errors
  wanting the nested class's field default-member-initializers "within
  definition of enclosing class" to compute the defaulted ctor's implicit
  exception spec, which isn't available yet at that point. Hit this in
  `inc/vb/world/lighting.hpp`'s `LightEngine::Neighbours() = default;`
  used as `relight_chunk(Chunk&, const Neighbours& = {})`'s default arg.
  Fix: give the nested ctor a user-provided empty body (`Neighbours() {}`)
  instead of `= default` — a user-provided ctor's noexcept-ness doesn't
  need the deferred computation, so no reordering/out-of-line-definition
  dance is required.

---

## Detail files

Every file below is full-detail material moved out of this core. Newest
work is summarized above under "Current status"; everything before that is
here, organized as it was originally written (mostly chronological,
newest-first within each file):

- **`state/changelog-part3.md`** — newest-first writeups moved out of
  "Current status" above: mob damage (and the still-open `PlayerHandle`
  corruption bug it surfaced), the hunger primitive, the "No PvP"
  confirmation, and back through all of Phase 7 (7.1-7.6), 6.18-6.21, and
  the visual/despawn/kind-rendering follow-ups, down to 6.18 (discrete
  punch combat) — see that file's own header for the full topic list;
  `changelog-recent.md` below picks up from there.
- **`state/changelog-recent.md`** — full writeups for the block-self-heal
  follow-up, Phase 6.18 (discrete punch combat), Phase 6.17 (decoupled
  block breaking + `MovementBindings`), Phase 1.3 networking polish
  (two-process smoke test, per-IP connection cap, hostname resolution),
  Phase 6.15 (kitchen-sink example pack), Phase 6.14 (Lua-driven worldgen/
  FastNoise2), Phase 6.13 (read-only server config visibility) — these
  phases were never logged into the older §8 structure below and are not
  duplicated in the next two files.
- **`state/changelog-part1.md`** — Phase 6.1/6.4/6.5/6.6/6.7/6.8/6.9/6.11/
  6.16 landed-feature writeups; the 2026-09-15 investigations (NVIDIA
  driver VAO/VBO heap corruption, FPS dip during chunk streaming,
  cross-chunk sky-light band, near-black flash on predicted breaks, chunk
  meshing moved off the main thread, GNS send-buffer overflow root cause);
  the 2026-09-10/11 Phase 0-4 bootstrap entries (config, transport,
  handshake, session layer, physics/netcode, worldgen, entity visuals,
  block editing, Lua VM); "Gotchas learned this pass".
- **`state/changelog-part2.md`** — Phase 5.1 `content/base` pack + the gap
  it found (neither binary loaded a pack at all before this); Phase 5.2-5.5
  (main menu, chat, player list/join-leave, day/night, death/respawn, sfx
  docs, real inventory sync + hotbar, hold-to-break progress, dropped-item
  entity, crafting recipes, `--singleplayer` running the real content pack,
  documentation pass); the Cellulose meshing spike and revert; librg wired
  in as the interest backend and flipped to default; Phase 6 design-only
  passes (entity classes, UI immediate-mode, keybind channel, `vb.db`,
  block-damage breaking, worldgen biome-selection redesign).
- **`state/gotchas.md`** — long-form compiler/toolchain traps referenced
  from §4 above (the `std::erase` MSVC-STL bug, the MSVC ternary
  ambiguity, the two independent asset-sync hash-mismatch bugs, the
  stale-comment lesson, the clang-format-untrustworthy-locally note).

Anything **not** listed above and not inline in this file no longer exists
in `STATE.md`'s history — if you're looking for something and can't find
it here, check `git log -- STATE.md` for when it might have been removed,
or `REMAINING_TASKS.md`/`ARCHITECTURE_SPEC.md` for design-level context.
