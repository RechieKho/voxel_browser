# Voxel Browser — Lua Content API Reference

> **Status: stub.** Complete, example-driven reference for the server content
> API (`ARCHITECTURE_SPEC.md` §10.3) and the client UI API (§10.4). Filled in
> during Phase 4; `content/base` is the worked example.

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

## Server API (pack VM) — planned surface

- Registration (pack load only): `vb.register_block`, `vb.register_item`,
  `vb.register_entity`, `vb.register_biome`, `vb.register_craft`,
  `vb.worldgen.set_pipeline`. `register_entity`'s `visual = {atlas, frame_size,
  facings, clips}` table defines its billboard sprite presentation — see
  `ARCHITECTURE_SPEC.md` §11.3.
- Runtime world: `vb.world.get_block/set_block/raycast/spawn`.
- Entities / players: `entity:*`, `player:send_message/open_ui/give/get_name`.
- Events: `vb.on("player_join"|"player_leave"|"block_break"|"block_place"|
  "player_interact"|"chat"|"tick", handler)`, vetoable via `return false`.
- Scheduling: `vb.after`, `vb.every`. Storage: `vb.storage`.

## Client UI API (restricted VM) — planned surface

- `ui.define(name, layout_fn)`; widgets: panel, label, button, list, item grid,
  text input.
- Callbacks `on_click` / `on_change` / `on_close` → `C2S_UiEvent` RPC.

## Sandbox

Implemented in `Vm` construction (`strip_sandbox`): opens only `base`, `string`,
`table`, `math`, `coroutine`, `utf8`, then nils `os`, `io`, `dofile`, `loadfile`,
`load`, `loadstring`, `collectgarbage`, `require`, `package`, and every `debug.*`
except `traceback`. Instruction-count hook (`lua_sethook` / `LUA_MASKCOUNT`) and a
ceiling allocator are always on.

Still to do (Phase 4.4): reimplemented `require` over the synced virtual pack FS;
per-callback wall-clock budget enforced by the tick loop; the separate restricted
client UI VM. See §10.2.
