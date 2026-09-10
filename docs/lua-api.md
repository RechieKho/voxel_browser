# Voxel Browser — Lua Content API Reference

> **Status: stub.** Complete, example-driven reference for the server content
> API (`ARCHITECTURE_SPEC.md` §10.3) and the client UI API (§10.4). Filled in
> during Phase 4; `content/base` is the worked example.

## Binding layer

`sol2` (v3.3.0), header-only, over PUC-Lua 5.4 — decision recorded in
`ARCHITECTURE_SPEC.md` §19 Q1.

## Server API (pack VM) — planned surface

- Registration (pack load only): `vb.register_block`, `vb.register_item`,
  `vb.register_entity`, `vb.register_biome`, `vb.register_craft`,
  `vb.worldgen.set_pipeline`.
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

Both VMs: no `os`/`io`/`debug` (except `traceback`), no bytecode `load`,
reimplemented `require` over the synced virtual pack FS, instruction-count hook,
memory ceiling, per-callback wall-clock budget. See §10.2.
