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
- [ ] Surface `ENGINE_PROTOCOL_VERSION` mismatch in the client connect UI
      (both FSMs already reject it; needs the Phase 5.3 main-menu error path).
- [ ] macOS CI doesn't build `VB_WITH_NET` (universal arm64+x86_64 build vs.
      single-arch brew protobuf) — needs a universal protobuf, see `build_macos.yml`.
- [ ] The two-client replication test runs over `LoopbackTransport` only;
      re-run over `GnsTransport`.

---

## Phase 2 — World State & Terrain Generation ✅ met (2026-09-11)

Goal: server generates terrain, streams chunks, client meshes and renders
them. `PalettedChunkStore`, `World`, fBm heightmap worldgen (determinism gate
green on all 3 platforms), per-chunk `LightEngine` + cross-chunk vertical
sky occlusion, chunk replication + `ClientChunkStore`, hand-rolled
face-culled mesher with a background mesh worker pool (Cellulose/greedy-merge
was tried and reverted — see `ARCHITECTURE_SPEC.md` §19 Q2 — hand-rolled
meshing is permanent).
Full detail: `remaining_tasks/phase2.md`.

**Remaining:**
- [ ] Horizontal cross-chunk light propagation (sideways-only spill under a
      horizontal overhang spanning a chunk border) — still per-chunk-only.
- [ ] Frustum culling, transparent second pass, texture atlas — Phase 4.

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
- [ ] System runner with explicit ordering (§7.2) — only worth adding once a
      Lua entity kind needs to iterate the registry generically.
- [ ] Client-side lightweight registry (currently `ClientSession` holds
      predicted state inline).
- [ ] Per-player rate limit / flood guard belongs with `GnsTransport`.
- [ ] Step-up jerk: physics is exact but visually abrupt; needs a render-only
      eye-height smoothing layer client-side (not attempted).
- [ ] Wall-clock `server_time_est` + smoothing on the client (needs
      `GnsTransport` RTT — loopback has no latency to estimate).
- [ ] `SpriteVisual` component (atlas handle, `facings`, per-clip frame
      lists) for entity kinds — nothing produces one until 4.2.
- [ ] Real billboard art (atlas, per-clip frames) — pack-defined, waits on
      4.2's `visual = {...}` + Asset Sync + base-pack sprites (5.1).

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
- [ ] Custom `require` over the virtual pack FS + per-callback wall-clock
      budget — needs the synced asset FS (4.4), still deferred.
- [ ] `EntityKind` tick/spawn/hit/death callbacks wired into real systems —
      superseded in practice by Phase 6.1's hardcoded-system approach; the
      formal `SystemRunner` itself is still `[ ]` (see Phase 3.1).
- [ ] `register_entity`'s `visual = {...}` sub-table (variant/facings/clips)
      for 3.5's `entity_renderer` — schema finalized, not implemented; nothing
      reads a per-kind visual def yet.
- [ ] Client meshing/atlas driven by the received block registry beyond
      solid/opaque/light — still just untextured cubes; waits on a real
      texture/model concept (4.4/5.1).
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
- [ ] Player + dropped-item billboard sprite atlases (§11.3/3.5) — replaces
      the flat placeholder quad; not started.
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
- [ ] No connect-screen byte-progress bar (status-text-only) — asset-sync
      never grew progress-fraction accounting.
- [ ] Live two-window manual playtest (chat + crafting + seeing each other,
      all at once, across all 3 platforms) — not yet run.

---

## Phase 6 — Lua-Driven Extensibility ✅ 6.1–6.16, 6.18 done; 6.17 superseded

Goal: content packs override/extend engine defaults the way block
registration already does — biomes, entities, UI, input, data. Landed:
per-instance Lua entity objects (6.1), reactive `render(state)` UI (6.2),
custom keybinds (6.3), `vb.db`/`vb.crypto` (6.4), block-damage system (6.5),
player damage/death primitive (6.6), physics/day-night/inventory/chat/
item-drop override surfaces (6.7-6.11), read-only config visibility (6.13),
Lua-driven FastNoise2 worldgen pipeline + Voronoi biome selection (6.14), a
`content/examples/kitchen_sink` pack exercising all of the above (6.15), an
engine-neutral client HUD primitive (`ui.define_hud` + `kRect`, 6.16), and
Growtopia-style discrete punch combat with block self-heal (6.18 — this is
the current shape of block breaking, **not** 6.17's original hold-to-break
plan, which was superseded same-day and is kept only for historical record).
Full detail: `remaining_tasks/phase6.md`.

**Remaining:**
- [ ] No fall damage, PvP, mob damage, or hunger — 6.6 only added the
      primitive (`player:damage`) and the decision hook (`player_death`); no
      content calls either yet.
- [ ] No automatic despawn-on-health trigger for generic script entities (no
      health primitive exists; a pack tracks HP on `self` itself) — 6.1.
- [ ] No client-side kind-specific rendering for script entities (`EntityKind`
      id threads through but nothing branches on it) — 6.1.
- [ ] Per-connection rate limiting on custom-keybind events — 6.3 (folds into
      Phase 3.2's still-unimplemented flood guard).
- [ ] Replicate block-damage *value* (not just begin/stop/complete) to nearby
      players — 6.5, prerequisite for a crack overlay.
- [ ] Default generic crack overlay + `crack_texture` override — 6.5, blocked
      on the real texture/atlas system (4.3/5.1).
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
- [ ] HUD widgets aren't wired to `report_click`/`report_change` (display-only
      for now) — 6.16.
- [ ] Player list / chat box / hotbar are still hardcoded C++, not migrated
      to `ui.define_hud` — 6.16.
- [ ] No punch-rate cooldown enforced engine-side; no swing animation; PvP has
      no armor/cooldown/knockback — 6.18, deliberate scope cuts.

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
- [ ] Address the 6 open questions in `ARCHITECTURE_SPEC.md` §19 as their blocking
      phase arrives; record decisions in that section.

---

## Deferred (post first-playable)

World persistence (region files, save/load, disk eviction) · token auth
verification · audio subsystem + Lua sfx/music API · server-side plugin
hot-reload · entity-entity physics/mounts/projectiles · particle system
beyond block-break puffs · compression tuning (zstd, snapshot deltas,
bit-packed inputs) · dedicated server browser/master list · modding
(stacked packs, dependency resolution) · rule-based decorative structure
placement for worldgen (depends on 6.14, which landed the prerequisite —
this item itself not attempted) · Voronoi biome-cell resolution caching
(6.14, no perf problem observed yet) · CSS-like declarative layout for
`UiRuntime` widgets (today: absolute pixel positioning only).

Full reasoning for each: `remaining_tasks/deferred.md`.
