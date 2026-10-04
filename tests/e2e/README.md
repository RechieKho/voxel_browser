# End-to-end multiplayer tests

Real `voxel_browser_server` and `voxel_browser` processes, driven from Python over the
development-only automation channel (`docs/automation-protocol.md`). Design and the rules
that keep all of this out of production binaries: `docs/e2e-automation.md`.

```python
from vbtest import expect

def test_block_break_replicates(server, clients):
    alice, bob = clients(2, names=["Alice", "Bob"])   # real processes, joined, cold asset caches
    expect(alice).to_be_on_ground()
    server.set_block((4, 70, 4), "base:stone")        # server-side setup
    expect(bob).to_see_block((4, 70, 4), "base:stone")

    alice.break_block((4, 70, 4))                     # aims and clicks like a player
    expect(bob).to_see_block((4, 70, 4), "base:air")  # auto-waits, no sleeps
```

## Running

You need a build with `-DVB_WITH_AUTOMATION=ON -DVB_WITH_NET=ON -DVB_WITH_LUA=ON
-DVB_WITH_COMPRESSION=ON` (and protobuf for networking), and `pip install -r requirements.txt`.
**Use a separate build directory and never ship it**: it contains admin commands
(`set_block`, `teleport`, `run_lua`, ...). Release builds set `VB_DISTRIBUTION=ON`, which
makes that combination a configure error.

```bash
cmake -S . -B build-e2e -DVB_WITH_AUTOMATION=ON -DVB_WITH_NET=ON -DVB_WITH_LUA=ON \
      -DVB_WITH_COMPRESSION=ON -DVB_WITH_REPLICATION=ON
cmake --build build-e2e
ctest --test-dir build-e2e -L e2e --output-on-failure      # or:
python3 -m pytest tests/e2e --vb-build-dir build-e2e -k break
```

| Option / env | Meaning |
|---|---|
| `--vb-build-dir` / `VB_BUILD_DIR` | where `voxel_browser{,_server}` are (default `build`) |
| `--vb-artifacts` / `VB_E2E_ARTIFACTS` | where failed tests' logs go (default `tests/e2e/artifacts`) |
| `--vb-keep-artifacts` | keep artifacts of passing tests too |
| `--vb-allow-remote-host` | let clients connect to a non-loopback host (a dedicated **dev** server only) |
| `VB_E2E_TIMEOUT_SCALE` | multiply every wait (CTest sets 3 for sanitizer builds) |

A failed test leaves, per process, `<name>.stderr.log`, `<name>.trace.jsonl` (every request
and reply with timestamps) and `<name>.final_state.json` under the artifacts directory; the
path is printed with the failure.

## Writing tests

- **Fixtures**: `server` (a fresh dedicated server per test, free loopback port, private copy
  of `content/base`, no persistence) and `clients(n, names=None)` / `client`. Mark a test
  `@pytest.mark.vb_server(view_distance=2)` to add `server.toml` keys.
- **Handles**: `Server` (`set_block`, `fill`, `teleport`, `give`, `set_time`, `set_health`,
  `kick`, `run_lua`, `block_at`, `state`) and `Client` (`key_press`, `walk_to`, `break_block`,
  `place_block`, `select_slot`, `chat`, `ui("id").click()`, `look_at`, `state`, ...). Every call
  is a real command from `docs/automation-protocol.md`; errors raise `AutomationError` with the
  process's error code.
- **Assertions**: `expect(handle).to_...(...)`; `.not_` negates. Each is *one* `wait_for` that the
  process re-checks every frame, so write no `sleep`s and no polling loops. On timeout the
  message says what the process last saw.

### Things that will bite you (each one did)

- **Wait for landing.** Players spawn above the ground and fall. Use
  `expect(c).to_be_on_ground()` before reading `feet`; a position sampled mid-fall is a
  block off. `feet.y` of a landed player is `62.9999`-ish: round with a small epsilon.
- **Build only in loaded chunks.** The server refuses edits outside them (it would otherwise
  create empty chunks), and its world can lag a moment behind the client's join. Wait with
  `expect(server).to_have_chunk_loaded(pos)` for *every* chunk your scene touches, and check
  `fill`'s `not_loaded == 0`.
- **Players spawn on the same spot.** A punch hits the nearest *player* before any block, so
  teleport the other player away before `break_block`.
- **Pack UI needs a dedicated server.** `--singleplayer` never installs the pack's keybinds
  (`docs/e2e-automation.md` §10), so `key_press("inventory")` only works against `server`.
- Click actions hold their aim steady for a few frames first (the server applies a command's
  look direction *after* running the pack's input hook), so `break_block`/`place_block` take
  about a tenth of a second longer than the raw round trip.
- Windowed-only features (menu, screenshots, typing into the chat box) are not available.

## Layout

```
conftest.py       fixtures: binaries check, artifacts, server, clients, loopback guard
vbtest/process.py JSON-lines client for one process (+ trace/stderr logs)
vbtest/handles.py Server / Client / Locator
vbtest/expect.py  expect(...).to_see_block(...) etc.
vbtest/net.py     free UDP port, "never aim bots at a real server" guard
test_*.py         the tests
```
