# Voxel Browser — Lua Content API Reference

> **Status: partial (Phase 4.2 landed the server API mechanism; no base pack
> content exists yet — waits on Phase 5.1).** Complete, example-driven
> reference for the server content API (`ARCHITECTURE_SPEC.md` §10.3) and the
> client UI API (§10.4) still lands with `content/base` as the worked example.

## Binding layer

`sol2` (v3.5.0), header-only, over PUC-Lua 5.4 — decision recorded in
`ARCHITECTURE_SPEC.md` §19 Q1. (v3.3.0's bundled optional does not compile under
Clang ≥ 18.)

## Runtime — `vb::script::Vm` (`inc/vb/script/vm.hpp`, implemented, `VB_WITH_LUA`)

pImpl wrapper around one `sol::state`; the rest of the engine never includes
sol2, and a build without `VB_WITH_LUA` links a stub whose every call returns
`core::ScriptError::kDisabled`.

- `Vm(VmLimits{ memory_bytes, instruction_budget })`
- `ScriptResult do_string(code, chunk_name)` — compiles **source only**
  (`sol::load_mode::text`; bytecode is rejected) and runs it under the per-call
  hook. `ScriptResult` carries `{ok, core::ScriptError, message}` where `message`
  is the Lua error text + `debug.traceback`.
- `begin_call_budget()` — re-arms the instruction counter; the tick loop calls it
  before each pack callback (Phase 4.2).
- `memory_used()` / `memory_limit()` / `sandbox_intact()`.

Error classes: `kSyntax`, `kRuntime`, `kBudgetExceeded` (hook fired),
`kOutOfMemory` (allocator ceiling — recoverable, the VM stays usable).

## Server API (pack VM) — `vb::script::PackRuntime` (`inc/vb/script/pack_runtime.hpp`, implemented, `VB_WITH_LUA`)

pImpl'd the same way as `Vm` (a disabled build links a no-op stub); owns one
`Vm`, the content registries, the event bus, and `vb.after`/`vb.every`
timers. Construction is split because a `player_join` veto must wrap
`HandshakeServerHost::authenticate` **before** the `ServerSession` that owns
it exists:

```cpp
PackRuntime rt(transport, registry, storage_path);
rt.load_pack_file(pack_source);
rt.freeze();
rt.install_join_veto(host);           // before constructing ServerSession
ServerSession session(transport, cfg, host);
// ... construct WorldReplicator ...
rt.attach_world(*session.world_replicator());
rt.attach_session(session);
// main loop, after session.tick(dt):
for (auto &j : session.take_joins())  rt.dispatch_player_join_completed(j);
for (auto &l : session.take_leaves()) rt.dispatch_player_leave(l);
rt.dispatch_tick(dt);
```

- Registration (pack load only, rejected once `freeze()` has run):
  `vb.register_block(def) -> BlockId` (idempotent by `name`; `texture`/`model`
  fields are accepted but not stored — no wire-visible model/texture fields
  exist on `BlockType` yet), `vb.register_item(def)`, `vb.register_entity(def)`
  (captures `on_spawn`/`on_tick`/`on_hit`/`on_death` but nothing dispatches
  them yet — no EnTT registry exists, Phase 3.1), `vb.register_biome(def)` /
  `vb.register_craft(def)` (captured, no consumer this phase).
  `vb.worldgen.set_pipeline` is **not implemented** — the worldgen pipeline
  swap is out of this phase's scope; calling it errors as a nil call.
  A pack registering blocks beyond the Phase 2 `base()` set logs an info
  line; those ids reach the client if (and only if) the host wires
  `HandshakeServerHost::block_registry` from this same registry
  (§4.3/`S2C_BlockRegistry` — `src/server/main.cpp` does). The server-side
  registry starts from `base()` so the well-known 8 ids still match the
  client's fallback `base()` call when no registry frame is sent at all.
- Runtime world (needs `attach_world()`): `vb.world.get_block(x,y,z)`,
  `vb.world.set_block(x,y,z,id)` (**known gap:** doesn't run the relight
  cascade `C2S_BlockEdit` does — can desync lighting until something else
  touches the chunk), `vb.world.raycast(origin, dir, max) -> {hit,x,y,z,nx,ny,nz}|nil`,
  `vb.world.spawn(kind, pos)` (logs and returns `nil` — no entity system to
  spawn into yet, Phase 3.1).
- Entity / player Lua object (needs `attach_session()`; one merged usertype
  today since no non-player entity exists): `:get_pos() -> {x,y,z}`,
  `:set_velocity(x,y,z)`, `:remove()` (no-op, logged — nothing to remove
  from), `:get_inventory() -> {{item,count}, ...}`, `:give({item,count})`,
  `:send_message(text)`, `:open_ui(name, ctx?)`, `:get_name()`.
  `send_message`/`open_ui` are real, framed messages (`S2C_Chat`/`S2C_OpenUi`,
  see `docs/protocol.md`) — no client handles them yet (Phase 4.5), so they're
  inert but correct on the wire.
- Events: `vb.on("player_join"|"player_leave"|"block_break"|"block_place"|
  "player_interact"|"chat"|"tick", handler)`, vetoable via `return false`.
  `player_join` fires from `install_join_veto`'s `authenticate` wrapper (a
  real pre-join veto); `block_break`/`block_place` fire from
  `WorldReplicator::apply_block_edit`'s new `BlockEditHooks` seam, and a
  block's own `on_break`/`on_place` callback (from `register_block`) fires
  separately, after the edit is applied — its return value (a would-be drop)
  is logged only, not materialized (waits on 5.1 items).  `chat` /
  `player_interact` are wired generically (`PackRuntime::dispatch_chat` /
  `dispatch_player_interact`) but nothing calls them yet — no `C2S_Chat` /
  interact message exists.
- Scheduling: `vb.after(seconds, fn)` (one-shot), `vb.every(seconds, fn)`
  (repeating; catches up on a stalled tick, capped at 8 fires/dispatch).
  Storage: `vb.storage.key = value` — a metatable-backed proxy over a
  `nlohmann::json` object, persisted to `<content_pack>/storage.json`
  (flushed once per tick when dirty, and in `flush_storage()`).

## Client UI API — `vb::script::UiRuntime` (`inc/vb/script/ui_runtime.hpp`, implemented, `VB_WITH_LUA`)

A second, separate `Vm` from the server's `PackRuntime` — pImpl'd the same
way, disabled-stub when `VB_WITH_LUA` is off. Drawing is split across the
core/render boundary: `UiRuntime` (`vb_core/script`) evaluates layout into a
plain `Widget` list (no raygui), and `vb::render::UiRenderer`
(`inc/vb/render/ui_renderer.hpp`) draws that list with raygui and reports
back which widgets fired an interaction — no sol2 in the render half.

- `ui.define(name, layout_fn)` — `layout_fn(ctx)` returns
  `{ widgets = { ... }, on_close = fn? }`. Widgets are computed once at
  `open()` time and don't re-layout afterward (known limitation — a callback
  that wants a different screen should `ui.close()` + have the server
  `open_ui()` again).
- Widget types implemented: `label`, `panel`, `button`, `textbox`, `list`
  (spec's named set minus **item grid**, deferred to 5.1 — needs real items).
  Each widget table: `id`, `type`, `x`/`y`/`w`/`h`, `text` (label/panel/
  button/textbox), `items`/`list_index` (list only), and optional
  `on_click`/`on_change` callback fields.
- `ui.send_event(kind, value)` — sends a `C2S_UiEvent` to the server
  (`current_name`/the widget whose callback is currently running are filled
  in automatically). `ui.close()` — always sends one `"close"` event, then
  runs the layout's own `on_close` (if any) for local cosmetic cleanup, then
  clears state.
- Client wiring (`src/client/main.cpp`): `ClientSession::take_open_ui()`
  drains a pending `S2C_OpenUi`; `UiRuntime::open()` evaluates it;
  `UiRenderer::draw()` runs once per frame while open, between the 3D pass
  and `window.end_frame()`; interactions route back through
  `report_click`/`report_change`/`report_list_change`. No base pack (5.1)
  exists yet, so nothing calls `ui.define` at real runtime today — the
  mechanism is exercised by `tests/unit/ui_runtime_test.cpp` and an
  end-to-end `player:open_ui` → click → `vb.on("ui_event", ...)` round trip
  in `tests/unit/pack_runtime_integration_test.cpp`.

## Sandbox

Implemented in `Vm` construction (`strip_sandbox`): opens only `base`, `string`,
`table`, `math`, `coroutine`, `utf8`, then nils `os`, `io`, `dofile`, `loadfile`,
`load`, `loadstring`, `collectgarbage`, `require`, `package`, and every `debug.*`
except `traceback`. Instruction-count hook (`lua_sethook` / `LUA_MASKCOUNT`) and a
ceiling allocator are always on.

Still to do (Phase 4.4): reimplemented `require` over the synced virtual pack FS;
per-callback wall-clock budget enforced by the tick loop; the separate restricted
client UI VM. See §10.2.
