# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.

Last updated: 2026-09-18 (Phase 6.9 — inventory stacking: `BlockType::max_stack`
(default `world::kDefaultMaxStackSize` = 64) + `vb.register_block{max_stack=N}`
override, same shape as 6.5's `max_damage`, not `register_item` (every holdable
item is already a registered block, see `content/base/blocks/planks.lua`). New
`PackRuntime::Impl::give_item()` combines into existing under-cap slots before
starting new ones; both `player:give()` and the item-pickup handler now share
it (previously two separate `push_back` call sites). See §8's newest entry for
detail. Previous entry: Phase 6.16 — client-local HUD mechanism:
`ui.define_hud(render_fn)` + a new `client.*` raw-state table
(`client.break_progress()`/`client.screen_size()`) let a pack render the
hold-to-break progress bar in Lua instead of hardcoded `DrawRectangle` calls
in `src/client/main.cpp` — "engine provides raw state, Lua deals with
presentation." Also fixed a real bug found while wiring it: `--singleplayer`
never loaded any `ui/*.lua` file at all (no asset sync on that path), so
every existing Lua UI screen was silently dead there, not just the new HUD.
See §8's newest entry for the full writeup. Previous entry: Phase 6.8 — day/night cycle curve:
`vb::world::DayNightCurve` (a generalized `vector<DayNightKeyframe>`
replacing the old fixed 4-stop gradient tables) + `vb.daynight.set_curve{...}`
+ `S2C_DayNightCurve` (id 51, `kEngineProtocolVersion` 14 → 15) so a pack's
custom sky gradient actually reaches the client; also wired
`day_length_seconds` to both `server.toml` and `vb.daynight.set_day_length(...)`,
the second half of this item's originally-scoped gap. See §8's newest entry
for the full writeup. Previous entry: Phase 6.7 — physics/movement parameters:
`vb.physics.set_params{...}` + `PackRuntime::effective_move_params()`,
`S2C_MoveParams` (id 50, `kEngineProtocolVersion` 13 → 14) so client
prediction mirrors the server's authoritative tunables instead of silently
drifting; also fixed a pre-existing bug found while wiring it — the client
never received the server's `MoveParams` at all before this, and two
`src/client/main.cpp` call sites were stomping it back to hardcoded defaults
right after join. See §8's newest entry for the full writeup. Previous
entry: Phase 6.5 — shared block-damage breaking:
`vb.register_block{max_damage=...}` + `vb::world::BlockDamageSystem` +
`C2S_BlockBreakBegin`/`Stop` + `vb.on("block_break_begin"/"block_break_tick"/
"block_health_tick", ...)`, completion drives the existing `C2S_BlockEdit`
pipeline; `kEngineProtocolVersion` 12 → 13. Crack-texture rendering and the
damage-value replication channel stay deferred (blocked on the texture/atlas
system, see `REMAINING_TASKS.md` 6.5) — see §8's newest entry for the full
design-scoping notes, including why re-registering an existing base block's
`max_damage` silently does nothing (`BlockRegistry::add_or_get` is
idempotent-by-name and never updates an existing entry's properties) and a
test-construction-order gotcha it cost debugging time to find. Previous
entry: Phase 6.4 — generic per-key persistent storage:
`vb.db.get/set/delete` + `vb.crypto.hash`, landed with a hand-rolled
`ScriptDb`/`sha256` instead of the SQLite backend `REMAINING_TASKS.md`
originally floated as the "leading candidate" — see §8's newest entry for
why and what's deferred if that swap is ever wanted. Note: the pre-built
`build-meshing/` ASan directory this file's §3 used to point at no longer
exists on this machine — only `build-net-lua/` remains; re-configure a fresh
ASan build from `cmake/Sanitizers.cmake` if that coverage is needed again.
Previous entry: 2026-09-17, Phase 6.1 — entity kinds as classes: `vb.register_entity`
+ `vb.world.spawn(kind, pos)` now really spawns something, with a persistent
per-instance `self` table and `on_spawn`/`on_tick`/`on_hit`/`on_death` all
wired up — following `ItemDropSystem`'s hardcoded-system-ahead-of-the-generic-
one precedent rather than the spec's EnTT-`ScriptState` design, since Phase
3.1's generic registry is still deferred. See §8's newest entry for the
design-deviation rationale and a test-writing gotcha (don't assert an exact
`self` value right after a spawn+pump — ticks already ran). Previous entry:
Phase 6.6 — player damage/death primitive: generic `player:damage(amount,
cause)` + a `vb.on("player_death", ...)` hook replace the hardcoded
instant-heal-and-teleport `check_respawns()`; `ServerSession` falls back to
the exact old behavior when no pack installs a handler, so every pre-6.6 test
still passes unmodified. See §8's Phase 6.6 entry for the
drop-inventory-at-death-not-respawn-position lesson learned while testing
it. Earlier: Phase 5.5 documentation pass complete —
`CONTRIBUTING.md` added (module map, build/test workflow, wire-message
checklist, Lua-binding guidelines); `docs/lua-api.md`/README's stale claims
fixed; see §8's fourteenth 2026-09-16 entry. Phase 5.1/4.3 — `--singleplayer` now runs the real
content pack: `Singleplayer` (`src/client/main.cpp`) rebuilt around a
directly-owned `LoopbackNetwork`/`ServerSession`/`ClientSession` instead of
`IntegratedGame`, so a real `PackRuntime` (scripting, chat, crafting, item
drops) and `HandshakeServerHost::block_registry` are both wired the same way
the dedicated server does them; see §8's thirteenth 2026-09-16 entry —
also documents a pre-existing, out-of-scope `server_smoke` failure mode
discovered along the way (`VB_WITH_COMPRESSION=ON` + a cwd with no
`content/base`). Phase 5.1 — simple crafting recipes, implemented
entirely as content per explicit user direction: `player:take()` is the only
new engine primitive, `content/base/crafting.lua` (a new generic top-level
pack module `load_content_pack` now knows to load) holds every actual game
rule; see §8's twelfth 2026-09-16 entry. Phase 5.1 — dropped-item entity: a real, working
`vb::world::ItemDropSystem` replicated through the existing interest-grid/
S2C_EntitySnapshot path (no new wire message), `vb.world.spawn_item_drop`,
`content/base/blocks/*.lua` on_break now drops into the world instead of
straight to inventory; see §8's eleventh 2026-09-16 entry. Phase 5.2 —
hold-to-break progress: LMB must be
held on the same voxel for a flat 0.35s before the edit is sent, plus a
screen-space progress bar; see §8's tenth 2026-09-16 entry. Phase 5.1 — real
inventory sync + basic hotbar:
`S2C_Inventory` (107), `PackRuntime::sync_inventory` pushed after
`player:give()`, `ClientSession::inventory()`, text-only HUD hotbar; see §8's
ninth 2026-09-16 entry — also corrected a stale 5.2 checklist entry claiming
the Lua block-edit veto was unwired, when it was actually already implemented.
Phase 5.4 complete — sfx hooks documented as not
implemented, `docs/lua-api.md`; see §8's eighth 2026-09-16 entry. Phase 5.4 —
death/respawn: void-kill Y threshold +
generic `health <= 0` respawn path, no new wire message (reuses `S2C_Chat`
privately); see §8's seventh 2026-09-16 entry. Phase 5.4 — day/night: `S2C_TimeOfDay`,
`vb::world::daynight.hpp` (pure tick/color math), sky-gradient `ClearBackground`
+ HH:MM overlay readout; see §8's sixth 2026-09-16 entry. Phase 5.4 — player
list / join-leave messages: `S2C_PlayerJoin`/`S2C_PlayerLeave`/`S2C_PlayerList`,
`ClientSession::players()`, top-right HUD list; see §8's fifth 2026-09-16
entry. Phase 5.4 — chat:
`C2S_Chat`/`S2C_Chat` wired end-to-end, `vb.on("chat")` veto, HUD chat box;
see §8's fourth 2026-09-16 entry. Phase 5.3 — main menu: `vb::render::MainMenu`
+ an `AppState` machine in `src/client/main.cpp` so the window opens before
any connection attempt in windowed mode; see §8's third 2026-09-16 entry)

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
- **Machine-local build/toolchain/agent-shell gotchas (vcvars64.bat path,
  Bash-vs-PowerShell-tool quirks, which pre-built `build-*` dirs actually
  exist, macOS-specific findings) live in `STATE.md.local`, not here** —
  that file is `.gitignore`d (`*.local`) and specific to whatever physical
  machine an agent session runs on; check it first when setting up a build
  in an agent shell, and add to it rather than here when you hit a new one.

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
- **A local `clang-format --dry-run --Werror` binary may not be trustworthy
  as-is against this repo's `.clang-format`** — see `STATE.md.local` for a
  machine where this was confirmed (a Homebrew clang-format flagging dozens
  of violations even on an untouched `HEAD` file) and the general
  workaround (diff against clang-format run on the unmodified file, don't
  trust a bare pass/fail on the edited one). Re-verify on whichever machine
  you're on before trusting either a pass or a wall of violations.
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

- **Cellulose** (`github.com/RechieKho/cellulose`) — **resolved and reverted,
  see §8's 15th entry.** Its API was inspected and wired in behind
  `VB_WITH_MESHING` (a header-only `greedy_mesh(vector<MeshSample>, ...) ->
  ChunkMesh` free function); it emits vertex data, not GPU buffers, and the
  32³ chunk size works fine with it. It was then reverted after causing the
  NVIDIA-driver VAO/VBO heap-corruption crash — hand-rolled
  `vb::world::chunk_mesher` is the permanent meshing backend now, not a
  placeholder for this.
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
2. ~~Cellulose meshing API shape (blocking spike, see §5).~~ **Resolved and
   reverted (2026-09-16, §8's 15th entry)** — hand-rolled meshing stays
   permanent; see `ARCHITECTURE_SPEC.md` §19 Q2's updated resolution note.
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

- **2026-09-18 — Phase 6.9 inventory stacking landed (uncommitted).**
  `REMAINING_TASKS.md` 6.9: no max stack size existed anywhere —
  `PlayerHandle::give()` always pushed a brand-new slot, never combined into
  an existing one.
  **Where the override lives, and why not `register_item`:** the task doc's
  own suggestion was "let `register_item` override its own stack size," but
  `vb.register_item` never allocates an id space at all (a known, already-
  documented gap — see `content/base/blocks/planks.lua`'s comment) and
  nothing holdable is ever actually a "registered item" in practice, only a
  registered block. Added `BlockType::max_stack`
  (`inc/vb/world/block.hpp`, default = a new `world::kDefaultMaxStackSize`
  constant, 64) instead, overridable via `vb.register_block{max_stack = N}`
  — identical def-parsing shape to 6.5's `max_damage` field
  (`src/script/pack_runtime.cpp`'s `register_block` lambda).
  **New shared helper, not just a `give()` rewrite:** `PackRuntime::Impl::
  give_item(NetId, BlockId, count)` is now the one place that actually
  credits an inventory — fills existing slots holding that item that are
  under `max_stack` first (oldest slot first, first-fit, no attempt at
  optimal packing), then keeps starting new slots sized up to `max_stack`
  for whatever's left. `PlayerHandle::give()` (Lua `player:give()`) and the
  item-pickup handler installed in `attach_session()` (a player walking over
  a dropped item) both now call it — previously two separate hardcoded
  `slots.push_back(...)` call sites that happened to do the same thing by
  coincidence, even though the pickup handler's own existing comment
  claimed pickup "credits their inventory exactly like player:give() does."
  Neither `give_item()` nor its two callers call `sync_inventory()`
  internally — each caller still pushes its own `S2C_Inventory` snapshot
  once afterward, unchanged from before.
  **`take()` untouched:** already worked across multiple slots correctly
  (6.6-era code, see its own test); stacking only changes how slots are
  *created*, not how they're consumed.
  **Same idempotent-registration caveat 6.5 already documented for
  `max_damage` applies here too, not re-litigated:** re-registering an
  existing block name (e.g. one of the base 8) with a different `max_stack`
  silently does nothing, since `BlockRegistry::add_or_get` is idempotent by
  name and never updates an existing entry's properties.
  **Scoped down, deliberately:** no cap on total inventory *slot count* —
  the task item's "stack cap ... and slot count" phrasing reads as one
  thing (how many items fit in one slot) rather than two, and there's no
  other evidence in the spec of an intended separate max-slots limit; not
  added.
  **Verification:** 2 new `tests/unit/pack_runtime_test.cpp` cases — two
  40-count `give()`s of a default-stack item combine into a 64 slot + a
  16-count overflow slot (not four separate ones); a `max_stack = 1` block
  registered via `register_block` keeps two single-item `give()`s as two
  separate slots. Full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 245/245 cases). All 4 CTest cases
  (`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass; both
  binaries rebuild clean. Not verified under a no-Lua/ASan config this
  session (no pre-built ASan dir remains on this machine, per 6.4's entry
  above) — the Lua-specific binding (`register_block`'s `max_stack` field
  parse) is entirely inside `pack_runtime.cpp`'s existing `#if VB_WITH_LUA`
  region; `BlockType::max_stack`'s default member initializer and
  `give_item()`'s logic have no Lua dependency, so the stub-build risk is
  low, just not re-confirmed here.

- **2026-09-18 — Phase 6.16 client-local HUD mechanism landed
  (uncommitted), user-requested.** The user noticed the hold-to-break
  progress bar (5.2) was still hardcoded C++ (`DrawRectangle` calls in
  `src/client/main.cpp`) despite Phase 6's Lua-extensibility push, and asked
  for it to move to Lua per the project's "engine provides raw state, Lua
  deals with presentation" rule.
  **Why a new mechanism was needed, not just a Lua rewrite:**
  `vb::script::UiRuntime`'s existing model (`ui.define(name, fn)` +
  `open()`/`close()`) is for server-pushed *modal* screens (inventory,
  pause) — there's no concept of an always-on overlay independent of that.
  Added one: `ui.define_hud(render_fn)` registers a single render function
  evaluated every UI frame unconditionally (not gated on `is_open()`), with
  its own persistent `state` table that a modal screen opening/closing
  alongside it doesn't touch. `UiRuntime::render_hud()` evaluates it;
  `evaluate_frame()`'s widget-table-to-`Widget` parsing was extracted into a
  shared `widget_from_table()` free function so `evaluate_hud_frame()`
  doesn't duplicate it.
  **New widget type, revised once already (see below): `WidgetType::kRect`.**
  The first pass shipped a `kProgressBar` type (`Widget::value`, drawn via
  raygui's `GuiProgressBar`) — the user immediately called this out as still
  baking a presentation *concept* into the engine (the engine "knows" what a
  progress bar looks like; Lua only supplied a number). Replaced with
  `kRect`: a raw filled rectangle (`fill_r/g/b/a`) with an optional 1px
  outline (`border_r/g/b/a`, alpha 0 = none), drawn with plain
  `DrawRectangle`/`DrawRectangleLines` — no raygui control, no semantic
  meaning at all. `content/base/ui/hud.lua` now builds the bar from *two*
  `rect` widgets (a background+border rect, and a fill rect whose width is
  `bar_w * progress`) itself; the engine has no idea a "progress bar" exists,
  it just draws boxes where it's told to. `widget_from_table()` parses
  `color = {r,g,b,a?}`/`border = {r,g,b,a?}` the same 1-indexed-array
  convention `vb.daynight.set_curve`'s `color` field already established.
  **Raw state relay, the actual "engine provides raw state" part** (this
  half was correct in the first pass and unchanged by the revision): a new
  `client` top-level table in the UI Lua VM (`src/script/ui_runtime.cpp`,
  sibling to the existing `ui` table) exposes `client.break_progress()`
  (nil, or 0..1) and `client.screen_size()` (`{width=.., height=..}`).
  `src/client/main.cpp`'s hold-timer/reach/target-tracking *logic* is
  completely unchanged from 5.2 (still engine-side, since input handling and
  reach validation are gameplay, not cosmetics) — it now just calls
  `ui_runtime.set_break_progress(...)`/`set_screen_size(...)` once per frame
  instead of computing pixel rectangles itself.
  **Second `UiRenderer` instance, not incidental:** drawing both the modal
  screen and the HUD through one `UiRenderer` would thrash its
  per-widget-id text-box/list-selection edit-state caches every single
  frame — `UiRenderer::draw()` clears them whenever the drawn `ui_name`
  differs from the previous call, and a modal name alternating with `"hud"`
  every frame is exactly that. `src/client/main.cpp` now holds
  `ui_renderer` (modal) and `hud_renderer` (HUD) as two separate instances.
  **Real bug found while wiring this in, bigger than the requested
  change:** `--singleplayer` never asset-syncs (no `PackRuntime`/manifest on
  that in-process path, a pre-existing 4.3 gap) — the `enter_playing` lambda
  only ever populated `ui_runtime` from `client->virtual_pack_fs()`, which
  is always empty for singleplayer. This meant `base:pause`/`base:inventory`
  (and now the new hud.lua) silently never loaded in singleplayer at all,
  the single most common dev/test path — not a regression from this
  session's work, but this session's work would have been invisible without
  fixing it, so it was fixed: singleplayer now reads `ui/*.lua` directly off
  `kSingleplayerContentPack` from disk (client and the integrated server
  share one filesystem there, so there's nothing to actually "sync"); the
  real-multiplayer branch is untouched.
  **Verified live twice, not just by unit test** (this repo's Lua UI
  features have no automated GL-context coverage — same limitation 5.2's
  original entry noted): once against the original `kProgressBar` version,
  again after the `kRect` revision — both times launched the real
  `voxel_browser.exe --singleplayer` windowed binary, captured the mouse,
  held LMB on a targeted block, and screenshotted mid-hold. The revised
  version shows the same visual (a light fill growing over a dark
  background+border), now composed from two independent `rect` widgets
  instead of one raygui control; console log both times showed zero "ui
  pack file failed to load" lines (`hud.lua`/`pause.lua`/`inventory.lua` all
  loaded cleanly). Screenshots not saved to the repo.
  **Unit tests**: `tests/unit/ui_runtime_test.cpp` gained 4 cases —
  hud renders nothing until `set_break_progress` is set and hides again on
  `nullopt`, `render_hud()` is a safe no-op when `ui.define_hud` was never
  called, the hud's `state` table persists across calls independent of a
  modal screen's open/close cycle, and the disabled-build stub's
  `set_break_progress`/`set_screen_size`/`render_hud` all no-op cleanly.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`), all 4 CTest cases
  (`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass, plus
  the live windowed run above.
  **Deliberately not attempted:** hud widgets aren't wired to
  `report_click`/`report_change` (no interactive HUD element exists yet to
  need it); the player list/chat box/hotbar (`src/client/main.cpp`'s other
  always-on HUD pieces, predating this session) are still hardcoded C++ —
  only the break-progress bar was in scope. Migrating the rest to
  `ui.define_hud` (replacing `draw_overlay` entirely with a real Lua HUD) is
  a natural, larger follow-up tracked in `REMAINING_TASKS.md` 6.16's last
  bullet, not started here.

- **2026-09-18 — Phase 6.8 day/night cycle curve landed (uncommitted).**
  `sky_brightness()`/`sky_color_for_time()` (`inc/vb/world/daynight.hpp` +
  `src/world/daynight.cpp`) were a fixed 4-keyframe gradient with no Lua
  reach at all; generalized into `vb::world::DayNightCurve` (a
  `vector<DayNightKeyframe>` of `{tick, brightness, color}`) with curve-taking
  overloads of both functions. `default_day_night_curve()` reproduces the
  exact 4 keyframes the old hardcoded tables had, and an *empty* curve passed
  to either overload falls back to it — so every pre-6.8 call site (both the
  existing no-curve overloads, kept as thin wrappers, and `daynight_test.cpp`'s
  existing assertions) is byte-for-byte unaffected.
  **Lua surface**: `vb.daynight.set_curve{keyframes = {{tick=, brightness=,
  color={r,g,b}}, ...}}` (`src/script/pack_runtime.cpp`) stores the raw table;
  `PackRuntime::effective_day_night_curve()` parses it into a
  `world::DayNightCurve` lazily (mirrors `effective_move_params()`'s posture
  of reading `move_params_table` field-by-field on demand rather than
  eagerly), returning `nullopt` if no pack ever called it. Rejects an
  empty/missing `keyframes` table and any call after `freeze()`.
  **Replication, new wire message**: `S2C_DayNightCurve` (id 51,
  `inc/vb/protocol/world.hpp`, `kEngineProtocolVersion` 14 → 15) mirrors
  `DayNightKeyframe` flat (`varint n` + `n × {u32 tick, f64 brightness, u8 r,
  u8 g, u8 b}`) rather than protocol/ depending on world/ — same posture as
  `S2C_MoveParams` mirroring `physics::MoveParams`. Sent between `C2S_Ready`
  and `S2C_JoinAccept` via a new `HandshakeServerHost::day_night_curve` hook
  (`nullopt` default = no frame at all, so a host/test that never opts in
  leaves the client on `default_day_night_curve()`, unchanged) — wired in
  both `src/server/main.cpp` and `--singleplayer`'s `make_singleplayer_host`
  (`src/client/main.cpp`), each reading `pack_runtime.effective_day_night_curve()`
  once and capturing the result by value in the lambda (same pattern
  `host.move_params` already used). `ClientSession::apply_day_night_curve`
  (`src/net/session.cpp`) rebuilds the `world::DayNightCurve` and stores it in
  a new `day_night_curve_` member, exposed via a `day_night_curve()` const
  getter; intercepted unconditionally in `tick()` (not gated on handshake
  state), same reasoning as `S2C_MoveParams`/`S2C_BlockRegistry` — real
  transports don't guarantee cross-lane arrival order relative to
  `JoinAccept`. `src/client/main.cpp`'s sky-clear code now reads
  `vb::world::sky_color_for_time(client->time_of_day(), client->day_night_curve())`
  instead of the bare single-argument overload, so a pack's override actually
  reaches the rendered sky, not just server-side bookkeeping.
  **Second half of the gap, also closed in this session (not originally
  strictly required, but named in the same task item and cheap once the
  curve plumbing existed):** `day_length_seconds` had a real runtime setter
  (`ServerSession::set_day_length_seconds`) that literally nothing ever
  called — every server (dedicated and `--singleplayer` alike) silently ran
  on the hardcoded `1200.0` member-initializer default with no way to change
  it. Extracted that literal to a new `vb::net::kDefaultDayLengthSeconds`
  constant (`inc/vb/net/session.hpp`) so `ServerSession`'s own default and
  `--singleplayer`'s host (which has no `server.toml` to read a config value
  from) can't drift apart. Added `ServerConfig::day_length_seconds` (`server.toml`,
  same default) as the config-layer base; `vb.daynight.set_day_length(seconds)`
  (rejects `seconds <= 0`) overrides it via a new
  `PackRuntime::effective_day_length_seconds(base)` — the exact same
  config-then-pack-override shape 6.7 established for `gravity`/
  `vb.physics.set_params`, not a new pattern. `src/server/main.cpp` calls
  `session.set_day_length_seconds(pack_runtime.effective_day_length_seconds(
  config.day_length_seconds))`; `Singleplayer`'s constructor body calls the
  equivalent with `vb::net::kDefaultDayLengthSeconds` as the base.
  **Deliberately not attempted:** a pack-supplied arbitrary curve *function*
  (a Lua callback re-evaluated per read) — every other Phase 6 "default +
  override" item ships data (a table of values), not an executable hook
  re-invoked from the replication path, and piecewise-linear keyframes can
  already approximate most shapes with enough points.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 240/240 cases, up from 230 at 6.7's
  count — this session's 4 new `daynight_test.cpp` cases, 4 new
  `pack_runtime_test.cpp` cases, 1 new `protocol_test.cpp` case, and 1 new
  `config_test.cpp` case). All 4 CTest cases
  (`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass. Not
  verified under a no-Lua/ASan config this session (no pre-built ASan dir
  remains on this machine, per 6.4's entry) — the Lua-specific bindings are
  entirely inside `pack_runtime.cpp`'s existing `#if VB_WITH_LUA` region
  (with a matching stub returning `base`/`nullopt` in the `#else` branch,
  compiled but not exercised here), and every other changed file
  (`daynight.cpp`, `world.cpp`, `handshake.cpp`, `session.cpp`) has no Lua
  dependency at all, so the no-Lua stub-build risk is low, just not
  re-confirmed here.

- **2026-09-18 — Phase 6.7 physics/movement parameters landed
  (uncommitted).** `vb.physics.set_params{...}` (`src/script/pack_runtime.cpp`)
  lets a pack override any `physics::MoveParams` field, per
  `REMAINING_TASKS.md` 6.7 and the struct's own long-standing "a Lua pack
  overrides per entity kind" comment.
  **Scoped to one global override, not per-entity-kind, on purpose:** no
  entity kind besides the player runs `step_movement` today (6.1's script
  entities have no physics at all) — a per-kind table would have nowhere
  else to apply. `PackRuntime::effective_move_params(base)` reads the raw
  `sol::table` field-by-field with `get_or(name, base.field)`, so a field the
  pack never set falls back to whatever `base` the caller passed in, not a
  fixed literal.
  **`ServerConfig.gravity` reconciliation, decided:** it's the *base*
  `effective_move_params()` is called with (`move_params.gravity =
  config.gravity` before the override runs, `src/server/main.cpp`) — the
  operator's `server.toml` sets the engine default, an explicit pack
  override wins over it if the pack sets `gravity` itself.
  **The actual bug this session found, bigger than the task as scoped:**
  auditing "does the client's prediction ever see this" turned up that it
  never did, for *any* value of `MoveParams` — not just a hypothetical
  future pack override, but `ServerConfig.gravity` itself. Every client
  (dedicated-server and `--singleplayer` alike) constructs its own
  `physics::MoveParams{}` locally and only ever calls
  `ClientSession::set_move_params()` with that hardcoded default; nothing
  on the wire carried the server's actual tunables. Client-side prediction
  running with the wrong gravity mostly hides behind
  `ClientSession::reconcile()` snapshotting the position back to server
  truth every tick, so this was invisible without measuring — worth
  remembering as a category: a value that only affects *prediction* (not
  correctness, since the server is authoritative regardless) can silently
  diverge for a long time before anyone notices the jitter.
  **Fix — new wire message**: `S2C_MoveParams` (id 50,
  `inc/vb/protocol/world.hpp`, `kEngineProtocolVersion` 13 → 14) mirrors
  `physics::MoveParams` flat (13 `f64` fields + `bool fly`) rather than
  protocol/ taking a dependency on physics/ — same posture as
  `BlockRegistryRecord` mirroring `world::BlockType`. Sent between
  `C2S_Ready` and `S2C_JoinAccept` alongside `S2C_BlockRegistry`/
  `S2C_KeybindRegistry` via a new `HandshakeServerHost::move_params` hook
  (`nullopt` default = no frame, so every existing host/test is unaffected).
  `ClientSession::apply_move_params` (handled unconditionally in `tick()`,
  same as block/keybind registry, since real transports don't guarantee
  cross-lane ordering) converts it straight into `move_params_`.
  **Second bug found while wiring the fix in, not just reasoned about:**
  two call sites in `src/client/main.cpp` (`run_headless`'s single-connection
  path and the windowed `enter_playing` lambda) constructed a *fresh*
  default `physics::MoveParams` and called `client->set_move_params()` with
  it right after checking `join_accept()` — i.e., *after* `S2C_MoveParams`
  had already arrived and been applied (it travels in the same handshake
  step as `JoinAccept`), immediately overwriting the real value back to the
  hardcoded default. Neither call site needed to construct a MoveParams at
  all; both were just trying to get *some* value into a local variable used
  for eye-height math. Fixed by adding a `ClientSession::move_params()`
  const getter and reading that back instead
  (`move_params = client->move_params();`) — the general lesson: once a
  session applies something unconditionally as soon as it arrives, any
  later "set it up for use" code must read it back, not reconstruct a
  default and reassert it.
  **`--singleplayer` wiring**: `Singleplayer`'s in-process host needed the
  same `host.move_params` hook as the dedicated server
  (`make_singleplayer_host`, `src/client/main.cpp`) — added a `move_params`
  member (declared right after `pack_runtime`, so member-init order lets it
  be computed via `pack_runtime.effective_move_params(vb::physics::
  MoveParams{})` before `server` is constructed) and an explicit
  `server.set_move_params(move_params)` call in the body (the dedicated
  server already did this; the integrated server previously didn't call
  `set_move_params` at all, silently running server-side physics on
  `MoveParams{}`'s hardcoded defaults too — not just a client-side gap).
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 230/230 cases, up from 225 — this
  session's 2 new `protocol_test.cpp` cases, 2 new `block_registry_test.cpp`
  cases, and 2 new `pack_runtime_test.cpp` cases). All 4 CTest cases
  (`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass. Ran
  the real `voxel_browser.exe --headless --singleplayer` binary directly
  (not just ctest) and confirmed the log line `received move params
  (gravity=28)` appears right after join, proving the new wire path fires
  in the actual integrated-server path, not just in a unit test's
  `LoopbackNetwork`.
  **Not done, deliberately deferred:** per-entity-kind override (no second
  physics-driven kind exists to need it, see above); 6.8-6.13's other
  "default + override" items (day/night curve, inventory stacking, chat
  transform, item-drop params, anim clip priority, read-only config
  visibility) are separate, untouched follow-ups in the same phase-6 shape.

- **2026-09-18 — Phase 6.5 shared block-damage breaking landed
  (uncommitted).** `vb.register_block{max_damage=N}` (default 0 = today's
  instant break) opts a block into a shared damage pool per
  `ARCHITECTURE_SPEC.md` §10.7 / `REMAINING_TASKS.md` 6.5.
  **Scoped down from the full spec on purpose:** the spec's design has the
  damage *value* itself ride the interest/replication system as a transient
  entity so nearby players see cracks form. That value has exactly one
  consumer (a crack overlay), which is itself blocked on the still-missing
  texture/atlas system (4.3/5.1 — client renders untextured cubes), so this
  session implemented only the begin/tick/complete *mechanism* —
  `vb::world::BlockDamageSystem` (`inc/vb/world/block_damage.hpp` +
  `src/world/block_damage.cpp`, pure/dependency-free/unit-tested, same
  posture as `ItemDropSystem`) tracks `pos -> {damage, max_damage,
  last_touched_tick, contributors}`, but its `changed`/`cleared`
  `BlockDamageTickResult` fields are computed and then ignored by
  `ServerSession::update_block_damage` — there's no wire message to put them
  on yet. Wiring that up is the natural next step once a client exists to
  show it to.
  **New wire messages**: `C2S_BlockBreakBegin{pos, face}` /
  `C2S_BlockBreakStop{pos}` (ids 48/49, `inc/vb/protocol/world.hpp`) bracket
  a player holding a target; `BlockRegistryRecord` also gains `max_damage`.
  `kEngineProtocolVersion` 12 → 13 (`cmake/version.hpp.in`,
  `docs/protocol.md`).
  **Server flow**: `ServerSession::handle_block_break_begin` gates entry —
  the same reach check `apply_block_edit` uses (extracted to a new public
  `WorldReplicator::in_reach`, since `C2S_BlockBreakBegin` doesn't itself
  mutate the world the way `apply_block_edit` does) plus an engine-level
  `max_damage > 0` check, then a `vb.on("block_break_begin", ...)` veto via
  a new `ServerSession::BlockBreakHooks` struct (same `std::function`-seam
  pattern as `BlockEditHooks`/`set_chat_handler`/etc. — no sol2 in
  `session.hpp`). `ServerSession::update_block_damage()` (called from
  `tick()` alongside `update_item_drops`) drives `vb.on("block_break_tick",
  ...)`/`vb.on("block_health_tick", ...)` once per server tick per
  damaged block; on completion (summed damage reaches `max_damage`) it
  synthesizes a `C2S_BlockEdit{kBreak}` and calls the *existing*
  `WorldReplicator::apply_block_edit` — the same `on_break`/`S2C_ChunkDelta`
  path a manually-sent edit uses, attributed to whichever player happened to
  be contributing when it completed (`BlockDamageSystem::CompletedBreak`
  captures one, arbitrary among concurrent "breaking together" contributors,
  at the moment `states_` erases the entry — captured *before* erasure since
  the completing tick's contributor list wouldn't otherwise survive past
  `tick()`'s return).
  **`vb.on` semantics, not veto-shaped**: `block_break_tick`/
  `block_health_tick` return numbers, not booleans — multiple concurrent
  *players* contributing sum their deltas per the spec, and this
  implementation also sums multiple *handlers* registered for the same
  event the same way (an orthogonal case the spec didn't call out;
  summing rather than picking one avoids silently dropping a registered
  handler's contribution). `block_health_tick`'s chain instead takes the
  *last* handler's non-nil return, mirroring `run_player_input`'s
  replacement-chaining style.
  **Idempotent-registration trap hit while writing the integration test,
  not just reasoned about:** `BlockRegistry::add_or_get` (existing code,
  unchanged) returns an already-registered name's id unchanged — it never
  applies the new `BlockType` passed in. Re-registering `"base:grass"` with
  `max_damage = 3` in a test does *nothing*, since `base:grass` already
  exists from `BlockRegistry::base()`. Fixed by registering a brand-new
  block name (`"test:crumbly"`) instead and placing it into the world
  directly (`world.set_block`, bypassing worldgen, which only ever emits
  the 8 base ids) — worth remembering for any future test (or pack) that
  wants to change a property of one of the base 8 blocks specifically:
  it silently won't take effect the way `vb.register_item`/`vb.register_biome`
  callers might expect from the "idempotent by name" framing.
  **Construction-order trap, also hit while writing the test:** `World`
  takes `BlockRegistry` *by value* (copies it at construction) — a new block
  a pack registers via Lua only exists in a `World`'s internal registry copy
  if the `World` is constructed *after* `PackRuntime::freeze()`, matching
  `src/server/main.cpp`'s real order. The existing integration tests in this
  file construct `World` *before* `PackRuntime`/`load_pack_file`/`freeze()`
  and get away with it only because they never register a genuinely new
  block id (just re-register existing base names for `on_break`/veto
  purposes, which doesn't need `World`'s registry copy to know about it) —
  copy this file's *newest* two tests' construction order, not the older
  ones, for anything that needs a Lua-registered block to actually exist in
  the live world.
  **Test-content-loading trap** (unrelated to the above, cost a separate
  debugging round): checking `world.solid_at(pos)` for a not-yet-loaded
  chunk silently returns `false` (unloaded reads as air) — a
  `REQUIRE(world.solid_at(far_away_pos))` placed *before* moving a player
  near it and pumping enough ticks to load that chunk fails with no hint
  why. The existing "vetoes a block break" test in this same file already
  gets this right (moves the player first, pumps, *then* asserts) — a new
  test copied the assert-then-move order by mistake and failed until
  reordered to match.
  **Not done, deliberately deferred** (all noted inline in
  `REMAINING_TASKS.md` 6.5 too): `crack_texture` field on `BlockType` (no
  texture/model fields exist on it at all yet, nowhere to put it); the
  damage-value replication channel (see the scoping note above); any
  content in `content/base` using `max_damage` (every base block keeps
  `max_damage == 0`, so the existing 5.2 client-side 0.35s hold-to-break
  timer in `src/client/main.cpp` is completely untouched and still governs
  every real block in the shipped game today); no client UI sends
  `C2S_BlockBreakBegin`/`Stop` yet, though `ClientSession::
  send_block_break_begin`/`send_block_break_stop` are real, tested wire
  calls ready for one.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 225/225 cases, up from 215 at
  6.4's count — the delta is this session's `block_damage_test.cpp` (7
  cases) + 2 new `protocol_test.cpp` cases + 2 new
  `pack_runtime_integration_test.cpp` cases); `voxel_browser`/
  `voxel_browser_server` also rebuilt clean. Not verified under a no-Lua/
  ASan config this session (`build-meshing/` no longer exists on this
  machine, per 6.4's entry above) — the Lua-specific bindings are entirely
  inside `pack_runtime.cpp`'s existing `#if VB_WITH_LUA` region, and the
  `ServerSession`/`WorldReplicator`/`BlockDamageSystem` changes have no Lua
  dependency at all, so the no-Lua stub-build risk is low, just not
  re-confirmed here.

- **2026-09-18 — Phase 6.4 generic per-key persistent storage landed
  (uncommitted).** `vb.db.get(key)`/`vb.db.set(key, value)`/
  `vb.db.delete(key)` + `vb.crypto.hash(data)`, per `ARCHITECTURE_SPEC.md`
  §10.6/§10.7 and `REMAINING_TASKS.md` 6.4.
  **Backend decision, deliberately not SQLite:** the task doc called SQLite
  "the leading candidate" but also explicitly flagged it as "implementation
  detail, not a design blocker." Added `vb::script::ScriptDb`
  (`inc/vb/script/db.hpp` + `src/script/db.cpp`) instead: one file per key,
  content-addressed by `sha256(key)` under a 2-hex-prefix shard dir
  (`<content_pack>/db/<prefix>/<hash>`) — the exact same on-disk shape
  `vb::assetsync::ClientAssetCache` already uses (§4.4, `src/assetsync/
  cache.cpp`), write-to-`.tmp`-then-`rename()` included. Rationale: no new
  dependency (SQLite meant either FetchContent-ing an amalgamation build or
  another package-manager landmine like GNS's protobuf, §5), the spec's own
  scaling complaint was specifically about `vb.storage`'s single JSON blob
  (one record *shared* by every key) — a real filesystem entry per key
  already solves that — and the spec gives `vb.db` no list/enumerate/query
  surface, so nothing here actually needs SQL. If a future feature needs
  range queries, transactions, or listing all keys, swapping the backend is
  a pure `ScriptDb`-internal change; nothing in `PackRuntime`'s Lua bindings
  would need to move.
  **`vb.crypto.hash`**: a from-scratch, dependency-free SHA-256
  (`inc/vb/core/sha256.hpp` + `src/core/sha256.cpp`, `vb::core::sha256_hex`)
  rather than pulling in a crypto library — same "small and self-contained
  beats a new dependency" call as the backend above, and it's also what
  `ScriptDb` uses internally for key-to-filename hashing (one algorithm,
  two call sites). Verified against three NIST/RFC test vectors (empty
  string, `"abc"`, the 56-byte SHA-256 vector) in `sha256_test.cpp` — the
  vectors were pulled from .NET's `SHA256.ComputeHash` run locally via
  PowerShell (cross-checked against Python's `hashlib.sha256`, not
  hand-typed from memory) rather than trusted from recall, since a
  self-authored implementation being checked against a self-typed "known"
  vector proves nothing if the vector itself is wrong.
  **Construction-order trap avoided, not hit:** `PackRuntime::Impl::db`'s
  root path is derived from `storage_path.parent_path()`, so `db` is
  declared *after* `storage_path` in the `Impl` struct (member init order
  follows declaration order, not initializer-list order) and initialized
  from `storage_path` itself in the ctor body, not from the constructor's
  `path` parameter after it's already been `std::move`'d into
  `storage_path` — moving-from-and-then-reading `path` again would've been
  UB-adjacent (unspecified-but-valid state, not guaranteed unchanged).
  **Values round-trip through the same `json_to_lua`/`lua_to_json` helpers
  `vb.storage` already uses** (`pack_runtime.cpp`, anonymous namespace), so
  `vb.db.set(key, {level = 3})` persists a real table, not just strings —
  consistent with `vb.storage`'s existing behavior, unlike a naive
  string-only KV store.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 215/215 cases — up from 197 at the
  last count in this file, Phase 6.1's entry above; the delta is this
  session's `sha256_test.cpp` (5 cases) + `script_db_test.cpp` (7 cases) +
  2 new `pack_runtime_test.cpp` cases, plus whatever landed in 6.2/6.3
  between those counts that this entry doesn't re-derive). Not verified
  under a no-Lua/ASan config this session — `build-meshing/` (the pre-built
  ASan dir prior entries used for that) no longer exists on this machine;
  `db.cpp`/`sha256.cpp` don't touch sol2 at all though, so the no-Lua stub
  build risk is low, just not actually re-confirmed here.
  **Not done:** no content uses this yet (same "mechanism before content"
  posture as every other Phase 4/6 primitive) — `content/base` has no login
  flow. `vb.db` has no TTL/expiry and no enumeration, matching the spec
  exactly (get/set/delete by an already-known key only).

- **2026-09-17 — Phase 6.1 entity kinds as classes landed (uncommitted).**
  `vb.world.spawn(kind, pos)` now actually does something: it creates a
  spawned instance whose `self` is a persistent Lua table (`ScriptState` in
  spirit) stored in `PackRuntime::Impl::entities`
  (`src/script/pack_runtime.cpp`), fires `on_spawn(self)` once, then
  `on_tick(self, dt)` once per `dispatch_tick` for as long as it's alive.
  **Design deviation from `ARCHITECTURE_SPEC.md` §7.1/§7.2, deliberate:** the
  spec assumes an EnTT-backed `ScriptState` component driven by real
  `ScriptPreTickSystem`/`ScriptPostTickSystem` classes — neither exists.
  Phase 3.1's generic registry wiring is still deferred (see §2 below), the
  same gap 5.1's `ItemDropSystem` hit first; this item followed that exact
  precedent again rather than building the general system: a small
  hardcoded map, replicated through the interest grid exactly like a dropped
  item (`ServerSession::spawn_script_entity`/`set_script_entity_state`/
  `remove_script_entity`, `src/net/session.cpp`, literally copy-pasted from
  `spawn_item_drop`'s shape). `self`'s base-component accessors
  (`get_pos`/`set_pos`/`get_kind`/`damage`/`remove`) reach it via a shared
  metatable's `__index`, not a usertype — arbitrary fields (`self.hp = 10`)
  live directly on the table, unlike `PlayerHandle` which is rebuilt fresh
  every call and has nowhere to persist anything.
  **`on_hit` has no built-in trigger** — the engine tracks no health at all
  for generic entities (matching 6.5's still-unbuilt "engine takes no
  position" stance on damage). `self:damage(amount, cause)` just fires
  `on_hit` as a notification; a pack wanting mob HP owns that entirely on
  `self` and calls `self:remove(cause)` (which fires `on_death` then
  despawns) itself.
  **Trap that cost a debugging round in the integration test:** don't assert
  an exact `self` field value right after `pump(N)` following a spawn — the
  spawning chat message doesn't land on the server until a tick or two into
  the pump, and every remaining tick in that same `pump` call already runs
  `on_tick` before the assertion, so e.g. an `hp` seeded to `10` and
  incremented by `dt` each tick is already `> 10` by the time you can check
  it, not exactly `10`. Assert a baseline (`> 10`, or capture the value into
  a Lua global) and compare against *that* after more ticks instead of a
  literal.
  **Test-running gotcha hit this session, unrelated to the code:** driving
  MSVC through `vcvars64.bat` from an agent shell via `cmd.exe /c '<path>'`
  silently hangs forever if the path is wrong (e.g. a bash `/tmp/...` path
  that doesn't resolve to the same location under `cmd.exe`) — no error, no
  output, just sits there looking like a slow compile. Write the batch file
  to (and invoke it from) a real Windows path (e.g. the scratchpad dir under
  `C:\Users\...\AppData\Local\...`), not bash's `/tmp`. `cmd /c '"<vcvars>"
  && ninja ...'` chained in one line via the PowerShell tool worked reliably
  where the two-line-batch-file-via-Bash-tool approach didn't.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 197/197 cases) and on `build-meshing`
  (`VB_WITH_LUA=OFF`, ASan build, 166/166 cases, no ASan findings — confirms
  the `ServerSession` engine-side change alone, without any Lua bindings,
  compiles and behaves under the no-Lua config too, same as 6.6's
  verification pattern).
  **Existing test updated:** `pack_runtime_test.cpp`'s
  "`vb.world.spawn` on an unregistered kind" case asserted the old stub's
  silent-no-op-returns-nil behavior; changed to assert it now raises a Lua
  error, matching `vb.world.set_block`'s existing "reject, don't silently
  swallow" convention for an unknown id.
  **Not done:** no client-side rendering branches on `EntityKind` yet (same
  pre-existing limitation `world::kItemDropKind` already has — every remote
  entity draws the same placeholder billboard); no automatic despawn-on-
  zero-health since no generic health primitive exists.

- **2026-09-17 — Phase 6.6 player damage/death primitive landed
  (uncommitted).** `player:damage(amount, cause)` (`PlayerHandle::damage`,
  `src/script/pack_runtime.cpp`) → `ServerSession::damage_player`
  (`src/net/session.cpp`) is now the one way to reduce a player's `Health`
  from Lua; the void-kill check is just another call into the same
  `apply_damage()` helper with `cause = "void"`, not a separate code path.
  `check_respawns()` no longer hardcodes the outcome (instant full heal,
  teleport to the join spawn point, a fixed chat line) — it calls a
  `ServerSession::set_respawn_handler` callback (unset = the exact old
  behavior, verified by `netcode_test.cpp`'s pre-existing void-kill test
  passing unmodified) and `PackRuntime::attach_session` wires one in only
  when a pack actually registered `vb.on("player_death", ...)`.
  **Bug caught by the new integration test, not just reasoned about:** the
  first version of `drop_inventory = true` handling spawned the dropped
  items at the handler's *returned* respawn position (`decision.pos`) —
  since the player is teleported to that exact same position in the same
  tick, `ItemDropSystem`'s pickup radius immediately picked the items right
  back up, so the inventory never actually looked empty to the client. Fixed
  by capturing the player's position *before* `check_respawns()` overwrites
  it (i.e. where they actually died) and dropping there instead
  (`run_respawn_handler` in `src/script/pack_runtime.cpp`). Worth
  remembering for any future code that both teleports a player and spawns
  something at "their" position in the same tick — the interest-grid pickup
  systems don't know or care which one happened first.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 196/196 cases) and on `build-meshing`
  (`VB_WITH_LUA=OFF`, ASan build, 166/166 cases — confirms the stub-build
  path and the engine-side change alone, without any Lua bindings, both
  still compile and pass).
  **Not done:** no content uses this yet — `content/base` has no
  `death.lua`, so the shipped base pack still gets the built-in fallback
  behavior (same "mechanism before content" posture as every other Phase
  4/5 item). Fall damage, PvP, mob damage, hunger are all still separate,
  unstarted follow-ups (`REMAINING_TASKS.md` 6.6's last bullet) — this item
  only adds the primitive and the decision hook, not any of the systems that
  would call them.

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
  - Cellulose's `greedy_mesh` was later wired into exactly this seam
    (`mesh_chunk_from_snapshot`'s call site inside `ChunkMeshWorkerPool::
    mesh`) and then reverted — its more volatile per-edit vertex/index
    counts defeated this entry's headroom/`UpdateMeshBuffer()` mitigation
    and reproduced the driver crash it exists to avoid. See §8's 15th entry
    (2026-09-16) for the full story; `VB_WITH_MESHING` is unused.

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
  **This local box's build-dir matrix** (useful to know before picking one
  instead of rediscovering it via CMakeCache.txt greps): `build/` — Ninja,
  RelWithDebInfo, everything off (headless-only, no Lua/net). `build-lua/` —
  Ninja, Debug, `VB_WITH_LUA=ON`, **no client target** (raylib not fetched,
  `VB_BUILD_CLIENT` off) — only `voxel_browser_server`/`vb_core`/`vb_tests`
  exist here. `build-net/`, `build-release/`, `build-asan-nonet/` — Visual
  Studio 18 2026 generator (multi-config, pass `--config Debug`/`Release`),
  `VB_WITH_LUA=OFF`; `build-net` has `VB_WITH_NET=ON` + a real client
  target, the other two don't (check `CMakeCache.txt` before assuming).
  None of these combine `VB_WITH_LUA=ON` with a real client — see above.

  **Small, separate finding while staging this commit:** `.gitignore` and
  `tests/CMakeLists.txt` are checked in with CRLF line endings (`file`
  confirms it; every other text file checked, including every new file this
  session, is LF-only) even though `core.autocrlf=input` is set locally —
  `git add` prints "CRLF will be replaced by LF the next time Git touches
  it" for both on every `git add`, harmlessly for a content-only edit but
  worth knowing before a real edit to either file: the first edit through a
  CRLF-preserving tool will silently flip the whole file's line endings in
  that diff, burying the actual change under a wall of noise. No
  `.gitattributes` exists to force this repo-wide either way. Not fixed
  here (out of scope for a content-pack pass, and normalizing either file
  produces a noisy diff of its own) — if it's ever worth cleaning up,
  `git add --renormalize <path>` after adding a `.gitattributes` rule is
  the low-noise way, not a manual line-ending find/replace.

- **2026-09-16 — Phase 5.3 main menu landed (uncommitted).** New
  `vb::render::MainMenu` (`inc/vb/render/main_menu.hpp` +
  `src/render/main_menu.cpp`) draws menu/settings/connecting/error screens
  with plain raygui calls (no Lua — engine-level per spec §5.3, distinct from
  `UiRuntime`/`UiRenderer` which draw *pack* screens post-join). See
  `REMAINING_TASKS.md` 5.3 for the full feature list.
  **The real structural change** is in `src/client/main.cpp`: it used to
  block on a full connect-then-handshake loop *before* ever constructing a
  `Window` — no way to show a menu, a connecting screen, or recover from a
  failed connect without the process exiting. Windowed mode now creates the
  `Window` first and runs an explicit `AppState{kMenu, kSettings,
  kConnecting, kPlaying, kError}` machine in the frame loop; `--headless` was
  deliberately left as a byte-for-byte-unchanged `run_headless()` function
  (the old blocking body, verbatim) since CI's only coverage of this file is
  the 3 headless smoke tests and none of them should need to change for a
  windowed-only feature.
  **New `vb::core::save_client_config()`** (`config.hpp`/`.cpp`) — toml++
  serializes a fresh `toml::table` from `ClientConfig` and overwrites the
  file; regenerates from scratch, doesn't preserve comments in a hand-edited
  `client.toml` (acceptable for a Settings-screen save, not attempted to fix
  by e.g. re-parsing-and-patching the existing file in place).
  `ClientConfig::recent_servers` already existed as a parsed-but-never-used
  field since Phase 1.1's config loader — this is the first thing to
  actually read and write it.
  **Verified:** clean `-DVB_WARNINGS_AS_ERRORS=ON` build of
  `voxel_browser`/`voxel_browser_server`/`vb_tests` on `build-asan-nonet`
  (MSVC + ASan), full `ctest` green (4/4, same one N/A here since this is the
  non-net build — no GNS UDP-bind test to hit the sandbox limitation at
  all), and a real windowed launch + full-screen screenshot confirming the
  main menu lays out correctly (title panel, player name / address / port
  fields, Connect / Play Singleplayer / Settings / Quit buttons all where
  the layout math says they should be, no overlap). **Not verified:**
  clicking through to Settings/Connecting/Error — a scripted-input attempt
  (PowerShell `SetForegroundWindow` + synthetic `mouse_event` clicks at the
  Settings button's screen coordinates) took a second screenshot still
  showing the main menu, i.e. the click didn't land. Most likely
  `SetForegroundWindow` silently failing (a well-known Windows restriction:
  a background process generally can't steal foreground focus from another
  process without the target's cooperation) rather than an app bug — raygui
  buttons here use the exact same `GuiButton`-returns-true-on-click pattern
  already exercised by `UiRenderer`/`pack_runtime_integration_test.cpp`, and
  the state-machine wiring around each button (`begin_connect`,
  `menu.open_settings(config)`, etc.) was code-reviewed, not just
  typed-and-hoped. Worth an actual manual click-through before trusting the
  non-main screens blind; if it's ever automated again, try
  `AllowSetForegroundWindow`/attaching input queues (`AttachThreadInput`)
  instead of a bare `SetForegroundWindow` call from an unrelated process.
  **Known gaps, not attempted (see REMAINING_TASKS.md 5.3 for the full
  list):** no byte-progress bar on the connecting screen (asset-sync never
  grew progress-fraction accounting, 4.4's pre-existing gap); window
  width/height/vsync changes apply on restart only, not live-resized;
  keybindings are still hardcoded, no rebind UI.

- **2026-09-16 (4th): Phase 5.4 chat** — `C2S_Chat` (100) landed (it was
  reserved in `MessageType` since Phase 4.2 but never given a struct);
  `kEngineProtocolVersion` bumped 7 → 8. The interesting part wasn't the
  codec, it was that `PackRuntime::dispatch_chat`/`vb.on("chat", handler)`
  already existed (Phase 4.2) with **nothing to call it** — same
  shipped-mechanism-ahead-of-content pattern as most of Phase 4/5.1. Closed
  it the same way 4.5's `C2S_UiEvent`/`set_ui_event_handler` did:
  `ServerSession::set_chat_handler(function<bool(NetId, string_view)>)`
  mirrors `set_ui_event_handler`'s shape (optional callback, unset = default
  behavior) but returns a veto bool instead of firing a one-way event —
  `ServerSession::handle_chat` calls it, and only if it returns true (or
  isn't set at all) does `ServerSession` itself format `"<name>: <text>"`
  and broadcast `S2C_Chat` to every playing connection. Formatting the
  "name: text" line server-side (not client-side, not left to Lua) was a
  deliberate small choice: it means `--singleplayer` (no `PackRuntime`, no
  handler ever set) gets working chat for free instead of needing its own
  formatting path — "unset = allow" was picked specifically so chat isn't a
  pack-gated feature.
  `ClientSession` gained `send_chat(text)` (thin wrapper, same shape as
  `send_ui_event`) and `take_chat_messages()` (drains a `vector<string>`,
  same drain-on-call convention as `take_open_ui()`).
  **Client HUD** (`src/client/main.cpp`, `kPlaying` state only): Enter opens
  a `GuiTextBox` in permanent edit mode (not the click-to-toggle pattern
  `UiRenderer`/`MainMenu` use elsewhere, since there's nothing else to click
  — chat only has one field) and releases mouse capture the same way an
  open pack UI already does (`ui_runtime.is_open() || chat_open` now both
  gate it); Enter again sends + closes, Escape cancels without sending. A
  plain `std::deque<std::string>` caps the visible log at 8 lines,
  bottom-left, above the input box when open. No raygui state (edit-mode
  bool, buffer) needed persisting across frames beyond `chat_buf` itself,
  unlike `UiRenderer`'s per-widget-id maps — there's exactly one field.
  **Verified:** `tests/unit/protocol_test.cpp` round-trip;
  `tests/unit/netcode_test.cpp` new case proves a real broadcast over
  `LoopbackTransport` reaches both clients including the sender, and that an
  empty line is silently dropped server-side (not broadcast, not even to
  the sender) rather than sent as a blank `S2C_Chat`;
  `tests/unit/pack_runtime_integration_test.cpp` new case proves the Lua
  veto path end-to-end (a `vb.on("chat", ...)` that checks `player:get_name()`
  actually suppresses one player's line while the other's still gets
  through). Full `ctest` green (4/4) on `build-asan-nonet`; clean
  `-DVB_WARNINGS_AS_ERRORS=ON` build of all three targets; a real windowed
  `--singleplayer` launch ran a few seconds without crashing or an ASan
  report (not a full manual click-through of the chat box itself — same
  `SetForegroundWindow` automation limitation noted in the Phase 5.3 entry
  above applied here too, not re-attempted).
  **Not attempted:** rate limiting / flood guard (`ServerSession` has none —
  a malicious client can spam `C2S_Chat` freely, same unaddressed gap
  `handle_input_batch` has for input flooding, tracked in Phase 1.3/3.2's
  "belongs with `GnsTransport`" notes); `/`-prefixed commands; timestamps;
  chat history isn't part of `S2C_JoinAccept` so a late joiner sees nothing
  said before they connected.

- **2026-09-16 (5th): Phase 5.4 player list / join-leave messages** — three
  new server-generated (no Lua involvement) message types:
  `S2C_PlayerJoin` (104), `S2C_PlayerLeave` (105), `S2C_PlayerList` (106);
  `kEngineProtocolVersion` bumped 8 → 9. Landed in `chat.hpp`/`chat.cpp`
  alongside chat rather than a new file — same "social RPC" grouping those
  files already used. `ServerSession` sends `S2CPlayerList` (everyone else
  already playing) to a connection the instant it finishes joining, then
  `S2CPlayerJoin` to everyone *else* already playing; a disconnect broadcasts
  `S2CPlayerLeave` to whoever's left. Both new broadcast loops sit right next
  to the existing `joins_`/`leaves_.push_back(...)` bookkeeping in
  `ServerSession::tick` (that vector was always server-internal only, for
  `take_joins()`/`take_leaves()` callers like the dedicated server's log
  lines — this is the first time join/leave became a real wire event).
  `ClientSession` keeps a `unordered_map<NetId, string> players_` synced from
  all three message types, exposed as `players()`. Join/leave **share the
  chat log**, not a separate channel: `apply_gameplay_frame`'s new
  `kS2CPlayerJoin`/`kS2CPlayerLeave` cases both push a
  `"* <name> joined/left the game"` line into the same `pending_chat_` that
  backs `take_chat_messages()` — no second HUD widget needed for that half,
  since 5.4's chat log (landed a few hours earlier, same day) already
  existed and a join/leave notice is conceptually the same kind of
  transient line. **Design trap this caused, fixed:** two pre-existing chat
  tests (`netcode_test.cpp`'s broadcast test, `pack_runtime_integration_test
  .cpp`'s veto test) join two clients back-to-back without draining chat in
  between, then assert `take_chat_messages().size() == 1` right after
  sending — the new "* Bob joined the game" line (Bob joins moments after
  Alice, both still mid-`pump()`) was silently included, failing both with
  an off-by-one. Fixed by draining `take_chat_messages()` once right after
  both `REQUIRE(...joined())` and before the real assertions in both tests —
  the correct fix, not a workaround: a real client would already have
  rendered/cleared that system line before the player goes on to type
  anything, so draining it first matches actual usage.
  `src/client/main.cpp`: a small always-visible player list, top-right
  corner (own name highlighted, everyone from `client->players()` below it).
  Deliberately **not** gated behind a hold key — Tab is already bound to
  mouse-capture release (see 5.3's HUD/menu bindings), and there's no
  spare key doing nothing else right now; always-on is simpler than adding
  a new binding for one pass.
  **Verified:** `tests/unit/protocol_test.cpp` new round-trip case for all
  three structs (including an empty `S2CPlayerList`, mirroring
  `S2CBlockRegistry`'s empty-list case); a new `tests/unit/netcode_test.cpp`
  case joins Alice alone first (drains her empty player list), then joins
  Bob and asserts Bob's list already contains Alice, Alice got the join
  broadcast + `"* Bob joined the game"` chat line, and disconnecting Bob
  (`tb.close(*idb, "left")`, the same trigger-the-server's-kDisconnected-
  event pattern `pack_runtime_integration_test.cpp` already used) removes
  him from Alice's `players()` and produces `"* Bob left the game"`. Full
  `ctest` green (4/4) on `build-asan-nonet` after fixing the two pre-existing
  tests above; clean `-DVB_WARNINGS_AS_ERRORS=ON` build of all three targets.
  Not re-verified with a live windowed launch this pass (the HUD list is a
  straightforward `DrawText` loop over an already-tested data source,
  `client->players()` — same judgment call as not re-clicking through
  5.3's menu screens every session).
  **Not attempted:** no distinct "system message" visual styling beyond the
  `"* "` prefix (still the same log/`take_chat_messages()` stream as real
  chat, no separate colour or channel); player list carries no extra
  per-player metadata (ping, idle time, `net_id` itself isn't shown in the
  HUD even though it's in the map).

- **2026-09-16 (6th): Phase 5.4 day/night cycle** — `S2C_TimeOfDay` (46);
  `kEngineProtocolVersion` bumped 9 → 10. `S2C_JoinAccept::time_of_day` has
  existed since Phase 1.3 (spec §8.3's handshake diagram always included it)
  but was pure dead weight until now: no server ever advanced it (`JoinGrant
  ::time_of_day` defaulted to 0 and nothing touched it after), and there was
  no message to keep an already-connected client's copy current even if it
  had. Same "shipped mechanism, no content" pattern as most of Phase 4/5.
  **New pure-logic header**, `inc/vb/world/daynight.hpp` +
  `src/world/daynight.cpp` — deliberately no raylib dependency (same
  reasoning as `vb/render/entity_visual.hpp`: keeps it unit-testable without
  a GL context). `kTicksPerDay = 24000` (0 = sunrise, 1/4 = noon, 1/2 =
  sunset, 3/4 = midnight — picked to match the existing `S2CJoinAccept`
  doc-comment "ticks into the day cycle" and give round numbers, not lifted
  from anywhere in the spec, which doesn't pin an exact tick count).
  `advance_time_of_day(current, dt, day_length_seconds)` does the
  accumulate-and-wrap math; `sky_brightness`/`sky_color_for_time` are a
  simple 4-keyframe (sunrise/noon/sunset/midnight) lerp — "simple sky
  gradient" per the spec line, not a physically based sky model.
  **Server:** `ServerSession` gained a `double time_of_day_ticks_`
  accumulator (double, not the wire `u32`, so slow real-time accrual doesn't
  get truncated to zero every tick), advanced once per `tick()` via
  `advance_time_of_day`; `set_day_length_seconds(seconds)` (default 1200.0
  == 20 real minutes/day, arbitrary but a common game-y pace, no spec
  number to match). The constructor's existing `on_ready` wrapper (the one
  that already fills in `net_id`/`world_seed` if a user hook left them at
  their zero-ish defaults — Phase 1.3) now also unconditionally sets
  `grant.time_of_day` from the live accumulator, so every join gets the
  *current* time, not whatever a test/host happened to return. Broadcasts
  `S2C_TimeOfDay` to every playing connection roughly once a second
  (`kTimeOfDayBroadcastIntervalSeconds`, a plain accumulator next to the
  existing `kAssetSendBudgetPerTick`-style anonymous-namespace constants) —
  deliberately coarser than snapshots/world state, since a clock only needs
  to *look* smooth, doesn't need per-tick precision over the wire.
  **Client:** `ClientSession::time_of_day()` returns
  `join_accept()->time_of_day` until the first `S2C_TimeOfDay` lands, then
  the latest broadcast value (`std::optional<uint32_t> time_of_day_override_`)
  — same "unset falls back to the handshake's own copy" shape as
  `players()`/chat needed no such fallback (those start genuinely empty,
  this one has a real seed value from the handshake itself).
  `src/client/main.cpp`: in the `kPlaying` state only, right before
  `BeginMode3D`, a second `ClearBackground` call (raylib allows multiple
  per frame; only the state actually drawn after it matters) overwrites
  `Window::begin_frame()`'s flat dark clear with
  `sky_color_for_time(client->time_of_day())`. `draw_overlay()` gained an
  "HH:MM" line, computed from the convenient fact that 24000 ticks/24h is
  exactly 1000 ticks/hour (`time_of_day / 1000`, `(time_of_day % 1000) *
  60 / 1000`) — no floating point needed for the readout.
  **Verified:** `tests/unit/daynight_test.cpp` (new, 8 cases: rate matches
  `day_length_seconds`, wraps at `kTicksPerDay`, a misconfigured
  zero/negative day length freezes instead of NaN/dividing by zero,
  brightness peaks at noon and dims toward midnight, both wrap cleanly past
  `kTicksPerDay`, color interpolates smoothly not in discrete jumps);
  `tests/unit/protocol_test.cpp` round-trip; a new
  `tests/unit/netcode_test.cpp` case (day length sped up to 100s/day so the
  test doesn't need to wait 1200s) proving a joined client's `time_of_day()`
  advances past its join-time value once the periodic broadcast lands (with
  a **known sharp edge, deliberately not over-asserted**: the client only
  ever reflects the *last broadcast*, not the server's continuously-ticking
  clock, so the test checks `<=` against `server.time_of_day()`, not `==` —
  an early draft asserted equality and flaked on timing) and that a second
  client joining later gets a strictly later `JoinAccept.time_of_day` than
  the first, proving the grant is live-filled per join. Full `ctest` green
  (4/4) on `build-asan-nonet`; clean `-DVB_WARNINGS_AS_ERRORS=ON` build of
  all three targets. Not re-verified with a live windowed launch this pass
  (same judgment call as the two 2026-09-16 entries above it — the actual
  sky-color math is unit-tested in isolation, and the render-loop glue is a
  two-line `ClearBackground`/`DrawText` call over an already-tested value).
  **Not attempted:** no ambient-light/mob-spawning gameplay coupling
  (cosmetic only this pass); no Lua binding to read/set the day length or a
  starting time_of_day (`set_day_length_seconds` is C++-only,
  `PackRuntime`/`vb.` has nothing for it); the sky is a flat
  `ClearBackground` fill, not a skybox/sun/moon/star render.

- **2026-09-16 (7th): Phase 5.4 death/respawn** — no new `MessageType`, no
  `kEngineProtocolVersion` bump (the only wire traffic is an existing
  `S2C_Chat` sent to one connection instead of broadcast, which the codec
  already supported — `send_message` has always taken a single `ConnId`,
  broadcasting is just something callers choose to loop over, see
  `handle_chat`'s loop vs. this feature's single `send_message` call).
  `ServerSession::Conn` gained two new fields: `spawn_pos` (captured once,
  at the same point `move.position` is already seeded from `JoinGrant` at
  join) and `health` (plain `float`, default 20 to match `ecs::Health`'s
  default — but this is *not* wired to that component; Phase 3.1's EnTT
  registry is still deferred, `ServerSession` still drives players directly,
  so `health` here is just more ad-hoc `Conn` state next to `move`/`look`,
  same category as everything else in that struct).
  **Design choice, and why:** the respawn trigger is `health <= 0`, checked
  generically every tick in a new `check_respawns()` — not a narrower
  `position.y < void_kill_y` branch that respawns directly. The actual only
  producer of damage this pass *is* the void check (`if (position.y <
  void_kill_y) health = 0`), but routing it through the generic health path
  means a future combat/fall-damage system gets a working respawn for free
  by just decrementing `health` — no `check_respawns()` change needed. This
  mirrors the "mechanism ahead of content" pattern the rest of Phase 4/5
  keeps repeating, but inverted: here the *content* (void-kill) arrived
  with a slightly wider *mechanism* (generic low-health respawn) than the
  content alone needed, deliberately, because the wider version was barely
  more code.
  `void_kill_y` is configurable (`ServerConfig::void_kill_y` /
  `server.toml`'s new `void_kill_y` key / `ServerSession::set_void_kill_y`),
  default -64.0 — an arbitrary "comfortably below any sane terrain" guess,
  **not validated against every worldgen seed's actual floor**; wired into
  `src/server/main.cpp` next to the existing `gravity` config wiring.
  `--singleplayer`'s `IntegratedGame`/`ServerSession` never calls
  `set_void_kill_y`, so singleplayer just gets the same -64.0 built-in
  default — untested whether that's ever reachable by falling through
  unloaded terrain (Phase 3.3's ground-load freeze should prevent that
  specific case, per its own entry above).
  On respawn: `health` reset to 20, `move` reset to a fresh `MoveState{}`
  with `position = spawn_pos` (velocity zeroed too, not just position —
  otherwise a player who died mid-fall would respawn still carrying
  terminal-velocity downward momentum and immediately re-trigger the void
  check next tick), `interest_.upsert(...)` updated so *other* players see
  the teleport on their very next snapshot rather than waiting for the
  respawned player's next input batch to refresh `interest_` (input batches
  are the normal path that keeps `interest_` current, per `handle_input_batch`
  — respawn needed its own explicit update since it happens independent of
  any input arriving), and a `S2C_Chat{"* you died and respawned"}` sent
  with a plain single-`ConnId` `send_message` call (not the broadcast loop
  chat/join/leave all use) so only the dying player sees it — deliberately
  no killfeed/broadcast-to-everyone this pass, spec line just says "with
  spawn point", nothing about visibility to others.
  **Client needed zero new code.** `S2C_EntitySnapshot.local` already carries
  whatever `state.move.position` is server-side each tick (Phase 3.4's
  reconciliation, `broadcast_snapshots` in `session.cpp`), and the client
  already unconditionally snaps its predicted position to `local` on every
  snapshot and replays only its *unacked* inputs on top. A respawn is just
  an unusually large position jump through a pipeline that already existed
  for ordinary lag-compensation corrections — confirmed this by not touching
  `src/client/main.cpp` or `ClientSession` at all for this feature, only
  `ServerSession`/`ServerConfig`.
  **Verified:** new `tests/unit/netcode_test.cpp` case — sets
  `void_kill_y` to `spawn.y - 5.0` (not a hardcoded absolute number, so the
  test doesn't depend on whatever height worldgen happened to pick for its
  fixed seed), flies the player straight down past it via `kInputFlyDown`,
  asserts `server.player_move_state(id)->position.y` snapped back to
  spawn height (`Approx(spawn.y)`, not still sitting below the void
  threshold), and asserts `"* you died and respawned"` shows up via
  `take_chat_messages()`. `tests/unit/config_test.cpp` gained a
  `void_kill_y` TOML-parsing assertion in its existing values test. Full
  `ctest` green (4/4) on `build-asan-nonet` (first attempt, no failures to
  fix this time — day/night's two 2026-09-16 entries above both needed a
  fix-up round, this one didn't); clean `-DVB_WARNINGS_AS_ERRORS=ON` build
  of all three targets. Not re-verified with a live windowed launch (same
  judgment call as the three 2026-09-16 entries above it).
  **Not attempted:** no fall damage for a survivable fall (binary
  instant-kill-or-nothing at the void threshold, no damage curve); no
  broadcast/killfeed of someone else's death; `Health`'s `max` field
  (20 here is a bare literal matching `ecs::Health::max`'s default, not
  read from that component or anywhere configurable) — a pack wanting a
  different max HP has no lever to pull yet.

- **2026-09-16 (8th): Phase 5.4 sfx hooks — documented as not implemented,
  closing out Phase 5.4.** Pure documentation, no code: added an "Audio /
  sfx — not implemented" section to `docs/lua-api.md` (right before its
  existing "Sandbox" section). Confirmed, rather than assumed, three facts
  before writing it: `cmake/Dependencies.cmake` really does build raylib
  with `SUPPORT_MODULE_RAUDIO OFF`; `ARCHITECTURE_SPEC.md` §10.5's
  block-break event-flow diagram really does say "sfx trigger" (in the
  `on_break` step) with nothing else in the spec backing it up as a real
  API; and `REMAINING_TASKS.md`'s existing "Deferred (post first-playable)"
  list already had "Audio subsystem + Lua sfx/music API" as its own line —
  this was tracked, just not documented anywhere a Lua-API reader would
  see it. All three now cross-reference each other. This was the last open
  item in Phase 5.4 — see the `### 5.4 Play polish  ✅ done (2026-09-16)`
  status line and paragraph in `REMAINING_TASKS.md` for the four-item
  session summary (chat, player list/join-leave, day/night, death/respawn,
  sfx docs).

- **2026-09-16 (9th): Phase 5.1 real inventory sync + basic hotbar, plus a
  stale-checklist correction for Phase 5.2's Lua veto.** Picked up
  `REMAINING_TASKS.md` 5.1's "no wire message syncing inventory contents to
  the client at all" gap.
  **Correction made along the way, worth flagging so it doesn't get
  re-"discovered" as a task later:** `REMAINING_TASKS.md` 5.2 still read "Lua
  veto + region protection: seam left, waits on Phase 4.2" as an open item.
  Checked the actual code before starting new work on it (per this file's own
  "verify before recommending" discipline) and found it was **already fully
  wired** — `BlockEditHooks` (`inc/vb/net/world_replicator.hpp`),
  `PackRuntime::attach_world`/`on_block_edit_before` (`src/script/
  pack_runtime.cpp`), and `src/server/main.cpp` calling `attach_world`, with
  an existing passing integration test
  (`pack_runtime_integration_test.cpp`'s "pack script vetoes a block break
  and observes on_break"). This was presumably wired during Phase 4.2 itself
  and the 5.2 checklist entry was simply never updated after. No dedicated
  "region protection API" exists (`ARCHITECTURE_SPEC.md §14`'s one mention of
  the phrase) and none was added — a pack veto handler already receives the
  player + block position, so a claims/region check is just Lua reading
  `vb.storage` inside the same `vb.on("block_break"/"block_place")` handler;
  judged not worth a dedicated C++ API unless a real pack needs one.
  Corrected the checklist text and status paragraph in `REMAINING_TASKS.md`
  to match reality; no code changed for this half.
  **New work — real inventory:** `S2C_Inventory` (107) —
  `inc/vb/protocol/inventory.hpp` + `src/protocol/inventory.cpp`
  (`InventorySlot{item, count}`, `varint n` + `n × {u16, u16}`), round-trip
  tested. `kEngineProtocolVersion` bumped 10 → 11 (`cmake/version.hpp.in`,
  `docs/protocol.md`). `PackRuntime::Impl::sync_inventory(NetId)`
  (`src/script/pack_runtime.cpp`) builds a full snapshot from
  `inventories[id]` and sends it via the same `conn_for_player` +
  `net::send_message` pattern `send_message`/`open_ui` already used; called
  from `PlayerHandle::give()` right after mutating the slot vector — the only
  inventory mutator that exists, so "sync after give" covers every current
  write path. Client side: `ClientSession::inventory_` (new member,
  `inc/vb/net/session.hpp`) + a new `kS2CInventory` case in
  `apply_gameplay_frame` (`src/net/session.cpp`), exposed read-only via
  `ClientSession::inventory()`. Deliberately a full-resend snapshot on every
  change, not a delta — same posture `S2C_PlayerList` already established,
  and inventories are small (a handful of slots) so there's no real cost to
  resending the whole thing.
  **New work — hotbar:** `src/client/main.cpp`, drawn right before the
  `ui_runtime.is_open()` block (same place the player list / chat HUD blocks
  already live): one box per `client->inventory()` slot, bottom-center,
  block name (`chunk_store().registry().get(item).name`, falls back to "?"
  for an id the client's current registry doesn't recognize) + count as
  plain `DrawText` — no slot-select input, no icons (every block is still
  textureless per 4.3/5.1's own long-standing gap, so there's nothing to draw
  an icon *from* yet), matching `ui/inventory.lua`'s existing text-only
  posture rather than inventing new visual language ahead of real art.
  **Tests:** `tests/unit/protocol_test.cpp` ("inventory round-trips,
  including an empty snapshot") and a new
  `tests/unit/pack_runtime_integration_test.cpp` case ("player:give() pushes
  a live S2C_Inventory to the client") — drives a real
  `ServerSession`/`ClientSession` pair over `LoopbackTransport`, triggers
  `give()` from a `vb.on("chat", ...)` handler (chosen because it's the one
  existing veto callback that already hands a real `PlayerHandle` — 4.2's own
  noted gap is that `player_join` only hands a name, not a handle, so that
  seam couldn't be reused here), and asserts `client.inventory()` matches
  after a tick pump.
  **Verification, three separate build trees (this repo has several
  pre-configured, see the top of §3):** `build-lua` (`VB_WITH_LUA=ON`,
  `VB_BUILD_CLIENT=OFF`) — full `vb_tests` green, 196/196 cases, including
  both new ones; `build-asan-nonet` (`VB_WITH_LUA=OFF`, `VB_BUILD_CLIENT=ON`,
  ASan) — confirms `voxel_browser`/`voxel_browser_server` compile clean with
  the new `ClientSession::inventory()` member and hotbar drawing code (this
  is the tree that actually has a client target; `build-lua` here doesn't),
  full `ctest` green (`vb_tests` 162/162 + all three smokes). Not yet done:
  no live windowed screenshot of the hotbar (no GL context available in this
  environment, same limitation noted throughout 5.3/5.4) — rests on the
  clean build + the fact its drawing code follows the exact same
  `DrawText`/`DrawRectangle` pattern the already-verified player-list HUD
  block uses right above it.

- **2026-09-16 (10th): Phase 5.2 hold-to-break progress.** Picked up
  `REMAINING_TASKS.md` 5.2's last remaining checklist line, "Break progress
  (hold-to-break): not yet."
  **Change, entirely in `src/client/main.cpp`, no protocol/server change:**
  the block-break click handler swapped `IsMouseButtonPressed` for
  `IsMouseButtonDown` and gates sending `C2S_BlockEdit` behind a new
  `breaking`/`break_target`/`break_progress` trio of locals — LMB must stay
  held on the *same* `look_hit.voxel` for a flat `kBreakSeconds` (0.35,
  chosen to feel roughly like the old instant-break but give the new
  progress bar something to visibly fill) before the edit actually goes out;
  switching to a different voxel or releasing the button resets progress to
  0, and losing mouse capture (menu/chat open) resets it too. Server-side
  validation/veto (already done, this session's 9th-adjacent correction
  above) is completely unaffected: the wire message this sends is byte-for-
  byte the same `C2SBlockEdit`, just deferred until the hold completes, so
  nothing downstream needed touching. Placing (RMB) stays instant, untouched.
  A small filled progress bar (plain `DrawRectangle`, no raygui) is drawn
  screen-space below the would-be crosshair position while `breaking` is
  true — there's still no actual crosshair sprite/reticle anywhere in the
  client; deliberately didn't add one here to keep this change scoped to
  just the one checklist item, though a future pass adding a reticle should
  probably anchor the bar to it instead of a bare screen-center offset.
  **Deliberately NOT attempted (separately tracked, not this item):**
  per-block hardness or tool-dependent break time — every block takes the
  same 0.35s regardless of type; `REMAINING_TASKS.md`'s own "Tool/hardness
  times: not yet" line (right above this one in the same section) already
  covers that as a distinct, larger follow-up (would need a `BlockType`
  field, `S2C_BlockRegistry` wire changes, and a tool/item concept that
  doesn't exist yet — out of scope for a hold-to-break pass).
  **Verification:** `build-asan-nonet` (the tree with `VB_BUILD_CLIENT=ON`)
  — a clean recompile of `voxel_browser` at `/W4` produced no new warnings
  (checked with `-clp:WarningsOnly`, only the pre-existing unrelated ASan
  `/INCREMENTAL`-ignored linker warning appeared); full `ctest` green (all
  four cases, including the three smokes) on the same tree. No windowed
  screenshot of the progress bar itself (no GL context here, same limitation
  as every other HUD element added this session) — rests on the clean build
  plus the fact the drawing code is the same `DrawRectangle`/
  `DrawRectangleLines` shapes the hotbar (9th entry, same session) already
  used successfully.

- **2026-09-16 (11th): Phase 5.1 dropped-item entity.** Picked up
  `REMAINING_TASKS.md` 5.1's last remaining item: `entities/dropped_item.lua`
  registered `base:dropped_item` declaratively but nothing ever spawned one —
  block drops went straight into the breaking player's inventory
  (`ctx.player:give()`), never through a world entity.
  **Key design decision, made before writing any code:** does this need
  Phase 3.1's EnTT registry? No. Re-read `vb::replication::InterestGrid`
  (`inc/vb/replication/interest.hpp`) and `ServerSession::broadcast_
  snapshots()` (`src/net/session.cpp`) closely and confirmed both are
  already fully generic over `NetId` + `EntityKindId` — nothing in the
  interest grid, the snapshot builder, or the client's `EntityRenderer`
  (Phase 3.5) assumes an entity is a player. The "no generic entity system"
  gap `register_entity`/`vb.world.spawn` keep citing is specifically about
  nothing calling a *pack's* `on_spawn`/`on_tick` Lua callbacks — the
  replication/rendering plumbing underneath was already entity-kind-agnostic
  from Phase 1.4/3.5. This meant a hardcoded, non-Lua-driven drop system
  could piggyback on that plumbing for free, with zero protocol changes.
  **New pure module:** `vb::world::ItemDropSystem`
  (`inc/vb/world/item_drops.hpp` + `src/world/item_drops.cpp`) — no net/
  script dependency, so unit-testable standalone
  (`tests/unit/item_drops_test.cpp`, 4 cases: id-space separation, pickup
  radius, lifetime expiry, independent multi-drop tracking). `spawn()`
  allocates ids from `0x8000'0000` upward specifically so they can never
  collide with `ServerSession::next_net_id_`'s player ids (which start at 1
  and count up by one per join) — the two counters never need to
  coordinate. `tick(dt, players)` is a pure function: ages every drop,
  checks position against every given player, returns `{pickups, removed}`
  for the caller to apply; it doesn't touch replication or inventories
  itself.
  **ServerSession wiring:** `spawn_item_drop(pos, item, count)` (new public
  method, `inc/vb/net/session.hpp`) upserts the new drop straight into
  `interest_` with a reserved `world::kItemDropKind` sentinel
  (`EntityKindId{0xFFFF}`, chosen to sit above any pack-registered kind,
  which count up from 1) — from that point on it's indistinguishable from a
  player to the replication/broadcast code, so **no new S2C message was
  needed at all**, and the client's existing `EntityRenderer` draws it as a
  tinted billboard with zero client-side changes. `update_item_drops()`
  (called once per tick, right after `check_respawns()`) builds the
  players-for-pickup-check list from `interest_.get(state.net_id)->pos`
  rather than `Conn::move.position` directly -- deliberate, so pickup
  detection works identically whether a player's position came from real
  input-driven movement (`handle_input_batch`, which upserts both) or the
  test/script-facing `set_player_state()` (which only upserts `interest_`,
  never touches `Conn::move`) — caught this distinction while writing the
  integration test below, before it became a real bug: an early draft read
  `state.move.position` and pickups silently never fired when a test moved
  a player via `set_player_state`. New `set_item_pickup_handler` callback
  (same `std::function`-based, no-Lua-dependency shape as
  `set_chat_handler`/`set_ui_event_handler`) — unset means picked-up items
  just vanish, matching "no handler, no side effect" everywhere else.
  **PackRuntime wiring:** `world_tbl["spawn_item_drop"]` (new Lua binding,
  `src/script/pack_runtime.cpp`) calls `session->spawn_item_drop` directly —
  deliberately a *separate* binding from `world_tbl["spawn"]`, which stays
  the inert `vb.register_entity`-kind path waiting on 3.1; conflating the two
  would have implied item drops are pack-entity-kind-driven, which they
  aren't. `PackRuntime::attach_session` gained a `set_item_pickup_handler`
  wire that pushes straight into `inventories[player]` +
  `sync_inventory(player)` — the exact same two calls `PlayerHandle::give()`
  makes, so a pickup is indistinguishable from a script handing the item to
  you directly (this session's 9th-entry inventory-sync work, reused as-is).
  **Content pack:** all six `content/base/blocks/*.lua` on_break handlers
  (`dirt`/`grass`/`leaves`/`sand`/`stone`/`wood`) swapped `ctx.player:give()`
  for `vb.world.spawn_item_drop({...ctx.pos + 0.5...}, id, 1)` — breaking a
  block now drops a real, visible item at that position instead of an
  instant inventory credit. `entities/dropped_item.lua`'s comment rewritten
  to explain the two systems don't share any code (the registration is
  kept purely as a placeholder for whenever a pack might want custom
  per-drop `on_tick` behavior through the real future EnTT path).
  **Tests:** `item_drops_test.cpp` (pure, 4 cases, see above) plus a new
  `pack_runtime_integration_test.cpp` case ("vb.world.spawn_item_drop
  replicates to a client and is picked up on approach") driving a real
  `ServerSession`/`ClientSession` pair over `LoopbackTransport` through all
  three states: too far to be visible, visible but out of pickup range, and
  collected (entity gone from `remote_entities()`, item credited to
  `client.inventory()`). `content_pack_test.cpp`'s existing "content/base
  loads cleanly" case (unmodified) confirms the six edited Lua files still
  parse/register without needing any change to that test, since it never
  exercised `on_break`'s body, only registration.
  **Verification, both build trees again (see the 9th entry for why two are
  needed):** `build-lua` — full `vb_tests` green, 201/201 (196 baseline + 4
  new `ItemDropSystem` cases + 1 new integration case), including a
  `content/base` re-parse. `build-asan-nonet` (`VB_WITH_LUA=OFF`) — confirms
  `ItemDropSystem`/`ServerSession`'s new non-Lua-gated code (the class
  itself, `spawn_item_drop`, `update_item_drops`, the pickup-handler
  plumbing) compiles clean with the Lua binding code compiled out entirely;
  clean build of `voxel_browser`/`voxel_browser_server`/`vb_tests`, full
  `ctest` green (all 4 cases). Not yet done: no live two-window playtest
  actually watching an item drop render and get picked up (no GL context
  here, same limitation as every other visual feature this session).

- **2026-09-16 (12th): Phase 5.1 simple crafting recipes — deliberately
  engine-agnostic, per explicit user direction.** The user asked for
  crafting but was explicit up front: "it should be in content as example
  since voxel browser is a generic voxel game browser." That framing decided
  the whole design before any code was written — this project's engine is
  meant to stay a generic host, with every actual game rule living in a
  content pack (`content/base` is documented, `ARCHITECTURE_SPEC.md` §16, as
  "the reference implementation of the Lua API," not a hardcoded ruleset).
  **What's engine-side, and why each piece earns that:**
  1. `PlayerHandle::take(itemstack) -> bool` (`src/script/pack_runtime.cpp`)
     — the symmetric counterpart to the already-existing `give()`. Removes
     up to `count` of `item` across however many inventory slots hold it,
     all-or-nothing (no partial consumption on an under-supply — sums
     availability across slots first, only mutates if the full amount is
     confirmed available). This is a generic inventory primitive exactly
     like `give()` already was; the engine has no idea it's being used for
     "crafting" specifically, the same way it has no idea `give()` is used
     for "picking up a mined block."
  2. `src/script/pack_loader.cpp`'s `load_content_pack` now also loads any
     other `*.lua` file sitting directly at a pack's root (sorted,
     `init.lua` excluded and always handled last) — after `blocks/`/
     `entities/`/`biomes/`, before `init.lua`. This is what let
     `crafting.lua` exist as a real, loadable file at all, without engine
     code special-casing "crafting" as a known content category the way
     `blocks`/`entities`/`biomes` already are. A pack could just as easily
     drop `weather.lua` or `economy.lua` there. Purely additive — no
     existing pack had a loose root-level `.lua` file besides `init.lua`,
     so no other pack's load order changes.
  **Everything else — actual game rules — is content, in
  `content/base/crafting.lua`:** the recipe list (a plain local Lua table,
  not read back from the engine's write-only `vb.register_craft` capture —
  `register_craft` is still called per recipe purely so the engine-side
  record exists for a hypothetical future consumer, e.g. a crafting-table
  UI's item grid), the matching logic (sum each required item across
  `player:get_inventory()`, an already-existing primitive), and the trigger
  (`/craft <name>` over chat, vetoing the raw command text so it never
  broadcasts, whether or not the craft actually succeeds). Two new
  craftable-only blocks back the wood → planks → sticks chain this session
  chose as the example (matching `REMAINING_TASKS.md`'s own suggested
  example verbatim): `content/base/blocks/planks.lua` (solid) and
  `blocks/sticks.lua` (`solid = false` — the closest available
  approximation to "not really a wall," since held items and placeable
  blocks still share one `BlockId` space; a separate item-id space is a
  bigger, still-open 5.1 gap this didn't touch).
  **A subtlety worth remembering if this needs revisiting:** `run_veto`
  (`src/script/pack_runtime.cpp`) calls each registered handler for an event
  in registration order and returns `false` **immediately** on the first
  handler that returns `false` — it does not call every handler
  unconditionally. This meant the test-only "give me some wood" chat handler
  added in `content_pack_test.cpp`'s new integration test had to be written
  so it only reacts to its own exact trigger text and returns `true`
  (pass-through) otherwise — if it had returned `false` unconditionally, or
  been registered *before* `crafting.lua`'s handler while both cared about
  overlapping text, one could have silently starved the other. Not a new
  finding (existing chat-veto tests already relied on the same behavior),
  but easy to trip over when stacking a second handler onto a pack that
  already has one, as this test does.
  **Tests:** `tests/unit/pack_runtime_test.cpp` — "player:take() removes
  items across slots, all-or-nothing" exercises the engine primitive in
  isolation via `PackRuntime::dispatch_chat()` directly (no `ServerSession`
  needed at all: `give`/`take`/`get_inventory` never touch the network
  layer). Written so a wrong Lua-side result actually fails the C++ `CHECK`
  — an earlier draft had the Lua handler `assert()` internally and always
  `return true`, which would have silently passed even on a wrong `take()`
  result, since a Lua error inside a veto handler is caught and warned by
  `run_veto`, not treated as a veto itself; rewritten so the handler returns
  the real outcome and the C++ side asserts on that. `tests/unit/
  content_pack_test.cpp` — a new "content/base crafting: wood -> planks ->
  sticks via /craft chat" case loads the *real* `content/base` files through
  a real `ServerSession`/`ClientSession` pair over `LoopbackTransport` and
  drives the full example end-to-end: missing-ingredients rejection (no
  inventory change), a successful two-recipe chain with exact before/after
  counts at each step, an unknown-recipe rejection, and confirms an ordinary
  chat message still broadcasts normally alongside the command handling.
  Also had to update that file's pre-existing "content/base loads cleanly"
  registry-size assertion (`base_size + 2`, was `base_size`) since planks/
  sticks are genuinely new blocks, not `add_or_get` re-declarations of the
  Phase 2 base set the way every other `blocks/*.lua` file's registration
  is.
  **Verification, both build trees again:** `build-lua` — full `vb_tests`
  green, 203/203 (201 baseline + 1 new `take()` unit case + 1 new crafting
  integration case). `build-asan-nonet` (`VB_WITH_LUA=OFF`) — confirms both
  engine changes compile clean: `PlayerHandle::take` is inside the
  Lua-gated half of `pack_runtime.cpp` and compiles out with the rest of it,
  while the `pack_loader.cpp` change is plain `std::filesystem` code with no
  Lua dependency at all and is exercised either way; clean build of all
  three targets, full `ctest` green (all 4 cases). Not yet done: no live
  two-window playtest of `/craft` actually being typed into the real HUD
  chat box (no GL context here, same limitation as every other
  content/gameplay feature this session) — rests on the integration test
  driving the identical `ServerSession`/`ClientSession` code path a live
  client's chat box would.

- **2026-09-16 (13th): `--singleplayer` now runs the real content pack —
  found while writing this session's README update, not asked for
  directly.** Drafting a "try crafting in `--singleplayer`" quick-start line
  for `README.md` prompted actually checking whether that claim was true
  before writing it down (per this file's own "verify before recommending"
  discipline) — it wasn't. `src/client/main.cpp`'s `Singleplayer` struct
  wrapped `vb::net::IntegratedGame` with no `PackRuntime` anywhere in
  sight, meaning `--singleplayer` had *always* run on the hardcoded
  `BlockRegistry::base()` set with zero Lua scripting, going all the way
  back to whichever session first wrote `Singleplayer` — chat, crafting,
  item drops, everything this session and the two before it built, only
  ever worked for real multiplayer (`--server`), never singleplayer,
  despite `--singleplayer` being the easiest way for a first-time user (or
  agent) to try any of it.
  **Root blocker, and why `IntegratedGame` couldn't just grow a
  `PackRuntime` parameter:** a `PackRuntime` needs the raw server-side
  `net::Transport&` (to send chat/give/item-drop messages), which
  `IntegratedGame` never exposes (its `LoopbackNetwork net_` member is
  private, only `server()`/`client()` accessors exist) — and separately,
  `PackRuntime::install_join_veto(host)` must run **before**
  `ServerSession` is constructed (it wraps `host.authenticate`, and
  `ServerSession` copies `host` by value at construction), which
  `IntegratedGame`'s single all-in-one constructor gives no hook for: by
  the time a caller could reach in, `ServerSession` already exists.
  **Fix:** rebuilt `Singleplayer` to construct `LoopbackNetwork`/
  `ServerSession`/`ClientSession` directly — the exact same pieces
  `IntegratedGame` wraps, just not through it — mirroring exactly how
  `tests/unit/pack_runtime_integration_test.cpp` already does this for
  every PackRuntime-involving test (that's *why* those tests don't use
  `IntegratedGame` either, in hindsight — worth remembering next time
  something here reaches for `IntegratedGame` and also wants scripting).
  Member declaration order (which decides C++ initializer-list *execution*
  order, not the order written) had to be gotten right: `net` →
  `registry` → `pack_runtime` (loads + freezes `content/base` into
  `registry` before anything else touches it) → `world`/`pool` (built from
  the now pack-extended `registry`) → `server` (built from a host that
  `make_singleplayer_host` has already run `install_join_veto` +
  `block_registry` on, using the same `pack_runtime`/`registry`). Two new
  free-function factories (`make_singleplayer_pack_runtime`,
  `make_singleplayer_host`) exist specifically so this ordering could
  happen entirely inside a member-initializer list rather than needing a
  two-phase-construction workaround.
  **A second, smaller gap fixed at the same time:** `host.block_registry`
  (`HandshakeServerHost`, Phase 4.3) was never wired for singleplayer
  either — without it, a joining client stays on its own `base()` registry
  regardless of what the server-side one grew to, so `crafting.lua`'s
  `planks`/`sticks` blocks would exist server-side (inventory give/take
  works fine, since that's just numeric ids) but resolve to nothing
  client-side — the hotbar (this session's earlier work) would have shown
  "?" for them. Mirrors `src/server/main.cpp`'s own `host.block_registry`
  callback line-for-line.
  **New `Singleplayer::tick(dt)`** centralizes what used to be scattered
  `sp->game.tick(dt)` calls across `main.cpp` (`run_headless`, the
  windowed connecting-loop, and the main playing loop — 6 call sites) —
  now also drains `take_joins()`/`take_leaves()` into
  `dispatch_player_join_completed`/`dispatch_player_leave` and calls
  `pack_runtime.dispatch_tick(dt)` every tick, exactly mirroring
  `src/server/main.cpp`'s own loop body so a pack behaves identically
  whether a real dedicated server or this in-process one drives it. Every
  `sp->game.tick(dt)`/`sp->game.client()` call site became
  `sp->tick(dt)`/`sp->client()` (mechanical, via `sed`, then verified by
  full rebuild — no behavior change intended or found at any site).
  **Degrades gracefully, not fatally**, if `content/base` isn't found
  relative to the working directory (logs a warning, keeps running with
  the hardcoded base block set) — deliberately different from the
  dedicated server's fatal-on-broken-pack posture (`src/server/main.cpp`
  exits on a bad content pack), since a first `--singleplayer` run
  shouldn't hard-fail just because of where it happened to be launched
  from.
  **Verified with an actual headless run, not just "it compiles":**
  `voxel_browser --headless --frames 5 --singleplayer --name CiBot` now
  prints `[base] content pack loaded (boot #1)` and `received block
  registry (10 blocks)` (10 = the base 8 + `planks`/`sticks`) — neither
  line appeared at all before this fix, confirming the pack genuinely
  loads and the client genuinely receives the extended registry, not just
  that construction succeeds. Full build + `ctest` (all 4 cases) green on
  `build-asan-nonet` (`VB_BUILD_CLIENT=ON`, `VB_WITH_LUA=OFF` — confirms
  the no-Lua stub path, where `PackRuntime`/`load_content_pack` degrade to
  no-ops, still works); `build-lua` reconfigured with
  `-DVB_BUILD_CLIENT=ON` (it previously had the client off) to get a real
  `voxel_browser` binary with `VB_WITH_LUA=ON` — clean build, full
  `vb_tests` green (203/203).
  **Found along the way, confirmed pre-existing and out of scope, left
  alone:** running `ctest` in the freshly-client-enabled `build-lua` tree
  for the first time (previous sessions only ever ran `./vb_tests.exe`
  directly there) turned up `server_smoke` failing —
  `voxel_browser_server`'s asset-manifest builder hits a real I/O error
  because `build-lua` also happens to have `VB_WITH_COMPRESSION=ON` (every
  *other* tree in this repo has it `OFF`) and ctest's working directory
  (the build dir) has no `content/base` at all (`content_pack` is a plain
  relative path, resolved from whatever the process's cwd happens to be).
  Confirmed unrelated to this session's changes: `src/server/main.cpp`
  itself is untouched, the failure reproduces identically regardless of
  any content-pack edit, and no CI workflow currently sets
  `VB_WITH_COMPRESSION=ON` at all, so this exact combination has just never
  been exercised via `ctest` before now. Not fixed here (would mean making
  `content_pack` path resolution cwd-independent, or making the dedicated
  server's manifest-build failure non-fatal like singleplayer's pack-load
  failure now deliberately is — either is a real, separate, small
  follow-up if `VB_WITH_COMPRESSION` ever gets CI coverage).

- **2026-09-16 (14th): Phase 5.5 documentation pass — `CONTRIBUTING.md`
  added, `docs/lua-api.md`/README's remaining staleness fixed.** Closes
  out the last checkbox in `REMAINING_TASKS.md` 5.5 (the section is now
  `✅`) — `CONTRIBUTING.md` didn't exist at all before this. New file
  covers: a module map (every `src/`/`inc/vb/` subdirectory → its target →
  one-line purpose, pulled from each subdirectory's own `CMakeLists.txt`
  header comment rather than invented from scratch); build/test workflow
  tips not already in the README (running more than one build tree side by
  side for `VB_WITH_LUA` on/off rather than reconfiguring back and forth —
  this project's own sessions already do exactly this, see the 13th
  entry's `build-lua`/`build-asan-nonet` pairing; doctest's `--test-case`
  glob-not-substring gotcha, already documented in §4 above, cross-
  referenced rather than duplicated); code style (`.clang-format`, no
  exceptions on engine hot paths, `vb::<module>` namespacing); a concrete
  step-by-step for adding a new wire message (struct → `MessageType` →
  round-trip test → version bump → `docs/protocol.md` entry → integration
  test) synthesized from this backlog's own repeated pattern rather than
  invented; and a "generic primitive, not a game-specific one" guideline
  for new Lua bindings, using the same-session crafting work
  (`player:take()` + a generic `load_content_pack` root-module loader,
  vs. game rules living entirely in `content/base/crafting.lua`) as the
  worked example.
  Also fixed the last checkbox text staleness: `docs/lua-api.md` and
  `README.md` had *already* been substantially rewritten earlier this
  session (see the 12th-adjacent work), but `REMAINING_TASKS.md` 5.5's own
  checkboxes still read `[ ]` for both — updated to `[x]` with notes
  explaining what was actually done and when, so a future session doesn't
  re-do work that already happened. `docs/protocol.md` was checked for
  drift and found already current (confirmed `kEngineProtocolVersion` 11
  matches `cmake/version.hpp.in`) — every prior session in this backlog
  updated it in the same commit as its wire change, so there was nothing
  to fix there, just confirm.
  No code changes this entry — documentation only. Not verified by any
  automated check (`CONTRIBUTING.md` isn't parsed by anything); read
  through once for internal consistency against the actual current
  `CMakeLists.txt`/`tests/CMakeLists.txt` contents before publishing.

- **2026-09-16 (15th): Cellulose evaluated as the `VB_WITH_MESHING` backend,
  then reverted — hand-rolled `vb::world::chunk_mesher` is now the permanent
  choice, not a placeholder.** §19 Q2 and this file's §5 entry above both
  called Cellulose's API shape "unknown, inspect before writing meshing
  code" — that spike finally happened this session. Fixed a real bug first:
  Cellulose's own `CMakeLists.txt` names its CMake target `libcellulose`
  (`LIBRARY_NAME = "lib${PROJECT_NAME}"`), not `cellulose` — `src/render/
  CMakeLists.txt` and `cmake/Dependencies.cmake`'s `NOT TARGET` guard both
  referenced the wrong name, so `target_link_libraries` silently fell back
  to treating `cellulose` as a raw linker item and the linker went looking
  for a `cellulose.lib` that could never exist (it's header-only/
  `INTERFACE`) — `LNK1104`. Also moved the link from `vb_render` to
  `vb_core`, since `chunk_mesh_snapshot.cpp` (the actual `greedy_mesh` call
  site per the header comments) lives there, not in render.
  With that fixed, `mesh_chunk_from_snapshot` was wired to build a
  `cellulose::MeshSample` grid from the existing `ChunkMeshSnapshot` (face
  visibility + per-corner AO computed the same way as the hand-rolled path,
  walked along `cellulose::impl::face_layout`'s axis_u/axis_v so
  `face_occlusion` packs the way `greedy_mesh` expects) and call
  `greedy_mesh(..., ambient_occlusion=true, weld_t_junctions=true)`. Built
  clean, all 166 `vb_tests` cases passed both with the flag on (after
  updating a handful of tests that hardcoded the hand-rolled mesher's
  unmerged quad counts — a full solid chunk's shell is 6144 unit quads
  unmerged vs. 6 once greedy-merged, etc.) and off.
  **Then it crashed in actual play**, reported as the same class of failure
  as §1's/this file's earlier NVIDIA-driver VAO/VBO heap-corruption bug (see
  the entry above this one's sibling investigation, and the 2026-09-15
  `chunk_renderer.cpp` headroom/`UpdateMeshBuffer()` mitigation) — rapid
  `UnloadModel`+`UploadMesh` churn. Root cause: that mitigation only helps
  when a re-mesh's new vertex/index count still fits the chunk's existing
  (25%-headroom) GPU buffers. The hand-rolled mesher emits a fixed 4
  vertices/6 indices per visible face, so an edit's effect on buffer size is
  small and local. Greedy-merged output doesn't have that property — merging
  means a single block edit near a merge boundary can swing a whole face's
  quad count by a lot (splitting or joining large merged rectangles), so
  chunks blow past their headroom and fall back to full unload/recreate far
  more often, which is exactly the churn pattern that reproduces the driver
  bug. The fewer-draw-calls win from greedy meshing came directly at the
  cost of undermining the one thing keeping the renderer stable on affected
  drivers.
  **Reverted in full** (nothing had been committed, so a plain `git
  checkout --` on the touched files restored the pre-spike state exactly):
  `chunk_mesh_snapshot.cpp` is back to the sole hand-rolled implementation,
  `VB_WITH_MESHING` is back to unused/untested (and still carries the
  `cellulose` vs. `libcellulose` target-name bug described above — nobody
  should re-flip this flag without re-applying that fix first), and
  `cmake/Dependencies.cmake`'s Cellulose `FetchContent` block is untouched
  (harmless dead scaffolding while the flag stays off). `README.md`'s Tech
  Stack row for Voxel Meshing now says this outright instead of "not yet
  wired in". This closes out design question #2 in §6 below and
  `ARCHITECTURE_SPEC.md` §19 Q2 with the opposite outcome from what Q2
  originally planned — see that section's updated resolution note.
  **If anyone revisits a greedy mesher here** (Cellulose or otherwise), the
  VAO/VBO churn sensitivity above is the thing to solve first, e.g. giving
  merged chunks proportionally more headroom, or decoupling "AO enabled"
  (the actual source of the volatility, since it's what breaks merge-key
  uniformity at edit boundaries) from whether merging happens at all.

- **2026-09-17: Wire librg in as the interest backend (`VB_WITH_REPLICATION`).**
  `cmake/Dependencies.cmake` already declared a `vb::librg` INTERFACE target
  and `src/core/CMakeLists.txt` already linked it under the flag, but nothing
  compiled `LIBRG_IMPL` or called into the header — the flag turned the
  dependency on and did nothing else (`REMAINING_TASKS.md` still had "Wire
  real librg in" and "Map players ↔ librg network entities" unchecked).
  **Checked the actual v7.4.0 API first** (`code/header/{general,entity,
  query}.h` in a throwaway clone) rather than trusting `docs/replication.md`'s
  existing summary, which turned out to describe an older/different librg
  shape (`librg_world_write`/`LIBRG_WRITE_*` framing callbacks) — the real
  v7.4.0 surface for a pure interest query is `librg_world_create/destroy`,
  `librg_config_chunk{size,amount,offset}_set`, `librg_entity_track/untrack`,
  `librg_entity_owner_set` (self-owned — required for an id to later be
  usable as a query owner; `librg_world_query` always force-includes entities
  owned by the querying id, which is how it returns "my own" objects), the
  per-tick `librg_entity_chunk_set(librg_chunk_from_realpos(...))`, and
  `librg_world_query` itself (grow-and-retry on its overflow return, per its
  own documented contract).
  **Kept the existing public interface exactly.** `InterestGrid` (`inc/vb/
  replication/interest.hpp`) split into a header (declarations only, no
  `librg.h` include — the librg world is stored as an opaque `mutable void*`
  so the header never needs the vendored C99 header) and `src/replication/
  interest.cpp`, which `#ifdef VB_WITH_REPLICATION`s between the librg-backed
  implementation and the original Phase 1 linear scan verbatim. No caller
  (`ServerSession`, both replication tests) needed a single line changed —
  same `upsert`/`remove`/`clear`/`get`/`visible_from` signatures either way.
  One necessary interface narrowing, documented in the header: the librg
  backend ignores `visible_from`'s `eye` parameter and instead uses the
  querying id's own last-`upsert`ed position, since librg's query is
  owner/chunk-based, not a free-floating-point query — true of every existing
  call site already (`ServerSession::broadcast_snapshots` always upserts a
  player before its own first snapshot).
  **librg's `LIBRG_IMPL` isolated in its own target** (`src/replication/
  librg_impl.c` → `vb_librg_impl`, linked `PRIVATE` into `vb_core`), same
  reason and same pattern as `vb_raygui_impl`: librg is vendored C99 and must
  never see the project's `-Werror` flags. New `src/replication/CMakeLists.txt`
  (subdirectory wasn't registered in `src/CMakeLists.txt` before — interest.hpp
  was purely header-only, pulled in via `vb_core`'s include path with no `.cpp`
  attached to any target).
  **Known limitation, documented in `docs/replication.md` and in code:**
  librg chunk ids need `chunkamount.x*y*z` to fit a signed 32-bit int
  internally, and `librg_chunk_from_realpos` casts each axis to `int16_t`
  chunks — `interest.cpp` picked 1024 chunks/axis (product ~1.07e9, safely
  under `INT32_MAX`), covering ±512×`cell_size` world units per axis around
  the origin. An entity straying outside that range gets `LIBRG_CHUNK_INVALID`
  and is silently excluded from everyone's interest until it re-enters — no
  crash. This was flagged as an explicit open item in the original spike doc;
  a bigger world needs a bigger `chunkamount` (traded against the same int32
  overflow risk) or a movable local origin, neither of which exists yet — not
  attempted here since nothing today needs a world bigger than ±16384 units
  per axis (default 32-unit cells).
  **CI**: added `-DVB_WITH_REPLICATION=ON` to all three `build_*.yml` OS legs,
  same posture as `VB_WITH_NET`/`VB_WITH_LUA` — librg needs no new system
  packages (single vendored C99 header, FetchContent only), so this was a
  low-risk addition, unlike the still-off `VB_WITH_NET` on macOS (multi-arch
  protobuf problem, see that workflow's own comment).
  **Verification:** built `-DVB_WITH_REPLICATION=ON` fresh with
  `CC=clang CXX=clang++` (this box's toolchain) — all 166 existing test
  cases / 99875 assertions pass unchanged (including both replication tests
  in `tests/unit/replication_test.cpp`, which exercise `InterestGrid`
  directly and the two-client join/move/leave visibility scenario). Also
  rebuilt with the flag off to confirm the linear-scan path is byte-for-byte
  unaffected (identical 166/99875 pass counts). Confirmed `interest.cpp`
  itself compiles clean under the project's exact `-Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-align -Wunused -Woverloaded-virtual -Wdouble-promotion -Werror` set
  in both configurations (isolated single-file compile, since building all of
  `vb_core` end-to-end with `-Werror` on this box's clang 21 currently fails
  on unrelated EnTT `meta.hpp`/`dense_map.hpp` `-Wsign-conversion` noise —
  confirmed pre-existing on `main` with the same toolchain before touching
  anything, not something this change introduced or can fix from here; CI's
  pinned compiler versions apparently don't hit it, since "CI is fully green"
  per the 2026-09-10 entry above).

- **2026-09-17: Flip `VB_WITH_REPLICATION` default from OFF to ON.** librg is
  now the default entity-replication backend (`CMakeLists.txt` option default),
  not just something CI opts into — it's the more battle-tested option, and
  the hand-rolled linear scan remains available as an explicit opt-out
  (`-DVB_WITH_REPLICATION=OFF`) for anyone who'd rather avoid the extra
  dependency. No source changes; `InterestGrid`'s dual-backend `#ifdef`
  structure (`src/replication/interest.cpp`) is unchanged. Updated
  `README.md` and `docs/replication.md` to describe librg as the default,
  not the opt-in, backend. CI's explicit `-DVB_WITH_REPLICATION=ON` in the
  three `build_*.yml` legs is now redundant but harmless — left as-is rather
  than churned.

- **2026-09-17: Design-only pass, "Phase 6 — Lua-Driven Extensibility."** No
  code changed. Captured a design discussion into `ARCHITECTURE_SPEC.md`
  (§7.1-7.2, §10.3-10.6, §17, §19 Q6) and a new `REMAINING_TASKS.md` Phase 6
  section, covering four systems: (1) entity kinds as Lua "classes" with
  spawned entities as "objects" carrying per-instance state via the
  already-declared-but-unused `ScriptState` component; (2) reframing
  `ui.define` from static declaration to an immediate-mode `render(state)`
  function called every UI frame — leans on `raygui` already being
  immediate-mode, so no virtual-DOM diffing is needed for a React/Svelte-like
  feel; (3) a closed-schema custom-keybind channel
  (`vb.register_keybind`/`vb.on("player_input", ...)`) where the flood
  defense is the wire format itself (bounded bitset, registered set only)
  rather than a post-receipt filter, plus a server-side input-interception
  hook (veto or replace) between `IngestInputSystem` and
  `MovementIntegrationSystem`; (4) a generic per-key `vb.db` store, distinct
  from the existing pack-global `vb.storage`, explicitly designed so the
  engine has no opinion on "logged in" — any auth/identity model is entirely
  pack-implemented on top of it (joining a world ≠ authenticating), with the
  engine only offering a `vb.crypto.hash` primitive so a pack that does build
  login doesn't roll its own credential hashing in pure Lua. None of this is
  implemented; it's a backlog entry, not a code change.

- **2026-09-17: Design-only pass, "Phase 6.5 — Shared block-damage
  breaking."** No code changed. Follow-up to the Phase 6 pass above, adding
  block breaking as a shared damage pool rather than an instant edit:
  `BlockType` gains `max_damage` (0 = today's instant break, default, no
  behavior change) and an optional `crack_texture` override; a sparse
  server-side `pos → damage` map rides the *existing* interest/replication
  system as a transient record (appears/disappears via the same spawn/despawn
  diffing every other replicated object already gets, so nearby players see
  cracks form without a new wire channel); `C2S_BlockBreakBegin`/`...Stop`
  bracket a player contributing, gated by the same reach/tool/protection
  checks `C2S_BlockEdit` already has; two symmetric per-tick Lua hooks split
  mechanism from policy — `block_break_tick` (per contributing player, sums
  concurrently so multiple players can break together) adds damage,
  `block_health_tick` (per damaged block, every tick) lets Lua decide heal
  policy entirely (no heal / full heal / decay / heal-after-idle, engine has
  no default); completion still drives the existing unchanged
  `C2S_BlockEdit`/`BlockEditSystem`/`on_break` pipeline. Crack rendering is
  default-with-override (baseline generic overlay + optional per-block
  texture), explicitly noted as blocked on the still-pending real
  texture/atlas system (4.3/5.1) landing first. Updated
  `ARCHITECTURE_SPEC.md` §5.2, §8.5, §10.3, new §10.7, §17, and added
  `REMAINING_TASKS.md` Phase 6.5. Backlog entry, not a code change.
  (Two further design-only passes landed the same day without their own
  STATE.md entries — `REMAINING_TASKS.md` Phase 6.6-6.13, a codebase audit
  for more Lua-extensibility candidates including a foundational player
  damage/death gap, and a Deferred-section note on rule-based decorative
  structure placement authored via an external tool.)

- **2026-09-17: Redesign worldgen §6 stage 2 (biome selection) to
  Voronoi-cell, adjacency-weighted probability.** No code changed — the
  Phase 4.2/6 worldgen pipeline is still fully unimplemented (base pipeline
  is heightmap-only today). This replaces the originally-sketched continuous
  temperature/humidity noise selection with a discrete Voronoi cellular
  partition where each cell's biome is a weighted draw from
  `vb.register_biome`'d biomes, weighted by that biome's base probability
  **times an adjacency-compatibility factor** against already-resolved
  neighboring cells (e.g. desert next to tundra near-zero, desert next to
  plains normal) — Lua supplies both tables, engine only draws.
  Deliberately **not** textbook wave-function-collapse: cell resolution
  order is fixed by a hash of `(world_seed, cell_id)` rather than
  exploration/entropy order (so two players approaching the same seed from
  different directions can't see different layouts — would violate the
  `(world_seed, chunk_coord, pack_version)`-purity every other stage
  depends on), and adjacency weights are **soft multipliers with a floor,
  never hard exclusions**, so a cell can never hit a contradiction and the
  algorithm never needs backtracking — required for a world generated
  lazily per-chunk that must always succeed, unlike a bounded grid solved
  once. Also added a new **stage 5, vein/scatter pass** for underground
  ore/valuable-block placement (`{block, target_rock, height_range,
  vein_size, spawn_rate}` per biome or pack-global) — this didn't exist
  anywhere in the prior pipeline sketch at all, not even as a stub; decor
  (trees) is stage 6 and lighting stage 7 now, renumbered from the prior
  5/6. Updated `ARCHITECTURE_SPEC.md` §6 and the corresponding
  `REMAINING_TASKS.md` worldgen items (2.2, 4.2) plus a stale `§6 stage 5`
  cross-reference in the Deferred section (now stage 6). Backlog entry, not
  a code change.

- **2026-09-17: Cellulose's dormant `VB_WITH_MESHING` scaffolding removed
  outright, at the user's request** — the 15th entry above (2026-09-16) had
  reverted the actual meshing wiring but deliberately left the build-flag
  and `FetchContent` block in place as documented dead scaffolding, "kept
  only in case a future session wants to re-investigate a different
  greedy-mesh approach." No further investigation is planned, and the
  scaffolding was judged not worth carrying forward just in case. Removed:
  the `VB_WITH_MESHING` option (`CMakeLists.txt`), its `FetchContent`
  declaration (`cmake/Dependencies.cmake`), the conditional
  `target_link_libraries`/`target_compile_definitions` block in
  `src/render/CMakeLists.txt`, the dependency-table row in
  `docs/protocol.md`, and the explanatory comments in `chunk_mesher.hpp`/
  `chunk_mesh_snapshot.hpp`/`chunk_mesh_worker_pool.hpp` that referenced it.
  `ARCHITECTURE_SPEC.md` §19 Q2, `REMAINING_TASKS.md` §2.5, and `README.md`'s
  Tech Stack row were reworded to say the dependency was removed rather than
  left unused. This entry (and the 15th entry it follows) remain as the
  historical record of why Cellulose isn't here — no source code besides
  the flag/comment surface was ever touched by this cleanup, since none of
  it had built or run since the revert. Nothing to test: `VB_WITH_MESHING`
  had no effect on any default build before this change, so removing it
  changes no build output.
