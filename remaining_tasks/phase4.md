## Phase 4 — The "Browser" Engine (Scripting & Assets)

> Full history for this phase; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.

Goal: server logic + content defined in Lua; client auto-downloads pack assets;
Lua-defined UI.

### 4.1 Lua runtime (`vb_core/script`)  🚧

- [x] Embed Lua 5.4 (`cmake/lua` wrapper) + sol2 **v3.5.0** (bumped from 3.3.0 —
      Clang ≥ 18 incompat). `VB_WITH_LUA` on in all three CI build workflows.
- [x] `vb::script::Vm` — pImpl over `sol::state`; `do_string` (source only,
      traceback capture), `ScriptResult` / `core::ScriptError`. Stub build when
      `VB_WITH_LUA` is off.
- [x] Sandbox env (§10.2): curated libs, `os`/`io`/`load`/`require`/`package`/
      `collectgarbage` nilled, `debug` → `traceback` only, instruction-count
      hook, ceiling allocator.
- [x] Sandbox-escape tests (`tests/unit/script_test.cpp`): os/io/load absent,
      runaway loop → `kBudgetExceeded`, memory bomb → `kOutOfMemory` (recoverable).
- [ ] Custom `require` over the virtual pack FS + per-callback wall-clock budget
      — deferred to 4.4 (needs the synced asset FS).
- [ ] Decision #1 (sol2) — recorded; **done**.

### 4.2 Server Lua API (§10.3)  🚧

- [x] `vb::script::PackRuntime` (`inc/vb/script/pack_runtime.hpp` +
      `src/script/pack_runtime.cpp`) — pImpl'd like `Vm`, stub build when
      `VB_WITH_LUA` is off. `Vm` gained `native_impl()` + a new internal
      `vm_internal.hpp` (shared `Vm::Impl`/`sol::state`) so binding code can
      reach the real Lua state without putting sol2 in any public header.
- [x] Registration API: `register_block` (idempotent by name via new
      `BlockRegistry::add_or_get`, rejected after `freeze()`), `register_item`,
      `register_entity` (captures `on_spawn`/`on_tick`/`on_hit`/`on_death` —
      **nothing dispatches them yet**, waits on 3.1's EnTT registry),
      `register_biome`/`register_craft` (captured; the engine itself still
      has no consumer, but as of 5.1 `content/base/crafting.lua` builds a
      real, working crafting feature on top of `register_craft` + the
      generic `player:give()`/`take()` primitives — see 5.1 below).
      `worldgen.set_pipeline` **not implemented** (out of scope — worldgen
      pipeline swap deferred, see below).
- [x] Freeze registries after pack load (`PackRuntime::freeze()`); stable
      `BlockId`s via `add_or_get`. **Known gap:** the server registry starts
      from `BlockRegistry::base()` and Lua-added ids beyond it are invisible
      to any client until 4.3 (`S2C_BlockRegistry`) ships — logged as a
      warning, not enforced.
- [x] Runtime world API: `get_block`/`set_block`/`raycast`/`spawn`.
      `raycast` shares one new `vb::world::raycast_voxel()` (extracted from
      `src/client/main.cpp`'s block-selection code, `inc/vb/world/raycast.hpp`)
      with the client. `spawn` logs + returns `nil` (no entity system to spawn
      into). `set_block` **known gap:** doesn't run the relight cascade
      `C2S_BlockEdit` does.
- [x] Entity + player API: one merged Lua usertype (`entity:`/`player:` are
      the same object — no non-player entity exists to justify two types)
      exposing `get_pos`/`set_velocity`/`remove` (no-op, logged)/
      `get_inventory`/`give`/`send_message`/`open_ui`/`get_name`. `send_message`
      / `open_ui` are real wire messages now — see 4.2's protocol addition
      below; no client handles them yet (4.5).
      `ServerSession` gained `set_player_velocity`/`conn_for_player`/
      `player_name` (+ a `name` field on its `Conn`, + `net_id` on
      `SessionPlayerLeft`) to support this.
- [x] Event bus: `vb.on("player_join"|"player_leave"|"block_break"|
      "block_place"|"player_interact"|"chat"|"tick", handler)` with veto
      returns. `player_join` wraps `HandshakeServerHost::authenticate` (a real
      pre-join veto, `install_join_veto`); `block_break`/`block_place` +
      per-block `on_break`/`on_place` fire from a new `WorldReplicator::
      BlockEditHooks` seam at `apply_block_edit` (closes the seam this doc
      called out under 5.2) — `on_break`/`on_place`'s return value (a
      would-be drop) is logged only, not materialized (waits on 5.1 items).
      `chat`/`player_interact` are wired generically
      (`dispatch_chat`/`dispatch_player_interact`) but nothing calls them yet
      — no `C2S_Chat`/interact message exists.
- [x] Scheduling: `vb.after`, `vb.every` (repeating, catches up on a stalled
      tick, capped at 8 fires/dispatch).
- [x] `vb.storage` — metatable-backed proxy over an `nlohmann::json` object
      (new dependency, header-only), persisted to `<content_pack>/storage.json`,
      flushed once per tick when dirty.
- [x] Protocol: `S2C_Chat` (101) + `S2C_OpenUi` (103) now have real codecs
      (`inc/vb/protocol/chat.hpp`) — `kEngineProtocolVersion` bumped 3 → 4,
      `docs/protocol.md` updated. Round-trip tested.
- [x] Unit tests (`tests/unit/pack_runtime_test.cpp`) + integration tests over
      a real `ServerSession`/`ClientSession`/`LoopbackTransport`
      (`tests/unit/pack_runtime_integration_test.cpp`): block-edit veto +
      `on_break` end-to-end, `player_join` veto end-to-end, `player_leave`
      dispatch with the right net id.
- [ ] `EntityKind` tick/spawn/hit/death callbacks wired into `ScriptPreTick` /
      `ScriptPostTick` systems — waits on 3.1's EnTT registry; `register_entity`
      already captures the callbacks, nothing iterates entities to call them.
- [x] Lua-driven worldgen pipeline replaces the Phase 2 hardcoded one —
      moved to Phase 6.14 (extensibility push, FastNoise2 backend); tracked
      there now instead of here. Landed 2026-09-18 — see that item.
- [ ] `register_entity`'s `visual = {...}` sub-table (`variant` frame-size
      lookup, `texture`, `facings`, `origin`, `clips` grid) for 3.5's
      `entity_renderer`, schema finalized 2026-09-17 — see `ARCHITECTURE_SPEC.md`
      §11.3. Not implemented yet; nothing reads a per-kind visual def (3.5
      still hardcodes a flat placeholder). Also needs the per-instance
      `entity.visual_override` merge (`ScriptState`) for reskins (e.g. player
      skins) once this lands.

### 4.3 Block registry replication  ✅ (name/solid/opaque/liquid/light only)

- [x] `S2C_BlockRegistry` (40) message (`inc/vb/protocol/world.hpp` +
      `src/protocol/world.cpp`): `varint n` + `n × {name, solid, opaque,
      liquid, light_emission}`, index == `BlockId`. Round-trip tested.
      `kEngineProtocolVersion` bumped 4 → 5. **No model/face-texture/
      collision-shape fields** — `BlockType` doesn't have them yet (waits on
      4.4 asset sync + 5.1 base pack); this phase only replicates what
      already exists.
- [x] Sent between `C2S_Ready` and `S2C_JoinAccept` (spec §8.3 order) via a
      new opt-in `HandshakeServerHost::block_registry` hook
      (`std::optional<vector<BlockRegistryRecord>>`, default `nullopt` = no
      frame sent at all) — every pre-4.3 host/test that never heard of this
      keeps identical wire behavior. `src/server/main.cpp` wires the
      dedicated server's live (possibly Lua-extended) registry into it,
      closing the exact gap `PackRuntime::freeze()` warns about (4.2).
- [x] Client (`ClientSession::tick`, `src/net/session.cpp`) intercepts
      `S2C_BlockRegistry` unconditionally (not through `ClientHandshake`'s
      strict per-state FSM) — robust to it arriving just before or after
      `JoinAccept`, since on real `GnsTransport` it travels on a different
      lane with no cross-lane ordering guarantee. Rebuilds a
      `world::BlockRegistry` in wire order and swaps it into
      `ClientChunkStore` via a new `set_registry()` setter.
- [x] Tests: `tests/unit/protocol_test.cpp` round-trip;
      `tests/unit/block_registry_test.cpp` (new) — a custom registry reaches
      a joined client end-to-end over `LoopbackTransport`, and a host that
      never opts in leaves the client on its own `base()` (regression guard
      for every other session-based test).
- [ ] Client meshing/atlas driven by the received registry beyond
      solid/opaque/light — still just untextured cubes (Phase 2 status);
      waits on a real texture/model concept (4.4/5.1).
- [ ] `src/client/main.cpp`'s singleplayer path doesn't wire this (no
      `PackRuntime` there yet, so its registry is always `base()` anyway —
      not a gap in practice, just unexercised).

### 4.4 Asset Sync Protocol (§9)  ✅ (progress accounting + UI deferred)

- [x] Server startup: walks the pack dir (`vb::assetsync::build_manifest`,
      `inc/vb/assetsync/manifest.hpp` + `src/assetsync/manifest.cpp`),
      rejects `..`/absolute paths/symlinks escaping the root, hashes every
      file (xxHash3-128, `XXH3_128bits`), builds `Manifest` + `manifest_hash`.
      Enforces `ServerConfig::asset_max_file_mb`/`asset_max_total_mb` as a
      hard startup failure, not a per-client thing. Gated behind
      `VB_WITH_COMPRESSION` (disabled build: `kDisabled`, whole feature
      no-ops gracefully like `VB_WITH_NET` off).
      **Dependency landmine fixed:** lz4 vendors its own private, older
      `xxhash.h` with no XXH3 API; `src/core/CMakeLists.txt` now links
      `xxHash::xxhash` *before* `LZ4::lz4` so `#include <xxhash.h>` resolves
      to the real one (see `docs/protocol.md`'s dependency table).
- [x] Messages: `C2S_AssetManifestRequest`, `S2C_AssetManifest`,
      `C2S_AssetRequest`, `S2C_AssetData` (chunked, lane 3) —
      `inc/vb/protocol/assetsync.hpp` + `src/protocol/assetsync.cpp`, round-trip
      tested. `kEngineProtocolVersion` bumped 5 → 6 (a structural handshake
      change, not just additive). Progress accounting (bytes-vs-manifest-total
      for a UI progress bar): not added — no connect-screen UI exists yet
      (waits on 5.3).
- [x] Client content-addressed cache (`vb::assetsync::ClientAssetCache`,
      `inc/vb/assetsync/cache.hpp` + `src/assetsync/cache.cpp`): disk layout
      `<cache_dir>/<2-hex-prefix>/<32-hex-hash>`, hand-rolled `index.bin`
      (write-to-.tmp-then-rename) mapping hash → `{size, last_used}` for LRU
      eviction to `ClientConfig::asset_cache_mb`. Default dir is a new
      `vb::core::user_cache_dir()` (`inc/vb/core/paths.hpp`, OS-appropriate:
      `%LOCALAPPDATA%`/`~/Library/Caches`/`$XDG_CACHE_HOME` or `~/.cache`),
      overridable via a new `ClientConfig::asset_cache_dir` +
      `--asset-cache-dir`.
- [x] Transfer state machine: three new `ServerHandshakeState` values
      (`kAwaitingAssetManifestRequest`/`kAwaitingAssetRequest`/
      `kStreamingAssets`) and two new `ClientHandshakeStatus` values
      (`kAwaitingAssetManifest`/`kSyncingAssets`) sit between `AuthResult`
      and `Ready` unconditionally now. `kStreamingAssets` is server-paced
      (`ServerHandshake::pump_assets()`, called once per tick from
      `ServerSession::tick()`'s existing non-playing-connection loop — a
      small per-tick chunk budget, not literal byte-in-flight windowing).
      Per-file hash verify + abort-on-mismatch/size-cap-breach lives in
      `ClientAssetCache::ingest_chunk` (spec §9.4, wired to `on_asset_chunk`
      returning `false` → `ClientHandshake::fail()`).
- [x] Virtual pack filesystem in memory (`ClientAssetCache::virtual_fs()`,
      exposed on `ClientSession` as `virtual_pack_fs()`) — assembled from
      cache reads + completed transfers. **Nothing consumes it yet**: Lua
      `require` (4.1), texture/model/UI loaders don't exist regardless of
      this landing.
- [x] Handshake ordering: `AssetManifestRequest → AssetManifest →
      AssetRequest → AssetData×N` sits between `AuthResult` and `Ready`
      (before `S2C_BlockRegistry`/`JoinAccept`, confirmed against the §8.3
      diagram) — not "before BlockRegistry/JoinAccept" loosely, exactly
      between Auth and Ready as spec'd.
- [x] Reconnect fast-path — **scoped down, confirmed with the user**: not
      persisted across process restarts. `C2S_AssetManifestRequest.
      known_manifest_hash` lets a *same-process* reconnect skip the manifest
      listing too, but a restarted client always gets the full listing —
      its on-disk CAS still makes `compute_missing()` return empty and the
      *data transfer* zero-byte regardless, which is what the integration
      test actually asserts. A literal cross-restart `manifest_hash` skip
      (persisting `HandshakeClientHost::last_known_manifest_hash`'s value to
      disk) is a cheap, unimplemented follow-up if wanted.
- [x] Integration test (`tests/unit/netcode_test.cpp`): a cold client
      downloads a real temp content pack and ends up byte-identical
      (`virtual_pack_fs()` vs. on-disk source); a second connection (fresh
      `ClientAssetCache` instance, same cache dir — simulating a process
      restart) asserts the server's `asset_file_bytes` hook is never called.
      Plus dedicated unit tests: `tests/unit/assetsync_manifest_test.cpp`
      (determinism, content-sensitivity, symlink-escape rejection, size
      caps) and `tests/unit/assetsync_cache_test.cpp` (missing-hash
      computation, hash-mismatch rejection leaves no stray file, LRU
      eviction under a tiny cap).
- [x] **Real bug found and fixed (2026-09-18):** the manifest is a
      startup-time snapshot (`src/server/main.cpp`'s `build_manifest` call,
      once), but `vb.storage`'s writes are deferred to the first
      `dispatch_tick()` (Phase 4.2's dirty-flag design) — which runs
      *after* the manifest is already built. Any pack that writes
      `vb.storage` during `load_content_pack` (both `content/base` and
      `content/examples/kitchen_sink` do, in their own `init.lua`'s
      boot-counter demo) hit a **guaranteed, first-connect asset-sync
      failure**: the manifest hashed `storage.json`'s stale pre-write bytes,
      then the first tick silently rewrote the file out from under that
      hash before any client's `asset_file_bytes` fetch, so verification
      failed with `"asset transfer failed (hash mismatch or size cap)"` —
      every time, on every machine, completely independent of build config
      (initially misdiagnosed as a `VB_WITH_COMPRESSION` mismatch across
      machines before being traced to this). Fixed with a single
      `pack_runtime.flush_storage()` call between `freeze()` and
      `build_manifest()`.
- [ ] **Not fixed, same class of bug, lower priority:** the manifest is
      still only ever built once at startup — if a pack writes `vb.storage`
      again *after* that point (a chat command, a timer, anything during
      normal play, not just load-time) its manifest entry goes stale for
      the remainder of that server process's lifetime; a client connecting
      afterward gets the *old* hash but the *new* bytes on the next resync
      of the same connection, or simply a permanently-wrong (but
      consistent, since neither side updates) hash that no current pack
      happens to trigger. No shipped pack does this today, so left
      unaddressed; would need either re-hashing just that one manifest
      entry on every `flush_storage()` call (cheap, targeted) or accepting
      that `vb.storage`/`vb.db` shouldn't live inside the asset-synced pack
      root at all (a bigger, unscoped redesign).

### 4.5 Client UI VM + raygui (§10.4)  ✅ (item grid + base pack deferred)

- [x] Separate restricted client VM: `vb::script::UiRuntime`
      (`inc/vb/script/ui_runtime.hpp` + `src/script/ui_runtime.cpp`) — a
      second `Vm` distinct from the server's `PackRuntime`, pImpl'd the same
      way, disabled-stub when `VB_WITH_LUA` is off. No world/net access of
      its own; outgoing events route through an attached `ClientSession&`.
- [x] `ui.define(name, render_fn)` declarative layout: `label`, `panel`,
      `button`, `list`, `text input` (spec's set minus **item grid**,
      deferred — needs real items, waits on 5.1). Originally evaluated once
      at `open()` and never re-laid-out afterward; superseded by Phase 6.2's
      per-frame `render(state)` reactive model below.
- [x] C++ renderer mapping layout → raygui: `vb::render::UiRenderer`
      (`inc/vb/render/ui_renderer.hpp` + `src/render/ui_renderer.cpp`),
      split across the core/render boundary from `UiRuntime` the same way
      `ClientChunkStore`/`ChunkRenderer` already are — `UiRuntime` emits a
      plain `Widget` list (no raygui), `UiRenderer` draws it (no sol2) and
      reports back which widgets fired an interaction this frame.
- [x] `player:open_ui(name, ctx)` server→client: `ClientSession` gained
      `take_open_ui()` (drains a pending `S2C_OpenUi`) + `send_ui_event()`.
      `on_click`/`on_change`/`on_close` → `ui.send_event(...)`/`ui.close()`
      → real `C2S_UiEvent{ui_name, widget_id, event_kind, value_json}`
      (`inc/vb/protocol/chat.hpp`, round-trip tested) →
      `ServerSession::set_ui_event_handler` (a plain `std::function`, no
      script dependency — same shape as `WorldReplicator::BlockEditHooks`)
      → `PackRuntime::dispatch_ui_event` → `vb.on("ui_event", handler)`
      (non-vetoable; spec gives no veto semantics for UI events).
      `kEngineProtocolVersion` bumped 6 → 7.
- [x] `src/client/main.cpp` wired end to end: `UiRuntime`/`UiRenderer`
      constructed unconditionally, drawn each frame between the 3D pass and
      `window.end_frame()`, mouse capture released while a UI is open.
      **No base pack (5.1) exists yet**, so nothing calls `ui.define` at
      real runtime today — same shipped-mechanism-ahead-of-content pattern
      as every prior 4.x phase.
- [x] Tests: `tests/unit/protocol_test.cpp` (`C2SUiEvent` round-trip),
      `tests/unit/ui_runtime_test.cpp` (layout evaluation, undefined-name
      no-op, `on_click`/`on_close` invocation), and a full end-to-end
      integration test in `tests/unit/pack_runtime_integration_test.cpp`:
      a server pack script's `block_break` handler calls `player:open_ui`
      (no earlier event hands a script a `PlayerHandle` for an online
      player), the client's `UiRuntime` opens it and a simulated button
      click round-trips to `vb.on("ui_event", ...)` on the server with the
      right `ui_name`/`widget_id`/`kind`/`value`.
- [ ] Base pack `ui/inventory.lua`, `ui/pause.lua` — waits on 5.1 (no
      content pack exists to put them in).
- [ ] Item grid widget — waits on 5.1 (needs a real item/inventory concept).

**Phase 4 exit — met (mechanism, not content):** all five Lua/asset-sync
subsystems (4.1–4.5) are implemented and tested end-to-end over
`LoopbackTransport`. What's still missing to literally satisfy the exit
line ("server runs the base Lua pack ... opens a Lua-defined inventory
screen") is content, not mechanism: there is no `content/base` pack yet
(Phase 5.1) for a real server to load, so nothing calls `vb.register_block`/
`ui.define`/etc. outside of tests today.

