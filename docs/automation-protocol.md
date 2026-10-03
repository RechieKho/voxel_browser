# Automation protocol (development builds only)

JSON-lines RPC between a test harness and a `voxel_browser` /
`voxel_browser_server` built with `-DVB_WITH_AUTOMATION=ON`. Design and safety
rules: `docs/e2e-automation.md` (§5 protocol, §7 keeping it out of production).
Production binaries contain none of this and exit non-zero on `--automation`.

**Protocol version: 1** (`kAutomationProtocolVersion`, `inc/vb/automation/protocol.hpp`).
Implemented so far: phase E2 (host, `hello/state/step/quit/wait_for`, predicate
engine, read-only state). Action/input/UI commands and server admin commands
arrive in E3 and will be added to the tables below.

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
| `timeout` | `wait_for` deadline passed; `error.last` holds the predicate's observed values |

## Commands (E2)

| Command | Roles | Args → result |
|---|---|---|
| `hello` | both | `{proto}` → `{role, proto, engine, build, clock}`; server adds `port` (the port it was told to listen on) |
| `state` | both | → snapshot (below) |
| `step` | client, manual clock | `{frames: 1..1000000}` → `{frames}` after that many frames ran |
| `wait_for` | both | `{pred, timeout_ms=5000}` → `{observed}` once true (evaluated every frame/tick in-process) |
| `quit` | both | → `{}`, then the process shuts down normally |

### State snapshots

Client: `app_state` (`menu|settings|keybindings|connecting|loading|playing|error`),
`joined`, `net_id`, `feet [x,y,z]`, `chat [str]` (the HUD's last 8 lines),
`entities [{net_id,name,pos}]`, `chunks_loaded`. Not yet exposed: yaw/pitch,
health, hotbar, UI widgets, inventory (E3).

Server: `tick`, `player_count`, `players [{name,net_id,pos}]`.

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
| `health` | `{op,value}` (`< <= > >= == !=`) or a number | `health` *(not yet in the client snapshot)* |
| `chunks_loaded` | `{min}` or a number | `chunks_loaded` |
| `player_count` | `{op,value}` or a number | `player_count` (server) |
| `ui_open`, `widget`, `inventory_has` | see design §5.2 | `ui`, `inventory` *(E3: client snapshot doesn't carry these yet)* |

## Known limitation: no `--port 0`

The design called for `--port 0` with the bound port reported by `hello`.
GameNetworkingSockets' direct-UDP listen has no ephemeral-port allocation
(`GnsTransport::listen(0)` fails), so the harness (E4) must pick a free UDP port
itself. `hello.port` currently echoes the configured port.
