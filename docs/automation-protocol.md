# Automation protocol (development builds only)

JSON-lines RPC between a test harness and a `voxel_browser` /
`voxel_browser_server` built with `-DVB_WITH_AUTOMATION=ON`. Design and safety
rules: `docs/e2e-automation.md` (§5 protocol, §7 keeping it out of production).
Production binaries contain none of this and exit non-zero on `--automation`.

**Protocol version: 1** (`kAutomationProtocolVersion`, `inc/vb/automation/protocol.hpp`).
Implemented so far: phase E2 (host, `hello/state/step/quit/wait_for`, predicate
engine, read-only state) and phase E3 (client input / UI / chat / multi-frame
actions, server admin commands, deferred replies). Windowed-only commands
(`menu.*`, `screenshot`, typing into the raygui chat box) are not implemented;
see "Not implemented" below.

## Transport

`--automation stdio` on either binary. One JSON object per line.

- **stdin** → requests. **stdout** → responses and events. Closing stdin makes
  the process exit (no orphaned servers).
- At startup fd 1 is duplicated for protocol frames and then redirected to
  **stderr**, so `std::cout`, `printf` and Lua's `print` can never corrupt a
  frame. Read logs from stderr.
- The client currently requires `--headless`. `--automation-clock real|manual`
  (client): `real` (default) runs frames at 60 Hz wall clock; `manual` only
  advances on `step`.
- Any `--automation` value other than `stdio` is an error; a build without the
  flag prints `built without VB_WITH_AUTOMATION` and exits 1.

All commands run on the main thread between frames/ticks; a reader thread only
enqueues parsed requests.

**Deferred replies (E3).** A command that takes frames to finish (`key.hold`,
`walk_to`, `break_block`, ...) is answered when it completes, not when it is
accepted, like `wait_for`/`step`. Several can be in flight at once; match
replies by `id`. Timeouts for client actions are counted in frames (60 per
second), so they behave the same under `--automation-clock manual`.

## Frames

```jsonc
→ {"id":1,"cmd":"hello","proto":1}                        // `proto` may also be in args
→ {"id":2,"cmd":"wait_for","args":{"pred":{"joined":true},"timeout_ms":5000}}
← {"id":1,"ok":true,"result":{...}}
← {"id":2,"ok":false,"error":{"code":"timeout","message":"...","last":{...}}}
← {"event":"log", ...}                                    // unsolicited, no id
```

`id` is a number or string, echoed verbatim. Responses can arrive out of order
(`wait_for`, `step` answer later).

### Error codes

| code | meaning |
|---|---|
| `bad_request` | unparseable line, missing/invalid fields, malformed predicate (extra `message`) |
| `unknown_command` | `cmd` not implemented by this role |
| `proto_mismatch` | `hello.proto` ≠ host's protocol version |
| `unsupported` | e.g. `step` without `--automation-clock manual` |
| `timeout` | `wait_for` or a client action ran out of time; `error.last` holds what was observed |
| `not_playing` | client action needs `app_state` = `playing` (and a joined session) |
| `out_of_reach` | `break_block`/`place_block` target is farther than the reach (5.0); extra `distance`, `reach` |
| `not_loaded` | the target chunk isn't streamed in (client) / loaded (server); server edits never create chunks |
| `no_ui` / `no_widget` / `bad_widget` | `ui.*`: no screen open / no such widget id (extra `widgets` lists the ids) / widget is the wrong type |
| `failed` | action ran but didn't take effect (e.g. `select_slot` while a UI or the chat box is open) |
| `no_player` / `unknown_item` / `lua_error` | server admin: unknown player (extra `players`), unknown block name, `run_lua` failed (`message` is Lua's) |

## Commands (E2)

| Command | Roles | Args → result |
|---|---|---|
| `hello` | both | `{proto}` → `{role, proto, engine, build, clock}`; server adds `port` (the port it was told to listen on) |
| `state` | both | → snapshot (below) |
| `step` | client, manual clock | `{frames: 1..1000000}` → `{frames}` after that many frames ran |
| `wait_for` | both | `{pred, timeout_ms=5000}` → `{observed}` once true (evaluated every frame/tick in-process) |
| `quit` | both | → `{}`, then the process shuts down normally |

## Client commands (E3)

All take effect through the real input path: they feed a `SyntheticInput` that
`ClientApp::frame()` consumes, or call the same `UiRuntime::report_*` functions
the renderer calls. Positions are world coordinates; `pos` for blocks is floored.
Yaw/pitch use the camera's convention (degrees; **positive pitch looks up**,
yaw 0 faces -Z).

| Command | Args → result |
|---|---|
| `key.press` / `key.hold` | `{key}` / `{key, frames}` → `{frames}` once the key has been down that many frames. `key` is a logical name (`forward back left right jump sprint` follow the player's bindings; `inventory` `pause` `chat`), a letter/digit, `slot1..9`, `space enter escape tab shift ctrl` |
| `mouse.press` / `mouse.hold` | `{button=left\|right\|middle}` / `+ {frames}` → `{frames}` |
| `mouse.capture` | `{on=true}` → `{mouse_captured}`. Movement/look/break/place input is only read while the mouse is captured (as for a player); actions below capture it automatically |
| `look` | `{yaw, pitch}` → `{yaw, pitch}` (absolute) |
| `look_at` | `{pos}` → `{yaw, pitch}` (aim the eye at a point) |
| `select_slot` | `{n: 1..9}` → `{slot}` (presses the number key) |
| `walk_to` | `{pos: [x, y\|null, z], tolerance=0.5, timeout_ms=15000}` → `{feet}`; holds forward facing the target, hops when stuck. `timeout`'s `last.feet` shows where it stopped |
| `break_block` | `{pos, timeout_ms=5000}` → `{block_before, block_after}`; aims at the block centre and punches (one click per rising edge, as `content/base/mechanics.lua` expects) until it changes |
| `place_block` | `{pos, face: [nx,ny,nz], timeout_ms=5000}` → `{placed, at}`; right-clicks face `face` of the existing block `pos`, placing at `pos+face` with the selected slot's item. `timeout`'s `last` has `looking_at`/`face` |
| `chat.send` | `{text}` → `{}`; sends over `ClientSession::send_chat`, what Enter in the chat box does |
| `ui.click` / `ui.fill` / `ui.select` | `{id}` / `{id, text}` / `{id, index}` → `{}` on the open modal screen (button / textbox / list) |
| `hud.click` / `hud.fill` / `hud.select` | same, on the always-on HUD widgets |

## Server commands (E3)

Setup and verification. The server is authoritative, so edits go to the real
world and replicate to clients like any other change.

| Command | Args → result |
|---|---|
| `block_at` | `{pos}` → `{block: name\|null}` |
| `set_block` | `{pos, block}` (registry name, e.g. `base:stone`) → `{changed}`; `not_loaded` if no chunk is loaded there |
| `fill` | `{from, to, block}` (inclusive box, ≤ 100000 voxels) → `{changed, unchanged, not_loaded}`; error `not_loaded` if the whole box is outside loaded chunks |
| `teleport` | `{player, pos}` (`player` = name or net id) |
| `give` | `{player, item, count=1}` → inventory is synced to the player like `player:give{}` |
| `set_time` | `{ticks: 0..23999}` → all clients are told immediately |
| `set_health` | `{player, value}`; lowering goes through the damage path (0 kills + respawns, cause `automation`) |
| `kick` | `{player, reason?}` |
| `run_lua` | `{code}` → `{}` or `lua_error`; runs in the pack VM after load: `vb.register_*` fails with `registry already frozen`; other APIs (e.g. `vb.world.*`) are callable, but world edits only stick in chunks that stay loaded (a player nearby) |

Build the scene in loaded chunks (e.g. near spawn, or after teleporting a
player and waiting for `chunks_loaded`): an edit outside them is refused rather
than creating an empty chunk.

## Not implemented
 
- `menu.set_name` / `menu.connect` / `menu.singleplayer` and `screenshot`:
  windowed-only (the menu is drawn by raygui; headless clients skip it and join
  from the command line). Deferred with the Xvfb leg (E5).
- `type{text}` and typing into the chat box: the box's text comes from raygui,
  so it only exists in a windowed client. Use `chat.send`.
- The `health` predicate on a client: health isn't replicated to clients. Read
  `players[].health` from the server's `state` instead.

### State snapshots

Client: `app_state` (`menu|settings|keybindings|connecting|loading|playing|error`),
`joined`, `net_id`, `feet [x,y,z]`, `yaw`, `pitch`, `chat [str]` (the HUD's last 8
lines), `chat_open`, `mouse_captured`, `selected_slot` (1-based),
`inventory [{item,count}]` (item = block name), `entities [{net_id,name,pos}]`,
`chunks_loaded`, `target_block {pos,normal,block}` (what the crosshair is on,
within reach; absent if nothing), `ui {name, widgets}` (only while a modal screen
is open), `hud {widgets}`, `busy_actions` (in-flight multi-frame commands).
Widgets are `{id, type, text}` (+ `items`, `list_index` for lists); `type` is
`label|panel|button|textbox|list|rect|text|icon`.

Server: `tick`, `time_of_day`, `player_count`,
`players [{name,net_id,pos,health,max_health}]`.

## Predicates (`wait_for.pred`)

An object with exactly one key. Combinators: `all:[..]`, `any:[..]`, `not:{..}`
(nesting ≤ 16). A scalar argument is shorthand for the primary field.
Missing state reads as "not true", never as an error; an unknown predicate or
bad arguments is `bad_request`.

| Predicate | Args | Reads |
|---|---|---|
| `joined` | – | `joined` |
| `app_state` | `{is}` or `"playing"` | `app_state` |
| `chat_contains` | `{text}` / `{regex}` or `"text"` | `chat` |
| `entity_visible` | `{name}` | `entities` |
| `entity_near` / `player_near` | `{name,pos,radius}` | `entities` / `players` |
| `block_is` | `{pos,block}` (registry name, e.g. `"base:air"`) | the role's own block view (client: chunk store, `false` if the chunk isn't loaded) |
| `pos_near` | `{pos,radius}` | `feet` |
| `health` | `{op,value}` (`< <= > >= == !=`) or a number | top-level `health` *(neither snapshot has one: clients don't receive health; use the server's `players[].health`)* |
| `chunks_loaded` | `{min}` or a number | `chunks_loaded` |
| `player_count` | `{op,value}` or a number | `player_count` (server) |
| `ui_open` | `{name}` or `"name"` | `ui.name` |
| `widget` | `{id, text?}` | `ui.widgets` (modal screen only) |
| `inventory_has` | `{item, count=1}` | `inventory` (client) |

## Known limitation: no `--port 0`

The design called for `--port 0` with the bound port reported by `hello`.
GameNetworkingSockets' direct-UDP listen has no ephemeral-port allocation
(`GnsTransport::listen(0)` fails), so the harness (E4) must pick a free UDP port
itself. `hello.port` currently echoes the configured port.
