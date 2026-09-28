# Voxel Browser — Remaining Tasks

> Companion to `ARCHITECTURE_SPEC.md`. This is the implementation backlog to get
> from the current state to the "Minimum Playable Base" described in `README.md`.
>
> **This file is the lean core.** Every phase's full history (the detailed
> `[x]` write-ups — what shipped, why, which files, which tests) lives in its
> own `remaining_tasks/phaseN.md`, linked from each phase section below. Read
> this file for current status and what's actually left; open the linked file
> only when you need the historical detail behind a specific `[x]` line.

## Current State (baseline)

- [x] Modular CMake: `vb_core` / `vb_render` / `voxel_browser` /
      `voxel_browser_server` / `vb_tests`, git-tag versioning, `PROJECT_NAME`
- [x] `cmake/{Dependencies,Warnings,Sanitizers}.cmake`; deps via pinned `FetchContent`
- [x] Runnable client + server skeletons (arg parsing, banners, loops, `--headless`)
- [x] CI: lint + Linux/macOS/Windows build matrix **+ ctest**, bundle, publish
- [x] `.clang-format` (tabs, `ColumnLimit 0`, `Standard: c++20`)
- [ ] Networking / world / ECS / scripting — Phase 1 onward

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · **(spike)** = timeboxed
investigation, may change the plan.

---

## Phase 0 — Project Restructure & Build System ✅ done (2026-09-10)

Prerequisite for everything else, not in the README's phase list. Full split
target/build system: `vb_core`/`vb_render`/client/server/tests, pinned
`FetchContent` deps gated behind `VB_WITH_*`, warnings/sanitizer cmake
modules, `VB_HEADLESS`, CI workflows, project renamed to `voxel_browser`.
Full detail: `remaining_tasks/phase0.md`.

**Remaining:**
- [ ] First `git tag v0.0.1` so `git describe` yields a real version and
      `bundle`/`publish` are exercised.
- [ ] `CMAKE_POLICY_VERSION_MINIMUM=3.5` shim is set for CMake ≥ 4 (doctest
      2.4.11 declares `cmake_minimum_required(3.0)`); drop it if doctest is bumped.
- [ ] Explicit source lists instead of relying on re-running CMake (already
      explicit; keep it that way as modules grow).

---

## Phase 1 — Core Foundation & Networking ✅ met (2026-09-11)

Goal: reliable client↔server handshake; two clients "see" each other in
network space; client window + render loop alive. Core primitives, transport
(`LoopbackTransport` + real-UDP `GnsTransport`), handshake FSMs, hand-rolled
`InterestGrid` replication (librg wired in behind `VB_WITH_REPLICATION`),
client shell (raylib window, `FirstPersonController`). Both binaries wired up
end-to-end; verified with two live processes over real UDP.
Full detail: `remaining_tasks/phase1.md`.

**Remaining:**
- [x] Surface `ENGINE_PROTOCOL_VERSION` mismatch in the client connect UI —
      already fully wired by the time this was checked (2026-09-28), no code
      change needed: `ClientHandshake::on_frame` (`src/net/handshake.cpp`)
      fails with `"engine protocol version mismatch"` both when the client's
      own check trips (`info->engine_protocol_version != kEngineProtocolVersion`)
      and when a server-side rejection frame (`kProtocolMismatch`) arrives;
      `src/client/main.cpp`'s `kConnecting` case (`client->failed() ?
      client->failure_reason() : ...`) already routes that string into
      `error_message` and `AppState::kError`, which `MainMenu::draw_error()`
      (`src/render/main_menu.cpp`) renders as a real raygui label with a
      "Back to menu" button — the Phase 5.3 main-menu error path this item
      said it was waiting on has existed since that phase landed. Covered by
      the existing `net_test.cpp` case "client rejects a protocol version
      mismatch". This item's own text was stale, tracking a gap that closed
      as a side effect of unrelated work rather than being picked up as its
      own task.
- [ ] macOS CI doesn't build `VB_WITH_NET` (universal arm64+x86_64 build vs.
      single-arch brew protobuf) — needs a universal protobuf, see `build_macos.yml`.
- [x] The two-client replication test runs over `LoopbackTransport` only;
      re-run over `GnsTransport` — see "Current status" in `STATE.md` for
      the full writeup (landed 2026-09-28).

---

## Phase 2 — World State & Terrain Generation ✅ met (2026-09-11)

Goal: server generates terrain, streams chunks, client meshes and renders
them. `PalettedChunkStore`, `World`, fBm heightmap worldgen (determinism gate
green on all 3 platforms), per-chunk `LightEngine` + cross-chunk vertical
sky occlusion, chunk replication + `ClientChunkStore`, hand-rolled
face-culled mesher with a background mesh worker pool (Cellulose/greedy-merge
was tried and reverted — see `ARCHITECTURE_SPEC.md` §18 Q2 — hand-rolled
meshing is permanent).
Full detail: `remaining_tasks/phase2.md`.

**Remaining:**
- [x] Horizontal cross-chunk light propagation — landed 2026-09-28, closing
      this phase's last remaining item (`relight_chunk`/`relight_column` were
      previously vertical-only: a chunk always assumed a closed border on
      every side except straight up). `LightEngine::Neighbours`
      (`inc/vb/world/lighting.hpp`) replaces the old bare `const Chunk *above`
      parameter with 5 optional fields (`above` plus new `north`/`south`/
      `east`/`west`) — a single-pointer constructor keeps every pre-existing
      `above`-only call site (including `relight_column`'s own) compiling
      unchanged. `relight_chunk` (`src/world/lighting.cpp`) seeds each of the
      4 new vertical *faces* from whichever horizontal neighbour is loaded,
      the same "attenuate by 1 step, `if (seeded > sky[i])` relax" shape the
      interior BFS already used, not the top face's special "straight down,
      no falloff" case (horizontal light always decays by 1 per step, matching
      how a purely-interior sideways step already behaved before this pass).
      `relight_column` now builds the full `Neighbours` set (via `find()`) at
      every level of its vertical cascade, so *any* relight — edit, initial
      load, or cascade — picks up whatever horizontal neighbours happen to be
      loaded at that moment, the same passive "use what's there" posture
      `above` already had.
      **Real reactive gap closed on top of that:** the passive form above
      only helps when a chunk happens to relight *after* its neighbour is
      already lit right; it does nothing for the common live-edit case (break
      one block near a border, and the chunk on the other side — already
      stably lit, with no other reason to ever relight again — never finds
      out). `relight_column_impl`'s new `push` parameter closes that: when a
      chunk in the cascade actually changes and a horizontal neighbour is
      loaded, it recursively relights that neighbour's whole column too
      (deferred until the whole triggering column finishes, so the pushed
      neighbour never reads a half-updated column back), reported through the
      same `on_relit` callback so `WorldReplicator`'s per-edit code sends the
      neighbour's own delta immediately, not on the next tick's separate
      revision-diff sweep. Bounded to exactly one hop, provably: every
      propagation step costs at least 1 of light's 0-15 range and a chunk is
      `kChunkDim` (32) blocks wide, so light that has just crossed one border
      has at most 14 of budget left — nowhere near enough to cross a second
      full-width chunk and reach a third one, so a pushed neighbour's own
      relight never tries to push again. Diagonal neighbours are still never
      touched directly (unchanged from before this pass) — any effect on one
      only ever arrives indirectly through whichever shared orthogonal
      neighbour pushes into it.
      Verified: full `vb_tests` 382/382 green (2 new `lighting_test.cpp`
      cases — a direct `relight_chunk` case proving sideways spill/falloff
      from a single `west` neighbour under an otherwise-sealed roof, and a
      `relight_column`-based end-to-end case proving an edit that opens a
      gap in one already-loaded chunk's ceiling automatically relights an
      already-stable neighbour on the other side of the border, without ever
      calling relight on that neighbour directly, and reports it via
      `on_relit`), clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server` (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to
      this dir's OFF default afterward). **Deliberately out of scope, per the
      pre-existing vertical asymmetry this pass didn't reopen:** block light
      still doesn't cross chunk borders at all (vertically or horizontally) —
      only sky light does, matching what the vertical-only implementation
      already covered before this pass. The actual rendered result (a human
      mining sideways near a chunk border and watching light spill in
      correctly instead of a dark band) was **not** manually eyeballed — no
      GUI in this agent environment, same still-open caveat as every other
      rendering-adjacent pass in this file.
- [x] Frustum culling, transparent second pass — landed 2026-09-27 (texture
      atlas itself landed separately 2026-09-23, see Phase 4's own entry).
      New `inc/vb/render/frustum.hpp` (header-only, no raylib dependency,
      same "pure math, unit-tested without a GL context" posture as
      `entity_visual_layout.hpp`): `build_frustum()` derives the 6 view-frustum
      planes straight from camera basis vectors (position/forward/up/fovy/
      aspect/near/far) via the standard "cross product of the far-plane
      corner vectors" construction — deliberately *not* extracted from a
      combined view-projection matrix, which would tie this pure header to
      raylib/rlgl's internal row/column matrix convention; `aabb_in_frustum()`
      is the standard conservative "positive vertex" AABB-vs-plane test.
      `ChunkRenderer::draw()` (`src/render/chunk_renderer.cpp`) now takes the
      `Camera3D` being rendered with (signature change, one call site in
      `src/client/main.cpp`), builds a frustum from it every call (near/far
      hardcoded to 0.01/1000.0 to match `BeginMode3D`'s own un-overridden
      `RL_CULL_DISTANCE_NEAR/FAR` defaults — nothing in this codebase calls
      `rlSetClipPlanes`), and skips any chunk whose 32-block AABB is provably
      entirely outside it — no draw call at all for a culled chunk, not just
      an early depth-reject.
      Transparent second pass: `ChunkRenderer` now uploads **two** GPU models
      per chunk instead of one — `split_transparent()` (new, `chunk_renderer.
      cpp`) partitions a chunk's meshed quads by the same flat fallback-color
      alpha `fill_mesh_arrays` already used for vertex-color alpha (today:
      only `base:leaves`, `a=220` — see `fallback_color_for()`), since every
      quad's 4 vertices already share one `block_id` and are contiguous by
      construction (`chunk_mesh_snapshot.cpp`'s own per-face `first` numbering)
      -- not a per-texel alpha check, and not a new `BlockType` field. `draw()`
      renders every visible chunk's **opaque** model first (any order, the
      depth buffer alone sorts it out), then every chunk with transparent
      geometry a second time with `rlDisableDepthMask()` set and sorted
      back-to-front by chunk-center distance from the camera (`rlDrawRenderBatchActive()`
      flushes around the depth-mask toggle so it doesn't retroactively apply
      to already-batched pass-1 draws) — chunk granularity only, not
      per-triangle, matches this engine's block scale. `GpuChunk` is now two
      `GpuMesh` slots (`opaque`/`transparent`) instead of one `Model`+capacity
      pair; `upload_part()` (new) is the old single-mesh reuse-if-it-fits/
      recreate-if-it-doesn't logic, now run once per slot.
      Verified: full `vb_tests` 370/370 green (7 new `frustum_test.cpp` cases:
      ahead/behind/beside/beyond-far/nearer-than-near/straddling-the-boundary
      AABB cases plus one proving a non-normalized non-orthogonal `up` still
      works), clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server` (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
      dir's OFF default afterward). The actual rendered result (a human
      walking around and watching off-screen chunks stop being drawn, and
      leaves/water blend correctly over terrain behind them) was **not**
      manually eyeballed — no GUI in this agent environment, same still-open
      caveat as every other rendering-adjacent pass in this file.

---

## Phase 3 — Entity Component System & Physics ✅ substantially met (2026-09-11)

Goal: server-simulated players with voxel collision, movement synced to
clients with prediction/interpolation. EnTT registry wiring on the server
(2026-09-17), fixed 20 Hz tick loop (including `--singleplayer`'s integrated
server), swept-AABB voxel physics (`vb/physics/movement.hpp`), input
pipeline + prediction/reconciliation, remote-entity interpolation, and
billboard-sprite presentation (direction buckets, anim priority) for remote
entities — mechanism shipped and tested, real multi-window art not yet
eyeballed live.
Full detail: `remaining_tasks/phase3.md`.

**Remaining:**
- [x] System runner with explicit ordering (§7.2) — `vb::ecs::SystemRunner`
      (2026-09-25); script entities (Phase 6.1) are now real registry
      entities, giving it a genuine second consumer besides players. See
      `remaining_tasks/phase3.md`.
- [x] Client-side lightweight registry (2026-09-25) — `ClientSession`'s
      remote-entity interpolation bookkeeping now lives in a real
      `entt::registry` (`ecs::InterpBuffer`/`ecs::EntityKind`), not the old
      ad hoc `RemoteSample` struct. See `remaining_tasks/phase3.md`.
- [x] Per-player rate limit / flood guard — landed 2026-09-27 as
      `ServerSession::set_max_messages_per_second(double)` /
      `ServerConfig::max_messages_per_second` (`server.toml`, default 0 =
      unlimited). A token bucket per playing connection (one token per
      post-join message, any type -- input batch, block edit, chat, UI
      event, block-break begin/stop -- refilled at the configured rate,
      capacity == one second's worth of burst), checked in
      `system_network_io()` before a message is dispatched to its handler;
      an empty bucket drops the message (not the connection). Defense in
      depth on top of the existing closed-schema per-message caps
      (`C2SInputBatch::kMaxCmds`, the fixed-width keybind bitset) -- those
      bound how much one message can do, this bounds how often a connection
      can send one at all. Exposed read-only via `vb.config.get(
      "max_messages_per_second")` (Phase 6.13's surface), same posture as
      `max_connections_per_ip`. See Phase 6's own entry below for the
      matching "closes the custom-keybind-flood item too" note. Verified:
      full `vb_tests` 380/380 green over `LoopbackTransport` (a new
      `netcode_test.cpp` case drives 3 back-to-back chat sends through a
      1 msg/sec limit, confirms only the first lands, then confirms a 4th
      lands again after ~1s of ticks refill the bucket; `config_test.cpp`/
      `pack_runtime_test.cpp` cases cover the TOML default/parse and
      `vb.config.get` round-trip) — 2 real-UDP `gns_transport_test.cpp`
      cases skipped in this run (this agent environment's Windows Firewall
      blocks unattended real-UDP listen/connect), clean `-Werror` build of
      `vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
      reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
      confirmed clean, reconfigured back to this dir's OFF default
      afterward).
- [x] Step-up jerk: physics is exact but visually abrupt — landed 2026-09-28.
      New `vb::render::EyeHeightSmoother` (`inc/vb/render/camera.hpp`, pure
      math, unit-tested without a GL context, same posture as
      `frustum.hpp`/`entity_visual_layout.hpp`): each frame it exponentially
      eases the *rendered* eye Y toward the true `feet.y + eye_height` target
      over a short (0.12s) time constant, rather than setting the camera to
      it directly — a step-up's instantaneous 1-tick position jump (up to
      `step_height`, 1.05m) now reads as a quick smooth rise instead of a
      pop. A jump bigger than `kSnapThreshold` (2.0m — above any real
      step-up, below a teleport/respawn distance) is applied immediately with
      no smoothing, so a respawn or reconnect never eases in from the old
      body's position. Purely a render concern: `feet`/collision/physics are
      completely untouched, `X`/`Z` are still set directly from `feet` every
      frame (only `Y` gets smoothed) — the "physics is exact" half of the
      original gap is unchanged, only what's drawn changes.
      `src/client/main.cpp` wires one `EyeHeightSmoother` per client loop
      (both the `--headless` path and the real windowed one), reset alongside
      `controller` at spawn/join and in `enter_playing()` (reconnect/respawn)
      so a fresh life never inherits a stale in-flight smoothing state from a
      previous one. Verified: full `vb_tests` 388/388 green (6 new
      `render_test.cpp` cases: first-update snaps, a step-up eases in over
      one frame rather than landing immediately, converges to the target
      after enough time, a large jump snaps immediately, a tiny continuous
      delta tracks almost exactly, and `reset()` drops any in-flight
      smoothing), clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server` (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to
      this dir's OFF default afterward). The actual smoothed step-up (a human
      walking up a single block and watching the camera rise instead of pop)
      was **not** manually eyeballed — no GUI in this agent environment, same
      still-open caveat as every other rendering-adjacent pass in this file.
- [ ] Wall-clock `server_time_est` + smoothing on the client (needs
      `GnsTransport` RTT — loopback has no latency to estimate).
- [x] `SpriteVisual`-equivalent client state (atlas handle, `facings`,
      per-clip frame lists) for entity kinds — landed 2026-09-25 as Phase
      4's `visual = {...}` item below (`EntityRenderer`'s `KindVisual`/
      `EntityVisualLayout`, entity-management follow-up).
- [x] Real billboard art (atlas, per-clip frames) — `base:player`/
      `base:dropped_item` now ship real checked-in spritesheets, see Phase
      4's own "Real base-pack art" entry, landed 2026-09-25.

---

## Phase 4 — The "Browser" Engine (Scripting & Assets) ✅ met — mechanism, not content

Goal: server logic + content defined in Lua; client auto-downloads pack
assets; Lua-defined UI. Sandboxed Lua 5.4 + sol2 VM, `PackRuntime`
registration/event-bus/scheduling API, `S2C_BlockRegistry` replication, full
Asset Sync protocol (manifest, chunked transfer, content-addressed client
cache, virtual pack FS), and a separate client-side `UiRuntime` + raygui
renderer driven by `player:open_ui`. All five subsystems (4.1-4.5) work
end-to-end over `LoopbackTransport`; what was missing was content, closed by
Phase 5.1.
Full detail: `remaining_tasks/phase4.md`.

**Remaining:**
- [x] Custom `require` over the pack's own virtual module filesystem +
      per-callback wall-clock budget — landed 2026-09-27. Reframed from the
      original plan: rather than reusing `ClientAssetCache`'s synced-FS map
      (4.4), which only ever exists on a *remote client*, never on the
      server/`PackRuntime` side that actually needs `require`, the virtual
      FS here is a small in-memory `path -> source text` map that
      `vb::script::load_content_pack` (`src/script/pack_loader.cpp`) builds
      once from its own recursive directory walk (every `.lua` under the
      pack root except `ui/*.lua`, which runs in its own restricted
      `UiRuntime` VM) and installs via the new `PackRuntime::
      set_pack_modules()` -> `Vm::install_require()`. `Vm` (`inc/vb/script/
      vm.hpp`, `src/script/vm.cpp`) reinstates a safe `require` global right
      after `strip_sandbox()` nils the stock one: a dotted or slash-separated
      module name resolves only against that map (`.` -> `/`, `.lua`
      appended if missing), rejects a name containing `..` or a leading `/`,
      caches a module's return value across repeated `require()` calls the
      same way stock Lua's `package.loaded` does (no explicit `return`
      caches as `true`), and detects a require cycle via an in-progress name
      stack rather than deadlocking or stack-overflowing. Wall-clock budget:
      `VmLimits` gains `wall_clock_budget_ms` (default 250); the existing
      instruction-count hook (`count_hook`) now fires far more often
      (`kHookPeriod` = 1000 instructions, independent of the caller's own,
      possibly huge, `instruction_budget`) and checks a `std::chrono::
      steady_clock` deadline first, so a callback with few but individually
      slow instructions still gets cut off in real time, not just by VM
      instruction count — both failure modes still classify to the existing
      `core::ScriptError::kBudgetExceeded` (no new enum value; the two
      distinct internal marker strings, `vb:instruction-budget-exceeded` and
      `vb:wall-clock-budget-exceeded`, are what `classify()` keys off of).
      `AllocState` (`inc/vb/script/vm_internal.hpp`) now carries the
      hook's own running state (`instructions_run`, `deadline`,
      `time_boxed`) since it's the one piece of state already reachable from
      inside the hook via `lua_getallocf`. `Vm::sandbox_intact()`'s old
      `absent("require")` assertion is inverted to `require` being present
      and a function (`sol::type::function`) — everything else it checks
      (`os`/`io`/`load`/`package`/trimmed `debug`) is unchanged. Existing
      packs are unaffected (`content/base`/`kitchen_sink` never call
      `require`); this is additive only. Verified: full `vb_tests` 381/381
      green (10 new cases: 8 `script_test.cpp` `Vm`-level cases covering
      resolve/dotted-path/caching/no-return-defaults-true/not-found/path-
      traversal/circular-dependency/hot-swap-drops-cache, 1 wall-clock-vs-
      huge-instruction-budget case, and 1 `content_pack_test.cpp` end-to-end
      case driving a real `require('lib.util')` through `load_content_pack`
      and confirming the result reaches `vb.storage`), clean `-Werror` build
      of `vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
      reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
      confirmed clean, reconfigured back to this dir's OFF default
      afterward).
- [x] `EntityKind` tick/spawn/hit/death callbacks wired into real systems —
      landed 2026-09-27, now that `SystemRunner` itself exists (Phase 3.1,
      2026-09-25). `PackRuntime::dispatch_tick` (the `tick` event + entity
      `on_tick`/`on_hit`/`on_death` dispatch + global timers) used to be a
      separate call each embedder's own loop made *after* `ServerSession::
      tick()` had already run every phase for that tick (`server/main.cpp`,
      `client/main.cpp`'s `Singleplayer::tick()`) — a script-driven entity
      move only reached `sync_interest`/`broadcast_snapshots` one tick late.
      New `ServerSession::set_script_tick_hook(fn(double))` (`inc/vb/net/
      session.hpp`) is a new `"script_tick"` `SystemRunner` phase, placed
      right after `check_respawns` and before `update_item_drops`/
      `sync_interest`/`broadcast_snapshots` — same "`ServerSession` has no
      idea this is Lua-backed" decoupling every other hook here already uses
      (`set_landed_hook` et al.), not a direct `PackRuntime` reference.
      `PackRuntime::attach_session()` installs it unconditionally (unlike the
      conditional hooks above it — `dispatch_tick` does real work, global
      timers included, even for a pack with no entity-kind callbacks at
      all). Both embedders' loops no longer call `pack_runtime.dispatch_tick()`
      directly. Verified: full `vb_tests` 381/381 green (updated the ~19
      existing `attach_session`-using pump loops in `pack_runtime_integration_
      test.cpp`/`content_pack_test.cpp`/`kitchen_sink_pack_test.cpp` to drop
      their now-redundant explicit `rt.dispatch_tick()` call — leaving it in
      would have double-fired every entity tick/timer per pump step), clean
      `-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
      (temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=
      ON`, confirmed clean, reconfigured back to this dir's OFF default
      afterward).
- [x] `register_entity`'s `visual = {...}` sub-table (variant/facings/clips)
      for 3.5's `entity_renderer` — landed 2026-09-25. Protocol version bumped
      **19 -> 20**: `EntityKindRegistryRecord` gains an optional `visual`
      field (`protocol::EntityVisualDef` — texture path, resolved
      frame_width/height, facings, origin, a `clips` list of
      `{clip, frames, fps}`), absent for a kind that never sets one (e.g.
      `kitchen_sink:sentry`, which only sets `width`/`height` — no behavior
      change for it). `PackRuntime`'s `vb["register_entity"]` binding parses
      and shape-validates `visual = {...}` at registration (unknown variant,
      missing texture, `facings` not 4/8, out-of-range `origin`, an empty
      `clips` array, or a non-positive `frames`/`fps` all throw a
      `sol::error`) — no image decoding happens on this, headless,
      pack-runtime side. Client-side, a new pure header
      `vb/render/entity_visual_layout.hpp` (`build_entity_visual_layout`)
      computes the running per-clip column layout and validates it against
      the *real* decoded PNG's pixel dimensions once
      `EntityRenderer::set_kind_visual()` decodes the synced/on-disk texture
      (wired from `src/client/main.cpp`, right after the block texture atlas
      is built, same one-shot join-time posture) — a mismatch there logs
      `VB_WARN` and that kind keeps the flat placeholder billboard rather
      than failing pack load. `EntityRenderer::draw()` now picks its
      billboard's source rect from the resolved pose row (unchanged
      `select_pose()` machinery) and animation clip (new
      `render::anim_clip_name(AnimClip)` bridges the existing
      `resolve_anim_clip()` enum to a pack's clip names, falling back to the
      first declared clip if the pack never named that one, per spec) and
      frame index (`clip_time * fps`, wrapped via modulo — the finalized
      schema has no separate loop/hold-last-frame flag). A kind with no
      `kind_visuals_` entry (players, or any kind that never set `visual`)
      renders exactly as before this landed. **Deliberately out of scope,
      kept for a follow-up:** per-instance `ScriptState.visual_override`
      (skins) and real base-pack art (`base:player`/`base:dropped_item`
      shipping actual spritesheets, still "5.1" below) — this pass proved the
      mechanism with synthetically-generated-at-test-time PNGs
      (`ExportImageToMemory`, same pattern `texture_atlas_test.cpp` already
      used), not new binary art checked into `content/`. Verified: full
      `vb_tests` 331/331 green (12 new cases: a protocol round-trip +
      too-many-clips-cap test, two new `pack_runtime_test.cpp` cases covering
      every rejection path plus a full valid-visual-reaches-the-registry
      case, a new `entity_visual_layout_test.cpp` covering the pure layout
      math, and an `anim_clip_name` coverage case in `entity_visual_test.cpp`),
      clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server` (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to
      this dir's OFF default afterward). The actual rendered sprite/animation
      (a human watching a real spritesheet animate on a billboard) was
      **not** manually eyeballed — no GUI in this agent environment, same
      still-open caveat as every other recent rendering-adjacent pass.
- [x] Per-instance `ScriptState.visual_override` (skins) -- landed 2026-09-25.
      `vb.world.spawn(kind, pos, { visual_override = {...} })` takes a third,
      optional options table whose `visual_override` is the same shape as
      `register_entity`'s `visual` (`variant`/`texture`/`facings`/`origin`/
      `clips`) but with every field independently optional -- an omitted
      field inherits the kind's own `visual` unchanged
      (`render::merge_visual_override`, `inc/vb/render/
      entity_visual_layout.hpp`), so a pack can override just `texture` (a
      player-skin variant) while keeping the kind's `facings`/`clips`/
      `origin`. Protocol bumped **20 -> 21**: `EntityRecord` (within
      `S2C_EntitySnapshot`) gains an optional `visual_override`
      (`protocol::EntityVisualOverride`, all-optional fields), populated only
      on the one `entered` record a client gets when a NetId first enters
      their interest set (`net::ServerSession::to_record`/
      `broadcast_snapshots`, backed by a new `set_script_entity_visual_
      override()`/`script_entity_visual_overrides_` map) -- `updated`/`local`
      records never carry it, and `ClientSession` caches whatever it first
      learned (`entity_visual_overrides_`) for the entity's whole replicated
      lifetime, same "learned once, immutable" posture as `EntityRecord.kind`
      itself. Client-side, `EntityRenderer::sync()` lazily decodes an
      override's texture (merged over the kind's own `EntityVisualDef`, or an
      all-default one if the kind never set `visual`) the first time it sees
      a given NetId's override -- at most once per id, cached in a new
      `instance_visuals` map that takes priority over `kind_visuals` in
      `draw()` -- reusing a `set_kind_visual`/lazy-decode path factored into a
      shared `decode_kind_visual()` helper. `EntityRenderer::set_virtual_fs()`
      (new) keeps a copy of the synced/on-disk pack filesystem for this
      lazy decode, since (unlike every kind's `visual`, fixed at registration
      before any client joins) an override's owning entity can spawn at any
      later time, not just during the one join-time pass `set_kind_visual`
      calls already cover. **Deliberately out of scope, left as a real
      follow-up:** `entity:set_visual_override()`/a live-update or clear path
      -- the override is fixed at spawn time only; there is no wire mechanism
      to change or clear it for a client that has already seen the entity.
      The reserved `self.visual_override` Lua table (spec's own key) is kept
      in sync for pack introspection only -- nothing engine-side reads it
      back, the parsed/validated copy already lives server-side. Verified:
      full `vb_tests` 340/340 green (1 new `protocol_test.cpp` round-trip
      case incl. an `updated` record never carrying an override, 3 new
      `entity_visual_layout_test.cpp` cases for `merge_visual_override`'s
      empty/texture-only/full-replace behavior, 2 new
      `pack_runtime_integration_test.cpp` cases -- one proving the override
      reaches a real client's `entity_visual_override()` with only `texture`
      set, one proving a malformed override rejects the whole spawn), clean
      `-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
      (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to
      this dir's OFF default afterward). The actual rendered skin swap (a
      human watching two instances of the same kind render with different
      textures) was **not** manually eyeballed -- no GUI in this agent
      environment, same still-open caveat as every other recent
      rendering-adjacent pass.
- [x] Real base-pack art for `base:player`/`base:dropped_item` -- landed
      2026-09-25. Neither ever went through the `vb.register_entity` kind
      mechanism (players hardcoded `EntityKindId::kInvalid` at join, drops
      carried the reserved `world::kItemDropKind` sentinel, outside
      `S2C_EntityKindRegistry`'s dense id space) -- fixed with a new generic
      `vb.register_entity{represents = "player" | "item_drop"}` field
      (`PackRuntime::attach_session()` forwards the resolved id to two new
      `ServerSession` setters), not a hardcoded pack-name check in engine
      code. `content/base/entities/player.lua` (new) and `dropped_item.lua`
      (edited) register real, checked-in `visual = {...}` spritesheets
      (idle+walk, 4 facings) generated with a throwaway stdlib PNG writer (no
      art tools in this environment). See `STATE.md`'s "Current status" for
      the full write-up, including a gotcha around `install_entity_kind_
      registry(host)` ordering relative to `ServerSession`'s constructor.
      Full `vb_tests` 334/334 green, clean `-Werror` build.
      **Follow-up (2026-09-27), user-requested:** the real art's front/side/
      back poses were too visually similar to tell apart by eye ("not very
      obvious... front or back, or left or right"). `base:player` swapped to
      debug-styled art (a big F/R/B/L letter baked into each row) and a new
      `vb.register_entity{visual = {mirror = false}}` engine feature (default
      `true`, every other pack unaffected) lets a kind author a real,
      distinct pose per facing instead of mirroring one "side" pose for both
      left and right — `kEngineProtocolVersion` 22 -> 23. Also fixed a real
      pre-existing bug this surfaced: `EntityRenderer` picked a facings=4
      kind's pose using hardcoded facings=8 sector math the whole time (never
      updated from the entity's actual registered kind). See `STATE.md`'s
      2026-09-27 entry for the full write-up.
- [x] Real texture/atlas system landed 2026-09-23 (see
      `state/changelog-recent.md`): `vb.register_block{texture=...}` ->
      `S2C_BlockRegistry` -> a per-session `vb::render::TextureAtlas` built
      from Asset Sync (or, `--singleplayer`, straight off disk) -> real
      per-face UVs in `ChunkRenderer`. Proved on `base:stone`/`base:water`
      only (user-scoped) — **remaining:** every other base block (dirt,
      grass, sand, wood, leaves) still renders its old flat placeholder
      color; a full base-pack reskin is a separate follow-up pass, not a
      mechanism gap.
- [ ] `--singleplayer`'s registry-wiring gap is closed (Phase 5.1); no
      remaining item here.
- [ ] Manifest staleness: a pack that writes `vb.storage` *after* startup
      (not just at load time) goes stale for the rest of that server
      process's life — no shipped pack triggers this today, left unaddressed.
- [ ] Item grid widget for `UiRuntime` — needs a real item/inventory concept.

---

## Phase 5 — Minimum Playable Base ✅ met over `LoopbackTransport`

Goal: a small, coherent, playable multiplayer sandbox. `content/base` (blocks,
biomes, crafting, dropped items, real inventory, hotbar), block break/place
over the network with a Lua veto seam and hold-to-break timing, a full raygui
main menu / connect / settings flow, and play polish (day/night, chat, player
list, death/respawn). Verified via automated tests + `--headless` runs; the
literal "two real windows, playing together" manual pass across all 3
platforms has not been run by a human yet.
Full detail: `remaining_tasks/phase5.md`.

**Remaining:**
- [x] Player + dropped-item billboard sprite atlases (§11.3/3.5) — landed
      2026-09-25, see Phase 4's "Real base-pack art" entry.
- [~] Cross-chunk relight on edit (breaking a floor lets light into the chunk
      below) — still per-chunk from scratch each edit.
- [ ] Per-block hardness/tool break-time variation — one flat duration today
      (superseded in direction by Phase 6.17/6.18's punch-based combat, but
      the "vary by block/tool" idea itself is still open).
- [x] Keybindings screen (5.3 Settings) — a new Settings -> Keybindings
      raygui screen (`MainMenu::draw_keybindings`) lets a player click an
      action's key and press any physical key to rebind it, for the 6
      `MovementBindings` axes (forward/back/left/right/jump/sprint — mouse
      break/place buttons are left alone, matching the read-only
      `MOUSE_BUTTON_LEFT`/`RIGHT` convention elsewhere). Persisted as 6 new
      `key_*` int fields on `ClientConfig`/`client.toml`
      (`vb::core::save_client_config`/`load_client_config`), applied to
      `src/client/main.cpp`'s live `MovementBindings` immediately on Save —
      no restart needed, unlike window size/vsync. **Note:** this rebinds
      the *physical key*, distinct from Phase 6.19's `vb.register_keybind`
      name registry (which is about a pack reading `input.keybinds["jump"]`
      by name, not which key produces it) — the two compose: rebinding
      "Jump" here still shows up as the same `keybinds["jump"]` bit to Lua.
- [x] No connect-screen byte-progress bar (status-text-only) — landed
      2026-09-28. New `assetsync::ClientAssetCache::sync_total_bytes()`/
      `sync_received_bytes()` (`src/assetsync/cache.cpp`) derive real
      byte-progress from the existing `pending_` map on every call —
      `PendingFile::expected_size`/`buffer.size()` already carried everything
      needed, so there's no new running counter to keep in sync with
      `ingest_chunk`/`compute_missing`. `net::ClientSession::
      asset_sync_total_bytes()`/`asset_sync_received_bytes()` (`inc/vb/net/
      session.hpp`) forward through the session's own (possibly null)
      `asset_cache_` pointer, same "0 when there's no cache" posture
      `virtual_pack_fs()` already had. `render::MainMenu::draw_connecting()`
      gained an optional `float fraction = -1.0f` parameter (`-1` = unknown,
      keeps the original text-only layout exactly; `>= 0` grows the panel by
      one row and draws a real `GuiProgressBar`, same look `draw_loading()`
      already established for 7.1's post-join loading screen).
      `src/client/main.cpp`'s `kConnecting` case now computes a real fraction
      only when `!connecting_singleplayer` (no `ClientAssetCache` on that
      path) and `client->status() == kSyncingAssets` and the total is
      nonzero — every other handshake stage still shows text-only, matching
      the fact that only asset-streaming has a meaningful byte count at all.
      Verified: full `vb_tests` 390/390 green (2 new `assetsync_cache_test.
      cpp` cases: total/received tracked correctly across two pending files
      including a mid-transfer partial-chunk read, and 0/0 before any
      `compute_missing()` call), clean `-Werror` build of `vb_tests`/
      `voxel_browser`/`voxel_browser_server` (temporarily reconfigured
      `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
      reconfigured back to this dir's OFF default afterward). The actual
      rendered bar (a human watching it fill while downloading a real content
      pack) was **not** manually eyeballed — no GUI in this agent
      environment, same still-open caveat as every other rendering-adjacent
      pass in this file. Phase 7.1's own loading-screen entry named this as a
      likely shared prerequisite for its own still-open "connect screen byte
      progress" gap — that dependency is now closed, though 7.1's chunk-load
      fraction (a separate signal) is untouched by this pass.
- [ ] Live two-window manual playtest (chat + crafting + seeing each other,
      all at once, across all 3 platforms) — not yet run.

---

## Phase 6 — Lua-Driven Extensibility ✅ 6.1–6.16, 6.18, 6.20–6.21 done; 6.17 superseded

Goal: content packs override/extend engine defaults the way block
registration already does — biomes, entities, UI, input, data. Landed:
per-instance Lua entity objects (6.1), reactive `render(state)` UI (6.2),
custom keybinds (6.3), `vb.db`/`vb.crypto` (6.4), block-damage system (6.5),
player damage/death primitive (6.6), physics/day-night/inventory/chat/
item-drop override surfaces (6.7-6.11), read-only config visibility (6.13),
Lua-driven FastNoise2 worldgen pipeline + Voronoi biome selection (6.14), a
`content/examples/kitchen_sink` pack exercising all of the above (6.15), an
engine-neutral client HUD primitive (`ui.define_hud` + `kRect`, 6.16),
Growtopia-style discrete punch combat with block self-heal (6.18 — this is
the current shape of block breaking, **not** 6.17's original hold-to-break
plan, which was superseded same-day and is kept only for historical record),
right-click placing decoupled into content the same way (6.20 — closes
6.17/6.18's own "Placing... unaffected" gap; `player:place_block()` is now
the validated primitive, `content/base/mechanics.lua` decides when/what), and
a unified, pack-overridable interaction reach + physics/action read-back
surface (6.21 — `vb.action.set_params{reach=}`/`get_params()` and
`vb.physics.get_params()`).
Full detail: `remaining_tasks/phase6.md`.

**Remaining:**
- [x] **6.22: fall damage.** Landed 2026-09-27. New `net::ServerSession::
      set_landed_hook(fn(NetId, double impact_speed))` — fires once per
      player exactly on the tick a fall is arrested by hitting ground
      (`handle_input_batch`'s per-cmd loop compares `on_ground` before/after
      each `physics::step_movement` call; `impact_speed` is the pre-step
      downward velocity, captured before that tick's own small gravity
      increment rather than widening `physics::MoveState`'s public contract
      just to smuggle the post-integration value out). Raw notification
      only, no built-in formula/threshold — same "mechanism, not policy"
      posture as `RegionHooks`/`BlockBreakHooks`; `vb.on("player_landed",
      function(player, impact_speed) ... end)` is the new Lua event (only
      installed when a pack actually registers one, same zero-extra-cost-
      when-unused posture as every other opt-in hook). `content/base/
      fall_damage.lua` (new) is the reference policy: no damage below an 8
      m/s safe-speed threshold, then 1 HP per m/s above it, via the existing
      `player:damage(amount, "fall")` primitive (6.6) — no protocol change
      (server-local hook, nothing replicated). Still open: PvP/mob
      damage/hunger, which this item doesn't touch. Verified: full
      `vb_tests` 371/371 green (1 new `pack_runtime_integration_test.cpp`
      case drives a real gravity fall over a `LoopbackTransport` — spawn a
      player 5 blocks above real generated terrain with no jump input, pump
      real 0.05s ticks, confirm the hook fires exactly once at a plausible
      impact speed and never again while resting on the ground), clean
      `-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
      (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to
      this dir's OFF default afterward). Not manually eyeballed in a real
      window (no GUI in this agent environment), same still-open caveat as
      every other pass in this file — though this item has no rendering
      component at all, so that caveat matters less here than usual.
- [ ] No PvP, mob damage, or hunger — 6.6 only added the primitive
      (`player:damage`) and the decision hook (`player_death`); fall damage
      (just above) is the first real content caller of either.
- [x] Automatic despawn-on-health trigger for generic script entities —
      landed 2026-09-25. Opt-in per kind via `vb.register_entity{health=...}`
      (`EntityKindDef::max_health`, rejects a non-positive value at
      registration); a kind that never sets it keeps the exact pre-existing
      behavior (`entity:damage()` is notification-only, `on_hit` fires,
      nothing else). A kind that does opt in gets a per-instance current
      health (`ScriptEntity::health`, initialized from the kind default at
      `vb.world.spawn`), new `entity:get_health()`/`entity:set_health(v)`
      accessors (nil/error respectively for an untracked kind), and
      `entity:damage()` now decrements it and calls the existing
      `despawn_entity()` (fires `on_death`, deregisters) once it reaches 0 —
      the same despawn path `entity:remove()` already used, just triggered
      automatically instead of requiring the pack to track HP on `self` and
      call `:remove()` itself. Server-side bookkeeping only, no protocol
      change (never replicated — no client HUD reads a script entity's
      health). **Gotcha hit while wiring this up:** `on_hit` can itself call
      `self:remove()` (kitchen_sink's `sentry.lua` does exactly this,
      tracking its own hand-rolled hp) — the first draft reused the iterator
      taken before firing `on_hit` to then touch `.health` afterward, which
      is a use-after-erase if `on_hit` already despawned the entity
      (`tests/unit/kitchen_sink_pack_test.cpp`'s sentry test crashed on an
      MSVC STL iterator-debug assertion, not silently). Fixed by re-`find`ing
      after the `on_hit` call instead of reusing the pre-call iterator.
      Verified: full `vb_tests` 319/319 green (a new
      `pack_runtime_integration_test.cpp` case spawns a `health=5` kind and
      an opted-out kind side by side, proves 2 hits of 3 despawn the tracked
      one exactly at 0 without an explicit `:remove()` while the untracked
      one survives 1000 damage notification-only; a `pack_runtime_test.cpp`
      case covers the registration-time validation), clean `-Werror` build of
      `vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
      reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
      confirmed clean, reconfigured back to this dir's OFF default
      afterward).
- [x] Client-side kind-specific rendering for script entities — landed
      2026-09-25 (`EntityKind` id now drives the billboard's own
      width/height, via a new `S2C_EntityKindRegistry` (protocol version 19)
      and `vb.register_entity{width=, height=}`). Real per-kind sprite art
      (the `visual = {...}` schema below) landed as its own separate pass on
      2026-09-25, see Phase 4's own entry above. See `STATE.md`'s "Current
      status" for the full write-up.
- [x] Per-connection rate limiting on custom-keybind events — 6.3, closed
      2026-09-27 as part of Phase 3's flood guard (see that phase's own
      entry): custom-keybind bits ride inside `InputCmd`/`C2SInputBatch`
      like every other input field, so the generic
      `set_max_messages_per_second` token bucket (one token per message,
      any type) covers a custom-keybind flood the same way it covers an
      input-batch or chat flood — no keybind-specific limiter was needed on
      top of it.
- [x] Replicate block-damage *value* (not just begin/stop/complete) to nearby
      players — landed 2026-09-27. `S2C_BlockDamage` (protocol 25) fans out
      every live punch-count change (`ServerSession::punch()`/
      `update_block_punch_healing()`) to every player currently mirroring the
      chunk, not just whoever's punching; `client.break_progress()` (6.16)
      now reports a real fraction for the looked-at block instead of always
      `nullopt`. A default generic crack overlay (a darkening cube over the
      targeted block) ships alongside it. See `remaining_tasks/phase6.md`
      6.5's own writeup for the full detail.
- [x] Real crack-stage texture art + `crack_texture` override — landed
      2026-09-27, closing 6.5 in full. Protocol bumped **25 -> 26**:
      `BlockRegistryRecord` (`S2C_BlockRegistry`) gains `string
      crack_texture`, mirroring `vb::world::BlockType::crack_texture` (new
      field; `BlockRegistry::set_crack_texture()` mirrors `set_texture()`'s
      "attach without disturbing other already-frozen fields" posture for a
      pack re-declaring an existing block). `vb.register_block{crack_texture
      =...}` (`pack_runtime.cpp`) wires it through the same way `texture` is.
      New `vb::render::CrackAtlas` (`inc/vb/render/crack_atlas.hpp`, pure
      `build()`/GPU `upload()` split like `TextureAtlas`): one shared row of
      `kStages` (8) cells holds the engine's own procedurally-generated
      default crack pattern (deterministic per-stage line drawing, no art
      tools in this environment); a block with a valid `crack_texture` (a
      real `kStages`-frame spritesheet, decoded and sliced) gets its own
      extra row instead — a wrong-shaped or undecodable override falls back
      to the shared default row rather than failing pack load. New
      `vb::render::CrackOverlay` (GPU-only, not unit tested) replaces the old
      flat translucent-cube overlay in `src/client/main.cpp`'s `kPlaying`
      draw block: a persistent unit cube mesh whose texcoords are remapped
      into the resolved `CrackAtlas` rect (re-uploaded to the GPU only when
      the rect actually changes, not every frame) and drawn with the atlas
      texture bound, alpha still climbing with `break_progress` for the same
      "more damaged reads as more visible" cue the flat cube gave. **Real
      pre-existing bug found and fixed in the same pass, unrelated to
      `crack_texture` itself:** all three call sites that build or apply a
      `BlockRegistryRecord` (`src/server/main.cpp`'s and
      `src/client/main.cpp`'s `host.block_registry` callbacks, and
      `ClientSession::apply_block_registry()` in `src/net/session.cpp`) used
      an aggregate-init listing only the record's first 5-6 fields — so
      `max_damage` (and now `crack_texture`) was silently dropped on every
      one of these three hops the whole time. In practice this meant no
      client ever received a real nonzero `max_damage` for any block, so
      `client.break_progress()` (6.5's 2026-09-27 replication half, just
      above) was permanently `nullopt` regardless of what a block's real
      `max_damage` was — caught by a new regression test
      (`block_registry_test.cpp`) that failed against the unfixed code before
      the fix, confirming it was real, not theoretical. Verified: full
      `vb_tests` 363/363 green (8 new cases: a `block_registry_test.cpp`
      end-to-end regression case for the bug above, 2 `pack_runtime_test.cpp`
      cases for `crack_texture=`'s re-declare-attach and stored/default-empty
      behavior, a `protocol_test.cpp` round-trip case, and a new
      `crack_atlas_test.cpp` covering `CrackAtlas::build`'s default-row/
      override-row/wrong-shape-fallback/missing-from-vfs-fallback/stage-
      clamping behavior), clean `-Werror` build of `vb_tests`/
      `voxel_browser`/`voxel_browser_server` (temporarily reconfigured
      `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
      reconfigured back to this dir's OFF default afterward). The actual
      rendered crack-stage overlay (a human punching a block and watching a
      real textured crack pattern darken/progress on it) was **not** manually
      eyeballed — no GUI in this agent environment, same still-open caveat as
      every other rendering-adjacent pass in this file.
- [x] Movement/action key bindings extended into the 6.3 keybind registry
      (6.19) — `move_forward`/`move_back`/`move_left`/`move_right`/`jump`/
      `sprint`/`primary`/`secondary` are pre-registered by every
      `PackRuntime` before any pack script runs, so a pack can read
      `input.keybinds["jump"]` etc. exactly like a custom keybind. Resolves
      the open design question: keyboard/mouse movement axes are already
      boolean (held/not-held), so they fit the existing boolean-keybind
      shape with no format change. Physical-key rebinding (the WASD/Space/
      Shift mapping) landed separately as Phase 5.3's keybindings screen,
      below — this item's own scope (name registry, not physical keys)
      is fully closed.
- [x] HUD widgets wired to `report_click`/`report_change`/`report_list_change`
      — landed 2026-09-27. See `STATE.md`'s "Current status" for the write-up.
- [x] Player list / chat log / hotbar migrated off hardcoded C++ into
      `ui.define_hud` — landed 2026-09-27. New `WidgetType::kText` (`inc/vb/
      script/ui_runtime.hpp`) is a raw, colored, alignable text draw —
      distinct from `kLabel`, which goes through raygui's un-colorable
      `GuiLabel` — with an `align` (`left`/`center`/`right`) field so Lua can
      right-align/center text without being able to measure its own pixel
      width; the actual `MeasureText` call for that lives in
      `vb::render::UiRenderer::draw()` (`src/render/ui_renderer.cpp`), the
      raylib-linked layer, not `vb::script::UiRuntime`, which has no raylib
      dependency. Six new `client.*` read-only accessors mirror the existing
      `client.break_progress()`/`client.screen_size()` pattern:
      `player_name()`, `players()`, `chat_log()`, `chat_open()`,
      `inventory()`, `selected_slot()` — set once per frame from
      `src/client/main.cpp` via three new `UiRuntime` setters
      (`set_player_list`/`set_chat`/`set_inventory`) right next to the
      existing `set_break_progress`/`set_screen_size` calls.
      `content/base/ui/hud.lua` now composes the player list (top-right,
      own name in green), the chat scrollback log (bottom-left), and the
      hotbar (bottom-center, selected-slot outline) itself out of `rect`/
      `text` widgets, replicating the old hardcoded layout exactly. The chat
      **input box** (typing, Enter-to-send) deliberately stays a plain
      `GuiTextBox` in `src/client/main.cpp` — real keyboard text-entry
      capture, same posture as `MainMenu`, never a `UiRuntime` widget (this
      was the file's own pre-existing scope note, unchanged by this pass).
      No new interactive HUD widgets were added, so `report_click`/
      `report_change` HUD wiring (the item directly above) is still its own
      separate `[ ]` — nothing in this pass needed it. Verified: full
      `vb_tests` 350/350 green (no test exercises `content/base/ui/hud.lua`
      directly — it's evaluated only at real client runtime, same
      never-been-eyeballed caveat as every other rendering-adjacent item
      here), clean build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server` on `build-net-lua`. The actual rendered result
      (a human watching the player list/chat/hotbar draw identically to
      before) was **not** manually eyeballed — no GUI in this agent
      environment.
- [x] No punch-rate cooldown enforced engine-side — landed 2026-09-28 as
      `PunchParams::punch_cooldown_seconds` (default `0.0`, disabled — every
      existing direct-`punch()` test/call site is unaffected unless a pack
      opts in). New `Conn::punch_cooldown_remaining` (`inc/vb/net/
      session.hpp`), ticked down once per server tick in
      `system_network_io()` (mirrors `msg_tokens`' own per-tick refill loop
      immediately above it in the same function). `ServerSession::punch()`
      rejects (empty `PunchResult`, same "no-op" shape as "nothing within
      reach") a call still on cooldown, and **arms the next cooldown up
      front**, before target resolution — a whiff costs the same swing time
      a landed hit would, matching a real attack-rate cap rather than only
      throttling punches that happen to connect. Pack-facing via
      `vb.combat.set_params{punch_cooldown_seconds=...}`
      (`PackRuntime::effective_punch_params`), same shape as the existing
      `hit_radius`/`player_damage`/`heal_after_seconds`/
      `heal_interval_seconds` fields on that table — deliberately opt-in
      (no engine default swing rate), unlike the heal timers, which do ship
      a real default: there's no obviously-correct default attack rate the
      way there is for "how fast should a punched block start healing."
      Verified: full `vb_tests` 392/392 green (2 new `blockedit_test.cpp`
      cases: a configured cooldown rejects an immediate second swing but
      lets a later one land once enough real time has ticked past via
      `server.tick()`, and the disabled-by-default case proves two
      back-to-back swings with no cooldown set both land, matching every
      pre-existing punch test's own back-to-back-calls style), clean
      `-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
      (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to
      this dir's OFF default afterward). **Still open, unchanged by this
      pass:** no swing animation; PvP has no armor/knockback — this item
      only closed the cooldown half of its own original scope.
- [x] Held item / hotbar selection — landed 2026-09-25 (entity-management
      follow-up, closes 6.20's own gap). Placing used to always place a
      hardcoded `base_stone_id` regardless of what the player was carrying,
      because no primitive existed anywhere for a pack to ask "what slot is
      this player's hotbar on, and what's in it". Wire protocol bumped
      **21 -> 22**: `InputCmd` (within `C2S_InputBatch`) gains a `u8
      selected_slot` (0-based), reported every cmd exactly like `buttons`/
      `keybinds` — the client (`src/client/main.cpp`) reads number keys 1-9
      into a persistent `selected_slot` local (gated on `mouse_captured`,
      same as movement/break/place, so typing a digit into an open chat box
      never changes it) and outlines the selected slot in the hotbar HUD.
      Server-side, `ecs::PlayerInput::selected_slot` mirrors the latest
      processed value (`ServerSession::handle_input_batch`), readable via
      the new `ServerSession::selected_slot(NetId)`; a pack's
      `vb.on("player_input", ...)` handler chain can also override it (the
      input table's `selected_slot`, 1-based to match
      `player:get_inventory()`'s own 1-based array — reconstruction lives in
      `PackRuntime::Impl`'s new `selected_slot_from_table`), same
      veto/replace shape `net::ServerSession::PlayerInputOverride` already
      gave `move`/`yaw`/`pitch`/`buttons`/`keybinds`. Two new Lua primitives
      on `PlayerHandle` close the actual gap: `player:get_selected_slot()`
      (1-based) and `player:get_held_item()` (resolves that slot against the
      player's real inventory — `nil` if the slot is out of range or empty,
      the same shape `get_inventory()`'s own entries use). `content/base/
      mechanics.lua`'s right-click placing now reads `player:get_held_item()`
      instead of the hardcoded stone id — an empty/out-of-range slot places
      nothing, and a successful placement spends one unit via
      `player:take()`, so placing is a real inventory drain now (matches
      the existing break -> drop -> pickup -> inventory loop) rather than an
      infinite stone dispenser. **Gotcha hit while wiring the Lua bindings:**
      `pack_runtime.cpp`'s `PlayerHandle` usertype (20+ methods, all through
      sol2's template-heavy `new_usertype`) was already close enough to
      MSVC's object-file section-count ceiling that adding these two pushed
      it over (`fatal error C1128: number of sections exceeded object file
      format limit: compile with /bigobj`) — fixed with a per-source
      `/bigobj` on `pack_runtime.cpp` only (`src/script/CMakeLists.txt`);
      a second gotcha on the way there: `set_source_files_properties()` is
      directory-scoped to where it's *called*, not where the consuming
      target is defined, so setting it from `src/script/CMakeLists.txt` (as
      first tried) silently never reached `vb_core`'s build rule (defined in
      `src/core/CMakeLists.txt`) until adding CMake 3.18's
      `TARGET_DIRECTORY vb_core` argument. Verified: full `vb_tests`
      (see `STATE.md`'s "Current status" for the pass/fail count) green,
      including a new `netcode_test.cpp` round-trip case for
      `InputCmd::selected_slot`, a `pack_runtime_test.cpp` case for
      `get_held_item()`/`get_selected_slot()`'s no-session default, and a
      `pack_runtime_integration_test.cpp` case proving a real client's
      `InputCmd::selected_slot` reaches both accessors end-to-end. The
      actual hotbar-highlight rendering (a human pressing 1-9 and watching
      the outline move) was **not** manually eyeballed — no GUI in this
      agent environment, same still-open caveat as every other recent
      rendering-adjacent pass.
- [x] **6.21:** block-edit reach and punch reach unified into one
      pack-overridable `net::ActionParams::reach` (`inc/vb/net/
      world_replicator.hpp`), replacing both `WorldReplicator`'s old
      hardcoded, non-overridable `kMaxReachBlocks` constant and
      `ServerSession::PunchParams::reach` — `WorldReplicator::set_reach()`/
      `reach()` hold the one value `in_reach()`/`apply_block_edit()` and
      `ServerSession::punch()` (via `world_replicator()->reach()`) all read.
      New `vb.action.set_params{reach=...}`/`vb.action.get_params()` Lua
      binding (`PackRuntime::effective_action_params`,
      `src/script/pack_runtime.cpp`) — deliberately its own namespace, not
      `vb.combat`, so a pack author hunting for the mining-reach knob doesn't
      have to think to search "combat" for it; `vb.combat.set_params` kept
      only its genuinely combat-specific fields. Also added
      `vb.physics.get_params()` (effective `MoveParams` as a plain table).
      `content/base/mechanics.lua`'s placing raycast now calls
      `vb.physics.get_params().eye_height`/`vb.action.get_params().reach`
      instead of its own hardcoded `EYE_HEIGHT`/`max_dist` constants. Wired
      into both `src/server/main.cpp` and `src/client/main.cpp`'s
      `--singleplayer` path, same config-then-pack-override precedent as
      `move_params`/`punch_params`. Tests: a new `blockedit_test.cpp` case
      proves `WorldReplicator::set_reach()` widens both block-edit reach and
      `punch()` reach from the same one call; new `pack_runtime_test.cpp`
      cases cover `vb.action.set_params` (override + post-freeze rejection +
      built-in default) and `vb.physics.get_params()`/`vb.action.get_params()`
      round-tripping the effective values. Full `vb_tests` green (294/294)
      on `build-net-lua`.

---

## Phase 7 — World & UX Polish (7.1-7.6 done)

> User-requested (2026-09-19): four UX gaps, scoped as their own phase since
> none of them extend Phase 6's "default + override" system pattern the way
> 6.1-6.20 did — these are new engine surfaces (loading UI, fog, liquid
> collision/vision/regions) plus closing a real content gap found while
> reviewing Phase 6 (`ui/pause.lua`/`ui/inventory.lua`'s own "nothing opens
> this yet" comments).

- [x] **7.1 — Engine-side loading screen with progress, during initial world
      load.** Landed 2026-09-22: a new `AppState::kLoading` in
      `src/client/main.cpp`, entered right after a successful join (instead
      of dropping straight into `kPlaying`) and left once the initial
      view-box of chunks has streamed in (or an 8-second
      `loading_deadline` elapses, so a server whose real `view_distance` is
      smaller than this client guessed never gets stuck at <100%). Stage 1:
      `MainMenu::draw_loading(fraction, operator_title)`
      (`src/render/main_menu.cpp`) draws a generic `GuiProgressBar` the
      instant the state is entered, no data dependency — `fraction` is
      `client->chunk_store().size()` over an expected count mirroring
      `WorldReplicator`'s own `chunks_in_view` box shape
      ((2·view_distance+1)²·7, vertical radius 3 hardcoded both in
      `src/net/world_replicator.cpp`'s `Singleplayer` construction and
      `src/server/main.cpp`), clamped to 1.0 so a real server's smaller box
      still reads "done". Stage 2: operator branding is **text only** — the
      existing `S2CServerInfo::motd` (already replicated during the
      handshake, `ClientSession::server_info()`) drawn above the bar once
      non-empty; **no color knob** was added (would need a new
      `ServerConfig`/`S2CServerInfo` field + protocol version bump, judged
      out of scope for this pass — a real future addition, not a cut
      corner). The `kLoading` state itself also pumps `sp->tick()`/
      `client->tick()` and `chunk_renderer->sync()` every frame (higher
      submit/upload budget than `kPlaying`'s steady-state 8/frame) — it's
      what's actually driving the load, not a passive wait. Verified: full
      `vb_tests` 294/294 green, clean `-Werror` build of both binaries on
      `build-net-lua`; the windowed state-machine path itself (a human
      watching the bar move) was **not** manually eyeballed this pass — see
      Phase 5's still-open "Live two-window manual playtest" item, same
      caveat applies here.
      Previously planned scope, explicitly **not** covered by the above:
      the connect-screen's own byte-progress bar (asset-sync fraction) is
      still text-only — asset-sync never grew progress-fraction accounting,
      so `kLoading`'s bar reads from *chunk* streaming only. Explicitly
      **not** Lua-driven (unlike 6.16's HUD/6.2's UI,
      which deliberately pushed presentation into content) — this covers the
      window between "connected" and "first playable frame" (asset sync +
      first-chunk load), before any `PackRuntime`/`UiRuntime` content is even
      guaranteed to have loaded, so it can't depend on a pack being present
      to draw it. Needs a real progress fraction to report — the connect
      screen's own still-open gap (Phase 5's "No connect-screen byte-progress
      bar (status-text-only) — asset-sync never grew progress-fraction
      accounting") is very likely a shared prerequisite; likewise chunk-load
      completion needs some "how many of my initial view-distance chunks are
      in" signal that doesn't currently exist as a queryable fraction.
      **Decided (2026-09-19), two stages, not one:** stage 1 is a fully
      generic bar (no data dependency at all) drawn the instant the loading
      window opens, so it never waits on anything network-derived; stage 2
      overlays lightweight **operator**-level branding (title text/color)
      read from `server.toml` once that config actually arrives, the same
      "operator, not pack, persona" split 6.13 already drew for
      `ServerConfig`. Deliberately **not** a pack/Lua-themeable surface at
      all, and not going any further than text+color — the point of this
      screen is getting out of the way of loading the real content quickly,
      not hosting a themable UI system.
- [x] **7.2 — Distance fog, adjustable from Lua.** Landed 2026-09-22: a real
      GLSL shader (`src/render/chunk_renderer.cpp`'s `kFogVs`/`kFogFs`, GLSL
      330, loaded once via `LoadShaderFromMemory` in `ChunkRenderer`'s
      constructor) replaces raylib's default mesh shader on every chunk's
      material — a faithful copy of it (same attribute/uniform names, so
      raylib's own `DrawMesh` keeps auto-wiring `mvp`/`matModel`/
      `colDiffuse`/`texture0` unchanged) plus a linear fog mix at the very
      end of the fragment shader. `ChunkRenderer::set_fog(view_pos, sky,
      start, end)` sets the `fogViewPos`/`fogColor`/`fogStart`/`fogEnd`
      uniforms once per frame from `src/client/main.cpp`'s `kPlaying` case,
      reusing the exact `SkyColor` already computed for `ClearBackground`
      that frame — fog color can never drift from the sky, by construction
      (matches the 2026-09-19 decision below).
      Wire protocol (bumped to **16**, `docs/protocol.md`): `S2CFogParams`
      (`inc/vb/protocol/world.hpp`, type 52) is `f32 fog_start, f32
      fog_end`, sent between `C2S_Ready` and `S2C_JoinAccept` alongside
      `S2C_MoveParams`/`S2C_DayNightCurve` only if
      `HandshakeServerHost::fog_params` returns a value
      (`PackRuntime::effective_fog_params()` on the server side, driven by a
      pack's `vb.render.set_fog{start=, ["end"]=}` — `end` needs a quoted
      key, it's a Lua reserved word). Unlike move_params/day_night_curve
      there's no server-side universal default this replaces: the server
      doesn't know each client's own `view_distance`, so `nullopt` (no pack
      override) means each client computes its own default from its own
      config instead (`src/client/main.cpp`: `fog_end = view_distance *
      kChunkDim`, `fog_start = fog_end * 0.6`) — `ClientSession::
      fog_override()` stays `std::optional`, not a struct with an
      always-valid default, to make that "no server value, client
      improvises" case explicit rather than a fake zero-initialized frame.
      No color field on the wire either, matching the decision below.
      Verified: full `vb_tests` 298/298 green (protocol round-trip +
      `PackRuntime` Lua-binding tests for `vb.render.set_fog`, including the
      `end <= start`/missing-field rejection cases), clean `-Werror` build
      of both binaries on `build-net-lua`, a `--headless --singleplayer`
      smoke run (never touches `ChunkRenderer`, so this only confirms the
      protocol-version bump and handshake didn't regress anything). The
      actual shader output (a human watching fog fade in near the edge of
      view distance in a real window) was **not** manually eyeballed this
      pass — no GUI in this agent environment, same still-open caveat as
      Phase 5's "Live two-window manual playtest" and 7.1's own loading-bar
      verification.
      **Decided (2026-09-19): fog color is never an independent Lua-settable
      field** — it's always whatever the current day/night sky color already
      is (6.8's `sky_color_for_time()`), so fog reads as "distance to the
      same sky," not a separate tint that can drift out of sync with it
      (e.g. green fog under a red sunset sky). Only the **distance**
      parameters are Lua-adjustable: an engine default (matching current
      `view_distance`) plus a `vb.render.set_fog{start=, end=}`-style
      override, following the same pre-freeze "default + override" shape and
      replication path as `S2C_MoveParams`/`S2C_DayNightCurve` (6.7/6.8) so a
      dedicated-server pack's choice reaches every client, not just
      `--singleplayer`.
- [x] **7.3 — Walkable "liquid" blocks (water): collision, underwater
      rendering (same sky-color fog mechanism, tighter distance), and a
      generic region-enter/exit hook for entity effects.** Landed
      2026-09-23. Collision needed no change — `is_solid` already gated
      `step_movement`'s AABB checks and `base:water` was already registered
      non-solid (Phase 2), so walking into/through it already worked;
      verified, not rebuilt.
      Underwater rendering: `src/client/main.cpp`'s `kPlaying` fog block
      (7.2) now computes the camera's own eye voxel (`controller.position()`
      floored) each frame and checks
      `client->chunk_store().registry().is_liquid(eye_block)` — when true it
      overrides whichever `fog_start`/`fog_end` were already chosen (engine
      default *or* a pack's `vb.render.set_fog` override alike) with a fixed
      close preset (`fog_start = 2.0f`, `fog_end = 8.0f`) before calling
      `ChunkRenderer::set_fog`. No separate tint/color system, exactly the
      2026-09-19 decision — underwater is 7.2's same sky-color fog mechanism,
      just a much closer distance, so surfacing restores normal visibility
      immediately with zero extra state to track.
      **Follow-up fix, same day:** the fog change alone didn't fully solve
      "can't see underwater" — `src/world/chunk_mesh_snapshot.cpp`'s face
      culling (`mesh_chunk_from_snapshot`) had always treated a liquid
      neighbour exactly like an opaque one (`blocks_face()`'s `is_opaque(id)
      || is_liquid(id)`), symmetrically, for *both* the block being meshed
      and its neighbour. That meant an opaque block's face touching water
      got culled too — a submerged block (seafloor, cave wall under a lake)
      was invisible from the water side even though nothing was actually
      drawn over it, a pre-existing bug (`tests/unit/mesher_test.cpp`'s old
      "a water block on solid ground only shows its top face" test even
      encoded it as expected behavior) that 7.2/7.3's fog work simply made
      visible for the first time. Fixed by making culling depend on the
      *current* voxel's own type, not just the neighbour's: a new
      `face_culled(current, outside)` culls on any opaque neighbour
      (unchanged), but only culls on a liquid neighbour when `current` is
      itself liquid (water-water merges into one body, matches the
      still-passing "liquid blocks cull faces against each other" test) —
      an opaque block's face is never culled by a liquid neighbour, and a
      liquid's own face is still culled by an opaque one below/beside it (no
      point drawing a submerged water face nobody can reach). `blocks_face()`
      itself is untouched and still used for AO sampling, where "is there
      occluding stuff here" is the right generic question regardless of the
      current voxel's own type. Updated the affected mesher test's name/
      comment/expected quad count (stone now keeps all 6 faces, was 5; total
      11, was 10) rather than leaving the old wrong-by-design assertion in
      place.
      **Second same-day follow-up fix (user-reported with a screenshot):**
      the mesher fix alone made the lake surface look "broken, like there
      are holes" when viewed from *above* — not a geometry bug this time.
      `src/render/chunk_renderer.cpp`'s `tint_for()` gave water `alpha=200`
      (semi-transparent), a pre-existing Phase-2-era value that was
      inconsequential as long as submerged terrain was always culled (there
      was never anything behind the water quad to blend with). Once the
      mesher fix above made that terrain actually render, the same alpha let
      the real sandy lakebed blend through — but as flat, hard-edged,
      voxel-shaped patches (no wave/refraction shading exists to sell a soft
      "shallow clear water" look), which read as corrupted geometry rather
      than water. Fixed by bumping water to `alpha=255`, opaque like every
      other block, so the lake surface is solid-looking from outside again;
      underwater visibility while swimming is untouched by this, since it's
      driven entirely by the fog system (once the camera's own eye voxel is
      inside the water) and never depended on this material's alpha.
      Generic region hook: `BlockType::region` (`inc/vb/world/block.hpp`) is
      a new flag independent of `liquid` — `BlockRegistry::base()` sets it on
      `base:water` only (`src/world/block.cpp`); `vb.register_block{region=}`
      defaults it to the block's own `liquid` value unless given explicitly
      (`src/script/pack_runtime.cpp`), so a pack's liquid opts in
      automatically and a non-liquid custom block (a future poison cloud)
      opts in on request. Server-side, `ServerSession::update_region_occupancy()`
      (`src/net/session.cpp`, called once per `tick()` after the punch-heal
      pass) reads each playing player's own position from the same
      `interest_` entry `update_item_drops` already reads, floors it to a
      voxel, and diffs `reg.is_region(block)` against a new
      `region_occupancy_` map keyed by `NetId` to fire
      `ServerSession::RegionHooks::enter`/`exit` exactly on the crossing —
      never once per tick spent inside one. Both hook fields are
      `std::function`s left unset (a no-op early return) unless a pack
      registered `vb.on("region_enter", ...)` or `"region_exit"`
      (`PackRuntime::attach_session`), same zero-extra-per-tick-cost posture
      as `BlockBreakHooks`. `PackRuntime::Impl::run_region_event` fires
      `vb.on("region_enter"/"region_exit", player, pos, block_name)` — the
      block is resolved to its registered *name* (via
      `replicator->world().registry()`) rather than a raw id, so a pack
      checks `block == "base:water"` instead of needing to know an id.
      **Explicitly out of scope, per user instruction:** no flowing-liquid
      physics/spread (Minecraft's water-source/flow-level simulation) — the
      block stays static once placed, only collision + visuals + the hook
      are in scope. **Position, not full AABB:** the occupancy check is a
      single-point test at the player's own position (matching every other
      per-player system in `ServerSession`, e.g. reach/item-pickup), not the
      player's full collision box — REMAINING_TASKS' original "position/AABB"
      note left this open; a point check was judged sufficient (water's
      collision box already visually matches the voxel) and simpler.
      Verified: full `vb_tests` 300/300 green — new coverage is
      `world_test.cpp`'s base-set `is_region` assertions,
      `pack_runtime_test.cpp`'s `register_block{region=}` default/override
      matrix, and a new `pack_runtime_integration_test.cpp` end-to-end case
      that moves a real player in and out of a real `base:water` voxel over
      a `LoopbackTransport` and confirms `region_enter`/`region_exit` each
      fire exactly once per crossing (via two counted `player:give()` calls
      gated on the passed block name), not once per tick spent inside;
      clean `-Werror` build of both binaries (temporarily reconfigured the
      local `build` dir with `-DVB_WARNINGS_AS_ERRORS=ON` — off by default
      for a plain local build, but what every CI workflow already passes —
      confirmed a clean rebuild of `vb_tests`/`voxel_browser`/
      `voxel_browser_server`, then reconfigured back to this dir's original
      OFF setting afterward). The actual underwater visual (a human swimming
      and seeing
      the closer fog kick in) was **not** manually eyeballed — no GUI in
      this agent environment, same still-open caveat as 7.1/7.2's own
      verification notes.
- [x] **7.4 — Wire `content/base`'s existing UI screens to a real trigger, as
      a working example.** Landed 2026-09-23. Root cause was one level deeper
      than "just add a keybind": Phase 6.3's `vb.register_keybind`/
      `S2C_KeybindRegistry` gives a pack a *named* bit in `InputCmd.keybinds`,
      but nothing on the client ever mapped a **physical key** to a
      pack-registered custom name — only the pre-registered engine names
      (movement + `primary`/`secondary`, Phase 6.19) got a real key via
      `MovementBindings`/`sample_input_cmd`. Two things landed together:
    - A new `kCustomKeybinds` table in `src/client/main.cpp` (`{"base:pause",
      KEY_ESCAPE}`, `{"base:inventory", KEY_E}`) — a minimal hardcoded
      default, not a real settings-screen UI (that's still a further, separate
      step past Phase 5.3's movement-only rebind screen). Read
      unconditionally in `sample_input_cmd` (unlike the engine-name lookups,
      which stay gated behind `mouse_captured`) via the same
      `set_engine_keybind` bit-setter, since opening a pause/inventory screen
      must work whether or not the mouse is currently captured for looking
      around.
    - A new `content/base/keybinds.lua` registering `"base:pause"`/
      `"base:inventory"` and a `vb.on("player_input", ...)` rising-edge
      handler calling `player:open_ui("base:pause", {})` /
      `player:open_ui("base:inventory", { slots = player:get_inventory() })`
      — the exact pattern `kitchen_sink/keybinds.lua` already demonstrated,
      applied to `content/base`'s own screens. `ui/pause.lua`/
      `ui/inventory.lua`'s stale "nothing opens this yet" header comments
      updated to point at the new wiring.
      Verified: full `vb_tests` 300/300 green (`content_pack_test.cpp`
      exercises the new `content/base/keybinds.lua` load as part of loading
      the whole pack), clean build of both binaries on `build-net-lua`. The
      actual keypress → screen-opens behavior (a human pressing Escape/E in a
      real window) was **not** manually eyeballed — no GUI in this agent
      environment, same still-open caveat as every other Phase 7 item's own
      verification note.
- [x] **7.5 — Underwater fog tint should default to the liquid block's own
      color, overridable from Lua.** User-requested (2026-09-23), and
      **supersedes 7.2/7.3's 2026-09-19 decision** ("fog color is never an
      independent Lua-settable field... it's always whatever the current
      day/night sky color already is") for the underwater case specifically
      — that decision stays correct for *normal* (above-water) fog, but
      underwater fog tinted by the sky reads as wrong once you're actually
      submerged (water should tint the murk itself, not echo whatever color
      the sky happens to be at the time). Two parts:
    - [x] **Default**, landed 2026-09-23 alongside the real texture/atlas
      system (Phase 4's own item, `state/changelog-recent.md`): the
      underwater tint is now the **real average pixel color of `base:water`'s
      own synced texture** — `vb::render::TextureAtlas::build()` computes it
      once per session (decoding every block's texture and averaging its
      pixels, real `Image` data, not a guess) and `ChunkRenderer::
      underwater_tint(BlockId)` exposes it; `src/client/main.cpp`'s `kPlaying`
      block now passes that instead of `sky` to `set_fog()` whenever
      `is_liquid(eye_block)`. A liquid block with no texture (or before any
      atlas exists at all) still falls back to the old flat placeholder
      color exactly as this item originally scoped as its own interim step —
      that fallback is `vb::render::fallback_color_for()` now (moved out of
      `chunk_renderer.cpp`'s old `tint_for()`, same values, renamed).
    - [x] **Override**, landed 2026-09-27: `vb.render.set_fog{start=,
      ["end"]=, underwater_tint={r=, g=, b=}}` — `underwater_tint` is a new,
      independently-optional field on the same call, each channel `0-255`,
      validated the same way a bad `start`/`end` already was (all three
      channels required if the table is given at all; out-of-range rejects
      the whole call). Protocol bumped **23 -> 24**: `S2CFogParams` (type 52)
      gains `bool has_underwater_tint` followed by, only if true, `u8
      underwater_tint_r/g/b` — `has_underwater_tint = false` means "no
      override, client keeps its own texture-average/placeholder default,"
      not "black," same "absence is not a value" posture `fog_start`/
      `fog_end` already had. `src/client/main.cpp`'s underwater branch checks
      `client->fog_override()->has_underwater_tint` first and only falls back
      to `ChunkRenderer::underwater_tint()`'s texture-average default when a
      pack never set one. Above-water fog color is untouched and still never
      independently settable (2026-09-19's decision, unchanged) — this is
      strictly the underwater-only exception 7.5 always scoped it as.
      Verified: full `vb_tests` 350/350 green (a `protocol_test.cpp`
      round-trip case with the tint set, `pack_runtime_test.cpp` cases for
      the override applying/being absent/being rejected on a missing channel
      or an out-of-range one), clean `-Werror` build of `vb_tests`/
      `voxel_browser`/`voxel_browser_server` (temporarily reconfigured
      `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
      reconfigured back to this dir's OFF default afterward). The actual
      rendered tint swap (a human swimming with a pack-set override active)
      was **not** manually eyeballed — no GUI in this agent environment, same
      still-open caveat as every other recent rendering-adjacent pass.

- [x] **7.6 — World persistence: chunks survive a server restart.** Landed
      2026-09-25, reversing ARCHITECTURE_SPEC.md §18 row 5's 2026-09-17
      "lean toward LMDB" direction note after review (see that section for the
      updated status) in favor of flat per-region files -- no new
      `FetchContent` dependency, reuses the existing palette+RLE chunk codec
      as-is. New `vb::world::RegionStore` (`inc/vb/world/region_store.hpp`,
      `src/world/region_store.cpp`) groups chunks into one file per X/Z region
      (16x16 chunks; Y is never grouped -- generated worlds here are only a
      few chunks tall, unlike Minecraft's motivating case). Only *edited*
      chunks are ever persisted: `Chunk::revision() == 0` means "still exactly
      what worldgen produced," so regenerating on next load is equivalent and
      cheaper than reading+writing it; a chunk already cached/on-disk at its
      current revision is never re-encoded either. Writes are batched:
      `save_if_dirty()` only touches an in-memory per-region cache,
      `flush()` is the one call that actually rewrites a dirty region file.
      `ChunkLifecycleSystem` takes an optional `RegionStore*` (nullptr =
      disabled, same posture as every other opt-in engine seam): `update()`'s
      request step loads a wanted chunk from disk instead of submitting it to
      worldgen if one was saved there, and its unload step saves an edited
      chunk before evicting it. `WorldReplicator::set_region_store()` forwards
      straight through. `voxel_browser_server`'s `main.cpp` owns the
      `RegionStore` (nullptr when `server.toml`'s new `persist_world = false`),
      wires it into the replicator, sweeps every loaded chunk through
      `save_if_dirty()` + one `flush()` on a config'd `autosave_interval_seconds`
      cadence (default 60s; 0 disables periodic autosave, edits still save on
      chunk unload) and unconditionally once more right before the process
      exits. Three new `server.toml` keys: `persist_world` (default `true`),
      `world_dir` (default `"world"`, relative like `content_pack`),
      `autosave_interval_seconds` (default `60.0`).
      **Deliberately out of scope, per the AskUserQuestion decision that
      shaped this pass:** no LZ4/zstd framing on region files yet (chunk
      codec's own RLE is the only compression here -- ARCHITECTURE_SPEC §18
      row 4's "chunk compression" item still covers adding that uniformly);
      `--singleplayer`'s in-process integrated server (`src/client/main.cpp`)
      is **not** wired to a `RegionStore` at all -- it explicitly has no
      `ServerConfig`/`server.toml` on that path (see its own
      `make_singleplayer_pack_runtime` comment), so persistence there is a
      separate follow-up, not a cut corner of this one; a corrupt/unreadable
      region file is logged (`VB_WARN`) and treated as "chunk never saved"
      (regenerates from worldgen), never fatal -- no migration/versioning
      story exists yet for a region file format change (version 1 today).
      Verified: full `vb_tests` 312/312 green (5 new cases in
      `region_store_test.cpp`, including one that drives a real
      `ChunkLifecycleSystem` through `WorldReplicator` end-to-end -- edit a
      block, walk far enough to unload+save the chunk, walk back and confirm
      it's loaded from disk with the edit intact rather than regenerated),
      clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server` (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
      dir's OFF default afterward -- same verification pattern as every other
      recent phase). Also manually ran `voxel_browser_server.exe --ticks 40`
      against a fresh working directory with no client ever connecting:
      confirmed no `world/` directory is created at all when nothing was ever
      edited (matches the "only persist edits" design, not a missed case).

---

## Cross-Cutting / Continuous

- [ ] Keep `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` in lockstep with every
      wire change.
- [ ] Every new `vb/protocol` struct gets a round-trip + fuzz test.
- [ ] Sanitizer (ASan/UBSan) debug CI job; TSan job for the threaded subsystems.
- [ ] Determinism golden-value CI gate stays green across platforms.
- [ ] Soak test target (N simulated clients, random walk + edits) run nightly or
      pre-release; watch queue growth + leaks.
- [ ] Perf budget checks: chunk mesh time, snapshot size, frame time — track in a
      simple benchmark harness.
- [ ] `--headless` stays functional for both binaries (CI + integration tests).
- [ ] Address the remaining open item(s) in `ARCHITECTURE_SPEC.md` §18 as
      their blocking phase arrives (renumbered from §19; most rows are
      already resolved or have a noted direction — Q4 chunk compression is
      the one still fully open; Q5 persistence and Q6 auth have a direction
      set but aren't implemented yet); record decisions in that section.

---

## Deferred (post first-playable)

`--singleplayer`'s integrated server wired to `RegionStore` (7.6 landed it for
the dedicated server only) · region file LZ4/zstd framing (7.6's chunk
payloads are RLE-only today) · region file format versioning/migration ·
token auth verification · audio subsystem + Lua sfx/music API · server-side
plugin hot-reload · entity-entity physics/mounts/projectiles · particle
system beyond block-break puffs · compression tuning (zstd, snapshot deltas,
bit-packed inputs) · dedicated server browser/master list · modding
(stacked packs, dependency resolution) · rule-based decorative structure
placement for worldgen (depends on 6.14, which landed the prerequisite —
this item itself not attempted) · Voronoi biome-cell resolution caching
(6.14, no perf problem observed yet) · CSS-like declarative layout for
`UiRuntime` widgets (today: absolute pixel positioning only).

Full reasoning for each: `remaining_tasks/deferred.md`.
