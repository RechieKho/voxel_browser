# Voxel Browser — End-to-End Client Automation (Design)

> Status: **Proposal**, nothing here is implemented yet. Covers a
> Playwright-style harness that drives real `voxel_browser` clients against a
> real `voxel_browser_server` in multiplayer, so gameplay can be tested by
> scripts instead of by hand. Complements spec §17 (Testing Strategy); the
> in-process `IntegratedGame`/`LoopbackNetwork` tests in `tests/unit/` stay
> as they are.

## 1. Problem

Tests today are in-process: `netcode_test.cpp`, `soak_test.cpp`, and
`blockedit_test.cpp` wire `ServerSession` and `ClientSession` together over
`LoopbackNetwork` and call `tick()` by hand. They cover the session and
protocol layer well. They do **not** cover:

- **The client app layer.** All of `src/client/main.cpp`'s windowed loop is
  untested: the `AppState` machine (menu → connecting → loading → playing),
  `sample_input_cmd`, mouse capture, chat box, hotbar keys, block targeting
  and break/place, `UiRuntime` screens and HUD, and the crack overlay.
  `--headless` goes through a separate, minimal `run_headless()` that skips
  all of it.
- **Real multi-process multiplayer.** That means two or more client
  processes talking to a dedicated server over GNS/UDP, with real asset sync
  into a cold cache, real timing, and real packet loss. Only the
  `*_smoke` CTest entries come close, and they only check that a join line
  gets printed.

Playwright solves the browser version of this problem in four parts:

1. A **driver channel** into the running app.
2. **Locators**, which find UI by meaning rather than by pixels.
3. **Auto-waiting assertions**, so tests don't need `sleep`.
4. A **test runner** with fixtures, parallel runs, and failure artifacts.

This design copies that shape for a native raylib game client.

## 2. Goals / non-goals

**Goals**

- Write tests like "client A breaks a block → client B sees it gone", "A
  says hi → B's chat shows it", or "open the inventory, click craft →
  planks appear" in a short, readable script.
- Drive the **same code path a player uses**. Synthetic input goes into the
  same `InputCmd` sampling, the same `UiRuntime::report_click`, and so on. It
  never bypasses them by sending protocol messages directly.
- Run headless in CI on Linux/macOS/Windows with no GPU. Screenshots are an
  optional extra (Xvfb leg).
- Run N clients against one server, with optional simulated lag/loss.
- Zero cost when unused: compiled out by default in release, with no network
  listener and no extra per-frame work unless `--automation` is passed.

**Non-goals (for now)**

- Clicking by pixel coordinates or doing image-diff golden tests. Both are
  brittle against a voxel scene; see §9 for a limited screenshot story.
- Bit-exact deterministic replays across processes. Real UDP is
  non-deterministic by nature. Use auto-waiting instead (§5.3). In-process
  deterministic tests stay where they are.
- Bots/AI players for load testing. The harness makes this easy later, but
  it's out of scope.

## 3. Architecture

```
            pytest  (tests/e2e/*.py)
              │  vbtest package: fixtures, Client/Server handles, expect()
              │
   ┌──────────┼───────────────────────────┬──────────────────────┐
   │ stdio    │ (JSON lines)              │ stdio                │ stdio
   ▼          ▼                           ▼                      ▼
voxel_browser_server             voxel_browser #A        voxel_browser #B
  --automation stdio               --automation stdio      --automation stdio
  ├ AutomationHost (server)        ├ AutomationHost (client)
  │   state / admin cmds           │   queries / actions / waits
  └ ServerSession  ◀── GNS/UDP ──▶ └ ClientApp ── InputSource = Synthetic
```

Four pieces, each independently useful:

| # | Piece | Where | Purpose |
|---|---|---|---|
| A | `InputSource` seam + `ClientApp` refactor | `src/client/` | Makes the real game loop drivable and headless-runnable |
| B | `AutomationHost` (client + server) | `src/automation/` (new module) | The driver channel: parse commands, run them, reply |
| C | Wire protocol | `docs/automation-protocol.md` | JSON-lines RPC, versioned |
| D | `vbtest` harness | `tests/e2e/` | Python/pytest: process orchestration, Playwright-like API |

### 3.1 Why stdio (pipe) transport

Playwright itself drives Chromium over `--remote-debugging-pipe`, and the
same reasons apply here:

- **No exposed port.** The server-side automation commands are effectively
  god-mode (`set_block`, `teleport`, `run_lua`). A pipe owned by the parent
  process can't be reached by anything else, so there's no auth problem to
  solve.
- **Cross-platform with no dependencies.** It's `std::cin`/`std::cout` on a
  reader thread. No sockets library is needed on the side that isn't
  `VB_WITH_NET`.
- **Lifetime is free.** When the harness dies, stdin hits EOF and the
  process exits. CI never leaves orphaned servers behind.

Game logs move to **stderr** when `--automation stdio` is active (stdout is
reserved for protocol frames). A TCP mode (`--automation tcp:127.0.0.1:0`)
can come later for attaching to a client a human is already running. It
would bind loopback only and need a token.

## 4. Piece A — make the client drivable

This is the main prerequisite, and also the largest change.

### 4.1 `InputSource`

Today, raylib input is read directly in about 17 places in
`src/client/main.cpp` (`IsKeyDown`, `IsKeyPressed`,
`IsMouseButtonDown/Pressed`, `GetMouseDelta`) and in the keybindings screen's
`GetKeyPressed()` in `src/render/main_menu.cpp`. Introduce:

```cpp
// inc/vb/client/input.hpp
struct InputFrame {
    std::bitset<512> keys_down, keys_pressed;   // raylib KeyboardKey values
    std::bitset<8>   mouse_down, mouse_pressed;
    Vector2          mouse_delta{}, mouse_pos{};
    std::u32string   chars;                     // GetCharPressed() drain
};

class InputSource {
public:
    virtual ~InputSource() = default;
    virtual InputFrame poll() = 0;              // called once per frame
};
class RaylibInput   final : public InputSource { ... };  // today's behavior
class SyntheticInput final : public InputSource {        // automation-fed
public: void hold(Key, int frames); void press(Key); void type(std::string);
        void look_delta(Vector2); ...
};
```

`sample_input_cmd(...)` and the chat, hotbar, and mouse-capture logic then
read from `const InputFrame&` instead of calling raylib. This change is a
pure refactor with no behavior change, and it also makes those functions
unit-testable.

**raygui is a special case.** Widgets read raylib's input state internally,
and raylib has no public injection API. So automation doesn't try to fake
pixel clicks on raygui. It goes one level up, to the place each widget's
result is consumed:

- Pack UI and HUD: call `UiRuntime::report_click/report_change/
  report_list_change(id)` (and the `_hud` variants), the same calls
  `main.cpp` makes when `UiRenderer::draw` reports a hit.
- Main menu: `MainMenu::draw_*` results become a value the automation layer
  can inject (`MenuResult{.connect=true}`), and the text fields
  (`player_name`, server address) get setters.
- Chat input box: `chat_buf` is filled from `InputFrame::chars`, and Enter
  submits it. Typing therefore goes through the real path.

### 4.2 `ClientApp`: one loop for windowed and headless

`run_headless()` is a separate, cut-down loop, so automation against it
would test something players never run. Pull the windowed loop's state and
logic out of `main()` into a `ClientApp` class:

```cpp
class ClientApp {
public:
    ClientApp(ClientConfig, AppOptions, InputSource&, bool render);
    void frame(double dt);          // one iteration of today's while-loop body
    AppState state() const;
    ClientSession* session();       // null until connecting
    UiRuntime& ui();
    const std::deque<std::string>& chat_log() const;
    std::optional<RaycastHit> target_block() const;
    ...
};
```

When `render == false`, all `window.begin_frame()`, draw, and GPU-upload
calls are skipped (mesh building can be skipped too, behind a flag).
Everything else runs the same way: the state machine, input sampling,
prediction, UI runtime, and chat. `run_headless()` then becomes "build
`ClientApp` with `render=false`, then call `frame()` N times". The
`*_smoke` tests keep their expected output.

### 4.3 Clock control

`--automation-clock real|manual`:

- `real` (default): the frame loop runs freely at fixed `dt = 1/60` when
  headless, or `GetFrameTime()` when windowed. Commands are applied at the
  start of the next frame.
- `manual`: the loop blocks until it receives `step {frames: n}`. This is
  for tests that need exact frame counts, such as "hold W for exactly 30
  frames, expect ≥ X metres moved". The **server** still runs on wall
  clock, so manual mode only removes client-side jitter.

## 5. Piece B/C — the automation protocol

JSON lines with request/response correlation and server-pushed events.
nlohmann/json is already a dependency (currently fetched only under
`VB_WITH_LUA`). Gate it on `VB_WITH_AUTOMATION` as well.

```jsonc
→ {"id":1,"cmd":"hello","proto":1}
← {"id":1,"ok":true,"result":{"role":"client","engine":"0.7.0","features":["ui","chat","screenshot"]}}
→ {"id":2,"cmd":"wait_for","args":{"pred":{"chat_contains":"hi"},"timeout_ms":5000}}
← {"id":2,"ok":false,"error":{"code":"timeout","last":{"chat":["<A> hello"]}}}
← {"event":"log","level":"warn","msg":"..."}           // unsolicited
```

All commands run on the **main thread** between frames. The stdin reader
thread only enqueues parsed requests into a mutex-guarded queue. This
keeps automation clear of the threading gotchas in `state/gotchas.md`
(mesh worker pool, worldgen pool).

### 5.1 Client commands

| Group | Command | Notes |
|---|---|---|
| meta | `hello`, `step{frames}`, `quit` | `step` is manual clock only |
| menu | `menu.set_name`, `menu.connect{host,port}`, `menu.singleplayer` | Injects `MenuResult` |
| query | `state` | Returns one snapshot: `app_state`, `handshake`, `net_id`, `feet`, `yaw/pitch`, `health`, `hotbar`, `selected_slot`, `chat[]`, `remote_entities[{net_id,name,kind,pos}]`, `ui{name,widgets[{id,type,text,items,list_index}]}`, `hud{widgets}`, `chunks_loaded`, `target_block` |
| query | `block_at{pos}` | From `ClientChunkStore` (the client's view) |
| input | `key.hold{key,frames}`, `key.press{key}`, `mouse.hold{button,frames}`, `look{yaw,pitch}` / `look_at{pos}`, `type{text}` | Feeds `SyntheticInput`; key names map to logical bindings (`forward`, `jump`, `primary`) **or** raw raylib keys |
| high-level | `walk_to{pos,tolerance}`, `break_block{pos}`, `place_block{pos,face,slot}`, `select_slot{n}`, `chat.send{text}` | Built *on top of* the input commands: look at the target, then hold primary until the block changes. No protocol shortcuts. |
| ui | `ui.click{id}`, `ui.fill{id,text}`, `ui.select{id,index}`, `hud.click{id}` | Locator = widget `id` (unique per screen). Fails fast if the screen or widget isn't present. |
| wait | `wait_for{pred,timeout_ms}` | Predicate is evaluated **every frame in-process** (no polling over the pipe) |
| artifacts | `screenshot{path}` | Windowed builds only; returns `unsupported` when headless |

### 5.2 Predicates (the auto-wait vocabulary)

These are declarative and composable (`all`/`any`/`not`), and evaluated
in C++:

`joined`, `app_state{is}`, `chat_contains{text|regex}`,
`entity_visible{name}`, `entity_near{name,pos,radius}`,
`block_is{pos,block}` (block by registry *name*, e.g. `"base:air"`),
`ui_open{name}`, `widget{id,text?}`, `pos_near{pos,radius}`,
`inventory_has{item,count}`, `health{op,value}`, `chunks_loaded{min}`.

On timeout, the reply includes the predicate's last observed values. That
makes failures say *why* (`chat was ["<A> hello"]`), the way
`expect(...).toHaveText` does.

### 5.3 Server commands

The server takes `--automation stdio` too. It's mainly used for **setup
and verification**, because the server is the source of truth:

`hello` (returns the **actually bound port**, so tests can pass `--port 0`
and avoid collisions), `state` (players, positions, tick, time of day),
`block_at`, `set_block`, `fill`, `teleport{player,pos}`,
`give{player,item,count}`, `set_time`, `set_health`, `kick`,
`run_lua{code}` (in the pack VM, for pack-specific setup), and
`wait_for{pred}`, which uses the same predicate engine with server-side
predicates (`player_count`, `block_is`, `player_near`).

Every server command sits behind the stdio channel. With no
`--automation` flag the code paths can't be reached, and with
`VB_WITH_AUTOMATION=OFF` they aren't compiled at all (the usual
`kDisabled` stub pattern from `CONTRIBUTING.md`).

### 5.4 Network conditions

Add `--net-sim lag_ms=120,jitter_ms=30,loss_pct=5` to both binaries. It
maps onto GNS's built-in `k_ESteamNetworkingConfig_FakePacketLag_*` and
`FakePacketLoss_*` settings in `GnsTransport`, and onto a simple
delay/drop queue in `LoopbackTransport` for singleplayer. This lets
prediction, reconciliation, and interpolation be tested under bad
conditions. Today that can only be checked by hand.

## 6. Piece D — the `vbtest` harness

**Python + pytest.** CI runners on all three OSes already ship Python 3,
and pytest supplies the Playwright Test features we want: fixtures,
parametrization, `-n` parallelism (pytest-xdist), retries, and JUnit XML.
The alternative was TypeScript with `@playwright/test` used only as a
runner. It's closer to Playwright, but it brings a Node toolchain into a
pure C++/CMake/Lua repo.

The API is async and mirrors Playwright's naming:

```python
# tests/e2e/test_multiplayer_basics.py
from vbtest import expect

async def test_block_break_replicates(server, clients):
    a, b = await clients(2)                       # spawn + join both
    await server.set_block((4, 70, 4), "base:stone")
    await server.teleport(a, (4, 71, 6))
    await expect(b).to_see_block((4, 70, 4), "base:stone")

    await a.break_block((4, 70, 4))               # look + hold primary
    await expect(b).to_see_block((4, 70, 4), "base:air", timeout=5)
    await expect(a).to_have_inventory("base:stone", 1)

async def test_chat_roundtrip(clients):
    a, b = await clients(2, names=["Alice", "Bob"])
    await a.chat("hello bob")
    await expect(b).to_have_chat("<Alice> hello bob")

async def test_craft_planks_via_inventory_ui(server, client):
    await server.give(client, "base:wood", 1)
    await client.key_press("inventory")           # logical keybind name
    await expect(client).to_have_ui_open("base:inventory")
    await client.ui("craft_planks").click()       # locator by widget id
    await expect(client).to_have_inventory("base:planks", 4)

@pytest.mark.net_sim(lag_ms=150, loss_pct=5)
async def test_remote_movement_is_seen_under_lag(clients):
    a, b = await clients(2)
    await a.walk_to((10, None, 0))
    await expect(b).to_see_entity("Player1", near=(10, None, 0), radius=1.5)
```

**Fixtures (`tests/e2e/conftest.py`):**

- `binaries`: locates `voxel_browser{,_server}` from `--vb-build-dir` or
  the `VB_BUILD_DIR` env var (set by CTest).
- `server`: one per test (function scope). Uses a temp dir copy of
  `server.toml.example`, a fixed seed, `--port 0`, and the base pack (or a
  `tests/e2e/packs/<name>` pack via `@pytest.mark.pack(...)`). It also gets
  a fresh region dir, since the world is regenerated per test and there's
  no persistence yet.
- `clients(n, names=…)`: a factory. Each client gets its **own temp asset
  cache dir** (so asset sync is exercised cold) and its own
  `client.toml`, spawns with
  `--headless --automation stdio --server 127.0.0.1 --port <bound>`, and
  awaits `wait_for joined`.
- Teardown: `quit` → wait → kill. On failure, attach each process's
  stderr log, the JSONL command trace, and the final `state` snapshot of
  every client to the report.

The `vbtest` package itself (`tests/e2e/vbtest/`) is about 500 lines:
an asyncio subprocess wrapper with request/response correlation,
`Client`/`Server` handles, `Locator`, and an `expect()` that just builds a
predicate and sends one `wait_for` (so retrying happens in-process, every
frame).

### 6.1 Tracing (Playwright trace-viewer analogue, later phase)

Every command and response gets a timestamp and is written to
`<test>/trace.jsonl` for each process. When the test fails, it also
includes the `state` snapshot taken just before each action. A small
static HTML viewer can lay out the clients' timelines side by side, which
is the main need when debugging "A did X, but B never saw it".

## 7. Build & CI integration

- New option `VB_WITH_AUTOMATION` (default `ON` when `VB_BUILD_TESTS=ON`,
  otherwise `OFF`). Release artifacts built by `build_*.yml` turn it off.
- `tests/CMakeLists.txt`: `add_test(NAME e2e COMMAND ${Python3_EXECUTABLE}
  -m pytest ${CMAKE_SOURCE_DIR}/tests/e2e --vb-build-dir ${CMAKE_BINARY_DIR}
  -q --junitxml=e2e.xml)` with label `e2e`, registered only when
  `VB_WITH_AUTOMATION AND VB_WITH_NET` and Python is found.
  `ctest -L e2e` / `ctest -LE e2e` select or skip it.
- `build_linux.yml`: run e2e in the `release` and `asan` legs (ASan
  catches teardown and leak bugs in real multi-process shutdown). Skip it
  under `tsan` at first, since GNS suppressions already make that leg
  noisy. Add a separate `xvfb-run` leg later for the windowed and
  screenshot tests.
- Windows and macOS: run after Linux is stable. macOS CI doesn't build
  `VB_WITH_NET` yet (README "Known gaps"), so it only gets the
  singleplayer variants (`clients(1, singleplayer=True)`).
- Upload `tests/e2e/**/artifacts/` on failure.

## 8. Phased plan

| Phase | Deliverable | Rough size | Verifiable by |
|---|---|---|---|
| **E0** | `InputSource` seam: `RaylibInput`, all direct raylib input calls in `src/client` + `main_menu` routed through `InputFrame`; unit tests for `sample_input_cmd` | S–M | Existing tests + manual play unchanged |
| **E1** | `ClientApp` extraction; `run_headless` re-based on it with `render=false` | M–L (highest risk: touches the 1.7k-line `main.cpp`; land behind no flag, purely structural) | `*_smoke` tests unchanged; windowed manual check |
| **E2** | `src/automation/`: JSON-lines host, stdin thread → main-thread queue, `hello/state/step/quit/wait_for` + predicate engine; `--automation stdio` on client + server; `--port 0` reporting | M | doctest unit tests for predicate engine + command parsing |
| **E3** | Action commands (input, high-level, ui, menu, chat) + server admin commands | M | |
| **E4** | `tests/e2e/vbtest` + fixtures + first 5 tests (join, chat, block break replicates, craft via UI, reconnect after kick); CTest `e2e` label; Linux CI legs | M | CI green |
| **E5** | `--net-sim`, trace JSONL + failure artifacts, screenshot under Xvfb | S–M | |
| **E6** (optional) | Recorder ("codegen"): `--automation-record out.py` logs a human session's *semantic* actions (connect, look, hold, ui.click id) as a vbtest script skeleton; TCP attach mode; HTML trace viewer | M | |

E0–E1 are worth doing on their own: they make the client loop testable
without any automation and remove the duplicated headless loop.

## 9. Risks & open questions

1. **`main.cpp` refactor risk (E1).** The windowed loop carries
   hard-won fixes (see `STATE.md`: the NVIDIA VAO/VBO churn bug, the inventory
   mouse-centering fix in a723d89). Do the extraction mechanically, as one
   commit that only moves code, and only then change behavior.
2. **Headless meshing.** Should `ClientApp(render=false)` still mesh chunks?
   Off by default is faster. Turn it on (`--mesh-headless`) for tests that
   want to catch mesher crashes from real replicated edits.
3. **Logical vs raw keys.** Tests should use logical names (`forward`,
   `inventory`) so they survive rebinding. Raw keys stay available for
   testing the keybindings screen itself.
4. **Flakiness under ASan/CI load.** Default `wait_for` timeouts scale with
   an env var (`VB_E2E_TIMEOUT_SCALE`, set to 3 on sanitizer legs). There
   are no fixed sleeps anywhere in `vbtest`, and that's enforced in review.
5. **Screenshots.** Raylib's `TakeScreenshot` works under Xvfb with Mesa
   llvmpipe. Use it only for debugging artifacts, never for pixel
   assertions (non-goal).
6. **Widget id uniqueness.** The locators depend on pack authors giving
   `ui.define` widgets stable, unique `id`s. `UiRuntime` should warn on
   duplicate ids within one frame. That's cheap, and useful even without
   automation.
