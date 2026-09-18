# Changelog (detail) — Part 2: Phase 5 content/UI features, Phase 6 design passes, librg

> Full landed-feature/bugfix writeups. See `STATE.md` for current status and pointers.
> Chronological, newest first (as originally written). Covers: Phase 5.1 content/base pack,
> Phase 5.2-5.5 (main menu, chat, player list, day/night, death/respawn, sfx docs, inventory,
> hold-to-break, dropped items, crafting, singleplayer content-pack wiring, docs pass), the
> Cellulose meshing spike+revert, librg replication wiring, and Phase 6 design-only passes.
> Preceded by `state/changelog-part1.md` (Phase 6.1-6.16 features + investigations + bootstrap).

---

### Gotchas learned this pass
- Heavy deps (GNS, librg, Lua/sol2, FastNoise2, LZ4/xxHash, Cellulose) are
  declared in `Dependencies.cmake` but **gated behind `VB_WITH_*` (default OFF)**.
  Phase 1 flips `VB_WITH_NET` / `VB_WITH_REPLICATION` on — expect the GNS build
  (protobuf + OpenSSL) to be the real work there, and the CI `apt`/`brew`/`choco`
  steps to need new packages then.
- `raylib` + `raygui` are only fetched when `VB_BUILD_CLIENT=ON`.
- raygui's implementation must be its own target (`vb_raygui_impl`) so project
  `-Werror` never touches it.
- `src/**` in the old lint job didn't recurse; lint now uses `find`. Every file
  under `src/` (incl. `.c`) must be clang-format-clean or CI fails.
- doctest + MSVC STL: comparing/streaming a `std::string_view` in a `CHECK`
  instantiates `toString<string_view>` which needs `<ostream>` complete —
  add `#include <ostream>` to any test TU that does this (see `core_test.cpp`).
- `Result<T,E>`: never call `.error()` when it holds a value (union UB). In
  tests use `REQUIRE(result)` then deref, not `REQUIRE_MESSAGE(result, msg(err))`.
- Sub-modules attach sources to `vb_core` via `target_sources()` from their own
  `src/<mod>/CMakeLists.txt`, added after `core` in `src/CMakeLists.txt`.
- Local dev on this Windows box: `clang`/`clang++` (LLVM 21) work; no `gcc`/`cl`
  on PATH in git-bash. `CC=clang CXX=clang++ cmake -G Ninja` configures fine.
  First configure is slow (~130s) — raylib + deep git clones.

- **2026-09-16 — Phase 5.1: `content/base` pack written, and a real gap
  found & fixed along the way: neither binary ever loaded a content pack at
  all (uncommitted at time of writing).**
  Before this: `src/server/main.cpp` constructed a `PackRuntime` and called
  `freeze()` immediately with nothing registered — `content_pack` in
  `server.toml` was read (for the asset manifest + storage path) but never
  actually pointed at anything Lua. Every Phase 4.2 test exercised
  `PackRuntime` directly (`load_pack_file` with inline source strings), so
  this never showed up as a test failure — it's the same category of gap as
  4.5's "nothing calls `ui.define` at real runtime today", just never
  written down explicitly for the server side.
  **Why a loader was needed instead of just `pack_runtime.load_pack_file(
  read_whole_file("init.lua"))`:** the spec's pack layout (§16) has
  `init.lua` `require` in `blocks/*.lua`/`entities/*.lua`, but `require` is
  one of the globals nil'd out by the sandbox (`vm.cpp`'s `strip_sandbox`)
  and was never reimplemented over the virtual pack FS (tracked as a
  REMAINING_TASKS.md 4.1 follow-up, still open). New
  `vb::script::load_content_pack` (`inc/vb/script/pack_loader.hpp` +
  `src/script/pack_loader.cpp`) works around this at the *host* level
  instead: walks `blocks/*.lua` → `entities/*.lua` → `biomes/*.lua` (each
  sorted for determinism) → `init.lua`, calling `load_pack_file` once per
  file. This is behaviourally identical to one concatenated script because
  every file shares the same Lua globals (`vb.register_block` etc. all live
  on one `PackRuntime`'s `sol::state`) — a later file (e.g.
  `blocks/grass.lua`) can read a plain global a sorted-earlier file set
  (`blocks/dirt.lua` sets `base_dirt_id`), which is how `content/base`
  itself demonstrates cross-file sharing without `require`.
  `src/server/main.cpp` now calls this before `freeze()`, and treats a real
  syntax/runtime error in any pack file as fatal server startup (same "a
  broken pack is a broken deployment" posture as a bad asset manifest);
  `core::ScriptError::kDisabled` (a `VB_WITH_LUA`-off build) is treated as
  non-fatal and logged once, matching every other `VB_WITH_*`-off
  graceful-degrade in this codebase.
  **The client had the identical gap for UI screens**, also fixed:
  `src/client/main.cpp` never called `UiRuntime::load_pack_file` for
  anything, so `ui.define` was never invoked outside tests either (4.5's
  exact words: "nothing calls `ui.define` at real runtime today"). Now,
  right after a real multiplayer join (`--server`, not `--singleplayer` —
  see below), the client iterates `ClientSession::virtual_pack_fs()`
  (Asset Sync's in-memory synced-files map, 4.4 — populated by the time
  join completes, since asset transfer sits between Auth and Ready in the
  handshake, §8.3) and loads every `ui/*.lua` entry into `UiRuntime`.
  **Known gap this does NOT close, worth knowing before assuming UI
  screens are reachable:** `player:open_ui(name, ctx)` is still the *only*
  way any screen opens (server-push only), and nothing calls it in real
  gameplay — there's no client keybind or C2S message requesting "open my
  inventory" or "pause". `content/base/ui/{pause,inventory}.lua` load and
  register cleanly (verified — see below) but currently can't be triggered
  by a player. That's a `REMAINING_TASKS.md` 5.1/5.4-shaped follow-up, not
  attempted here on purpose (adding an ad-hoc trigger, e.g. auto-opening
  the inventory on every block break, would be surprising unrequested
  gameplay behaviour, not a real fix).
  **`--singleplayer` still doesn't wire any of this** — it has no
  `PackRuntime`/asset manifest on its in-process `IntegratedGame` path at
  all (pre-existing gap, REMAINING_TASKS.md 4.3 already noted this for the
  block registry specifically; it applies equally to the pack loader and UI
  loading added here). Not attempted — wiring a `PackRuntime` into
  `Singleplayer`'s constructor (`src/client/main.cpp`) is a real, separate
  chunk of work (needs its own manifest/storage path, and the *client*
  reading Lua files straight off disk instead of through asset sync since
  there's no separate server process to sync from), closer in spirit to
  5.3's "integrated-server path for singleplayer" than to authoring pack
  content.
  **Content itself** (`content/base/{pack.toml,init.lua,blocks,entities,
  biomes,ui}/*.lua`): registers the Phase 2 base block set by name (ids
  unchanged, `add_or_get` is idempotent), wires `on_break` to actually give
  the broken block back via `player:give(...)` (a real drop — distinct
  from `on_break`'s *return value*, which `pack_runtime.cpp`'s
  `on_block_edit_after` still only logs, not materializes), and declares
  two biomes + one entity kind purely for the pack-format shape (neither
  has a consumer yet — worldgen is still the hardcoded Phase 2 pipeline,
  and generic entities still wait on 3.1's EnTT registry). `vb.storage` is
  used for real (a boot counter), not just declared, to prove the
  JSON-round-trip path actually persists across restarts (verified live,
  see below). Full accounting of what's real vs. still-declarative lives in
  `REMAINING_TASKS.md`'s Phase 5.1 section now — this entry is the *why*,
  that one's the checklist.
  **New test:** `tests/unit/content_pack_test.cpp` loads the real
  `content/base` files (not inline strings, unlike every existing
  `pack_runtime_test.cpp` case) via `load_content_pack`, asserting a clean
  load + unchanged block ids, plus a separate broken-pack-directory-is-
  fatal case. Needed a new `VB_PROJECT_SOURCE_DIR` compile definition on
  `vb_tests` (`tests/CMakeLists.txt`) since ctest's working directory is
  the build dir, not the source tree, and this is the first test that needs
  to find a real file under the repo root rather than a temp file it wrote
  itself.
  **Verified, not just built:** full `vb_tests` green on `build-lua/`
  (`VB_WITH_LUA=ON`, headless-only build, 180/180) and on `build-net/`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=OFF`, real GNS + real client, 147/148 —
  the 1 failure is the pre-existing sandbox UDP-bind one from §3, confirmed
  unrelated). Ran the actual dedicated server twice in a row against
  `content/base` (`build-lua/voxel_browser_server.exe --content-pack
  content/base`) and watched `vb.storage.boot_count` go 1 → 2 across the
  two separate process runs, proving the persistence path really works, not
  just that the code compiles. Ran a real two-process smoke test over
  `build-net`'s GNS build: `voxel_browser_server.exe --content-pack
  content/base --port 27099` + a real headless `voxel_browser.exe --server
  127.0.0.1 --port 27099` joined successfully and received an
  8-block registry (the unchanged Phase 2 base set, since this build has no
  Lua) — confirms `load_content_pack`'s `VB_WITH_LUA`-off degrade path
  doesn't break a live join. **First caught a stale-binary trap while doing
  this:** `build-net`'s `voxel_browser_server.exe` hadn't been rebuilt after
  editing `src/server/main.cpp` (only `voxel_browser`/`vb_tests` were
  explicitly targeted) — the old binary printed the *previous* commit's
  version banner and rejected the handshake outright (protocol mismatch, 3
  vs. 7); always rebuild every target that changed, not just the one you
  think you're testing, before trusting a live-process smoke test.
  **Not done / worth knowing before touching this again:** no full
  4-way combination (`VB_WITH_LUA=ON` *and* `VB_WITH_NET=ON` *and* a real
  client build) exists in this environment's build dirs, so the "a real
  Lua-driven block registry (more than 8 blocks) reaches a real
  GNS-connected client" path specifically is unverified live — it rests on
  `build-lua`'s server-side test coverage (Lua registration definitely
  works) plus `build-net`'s live-join coverage (the wire path definitely
  works) separately, not both at once. If content ever adds a genuinely
  new block (not just a re-declaration), that combination is worth a real
  live check before trusting it blind.
  **This local box's build-dir matrix** (useful to know before picking one
  instead of rediscovering it via CMakeCache.txt greps): `build/` — Ninja,
  RelWithDebInfo, everything off (headless-only, no Lua/net). `build-lua/` —
  Ninja, Debug, `VB_WITH_LUA=ON`, **no client target** (raylib not fetched,
  `VB_BUILD_CLIENT` off) — only `voxel_browser_server`/`vb_core`/`vb_tests`
  exist here. `build-net/`, `build-release/`, `build-asan-nonet/` — Visual
  Studio 18 2026 generator (multi-config, pass `--config Debug`/`Release`),
  `VB_WITH_LUA=OFF`; `build-net` has `VB_WITH_NET=ON` + a real client
  target, the other two don't (check `CMakeCache.txt` before assuming).
  None of these combine `VB_WITH_LUA=ON` with a real client — see above.

  **Small, separate finding while staging this commit:** `.gitignore` and
  `tests/CMakeLists.txt` are checked in with CRLF line endings (`file`
  confirms it; every other text file checked, including every new file this
  session, is LF-only) even though `core.autocrlf=input` is set locally —
  `git add` prints "CRLF will be replaced by LF the next time Git touches
  it" for both on every `git add`, harmlessly for a content-only edit but
  worth knowing before a real edit to either file: the first edit through a
  CRLF-preserving tool will silently flip the whole file's line endings in
  that diff, burying the actual change under a wall of noise. No
  `.gitattributes` exists to force this repo-wide either way. Not fixed
  here (out of scope for a content-pack pass, and normalizing either file
  produces a noisy diff of its own) — if it's ever worth cleaning up,
  `git add --renormalize <path>` after adding a `.gitattributes` rule is
  the low-noise way, not a manual line-ending find/replace.

- **2026-09-16 — Phase 5.3 main menu landed (uncommitted).** New
  `vb::render::MainMenu` (`inc/vb/render/main_menu.hpp` +
  `src/render/main_menu.cpp`) draws menu/settings/connecting/error screens
  with plain raygui calls (no Lua — engine-level per spec §5.3, distinct from
  `UiRuntime`/`UiRenderer` which draw *pack* screens post-join). See
  `REMAINING_TASKS.md` 5.3 for the full feature list.
  **The real structural change** is in `src/client/main.cpp`: it used to
  block on a full connect-then-handshake loop *before* ever constructing a
  `Window` — no way to show a menu, a connecting screen, or recover from a
  failed connect without the process exiting. Windowed mode now creates the
  `Window` first and runs an explicit `AppState{kMenu, kSettings,
  kConnecting, kPlaying, kError}` machine in the frame loop; `--headless` was
  deliberately left as a byte-for-byte-unchanged `run_headless()` function
  (the old blocking body, verbatim) since CI's only coverage of this file is
  the 3 headless smoke tests and none of them should need to change for a
  windowed-only feature.
  **New `vb::core::save_client_config()`** (`config.hpp`/`.cpp`) — toml++
  serializes a fresh `toml::table` from `ClientConfig` and overwrites the
  file; regenerates from scratch, doesn't preserve comments in a hand-edited
  `client.toml` (acceptable for a Settings-screen save, not attempted to fix
  by e.g. re-parsing-and-patching the existing file in place).
  `ClientConfig::recent_servers` already existed as a parsed-but-never-used
  field since Phase 1.1's config loader — this is the first thing to
  actually read and write it.
  **Verified:** clean `-DVB_WARNINGS_AS_ERRORS=ON` build of
  `voxel_browser`/`voxel_browser_server`/`vb_tests` on `build-asan-nonet`
  (MSVC + ASan), full `ctest` green (4/4, same one N/A here since this is the
  non-net build — no GNS UDP-bind test to hit the sandbox limitation at
  all), and a real windowed launch + full-screen screenshot confirming the
  main menu lays out correctly (title panel, player name / address / port
  fields, Connect / Play Singleplayer / Settings / Quit buttons all where
  the layout math says they should be, no overlap). **Not verified:**
  clicking through to Settings/Connecting/Error — a scripted-input attempt
  (PowerShell `SetForegroundWindow` + synthetic `mouse_event` clicks at the
  Settings button's screen coordinates) took a second screenshot still
  showing the main menu, i.e. the click didn't land. Most likely
  `SetForegroundWindow` silently failing (a well-known Windows restriction:
  a background process generally can't steal foreground focus from another
  process without the target's cooperation) rather than an app bug — raygui
  buttons here use the exact same `GuiButton`-returns-true-on-click pattern
  already exercised by `UiRenderer`/`pack_runtime_integration_test.cpp`, and
  the state-machine wiring around each button (`begin_connect`,
  `menu.open_settings(config)`, etc.) was code-reviewed, not just
  typed-and-hoped. Worth an actual manual click-through before trusting the
  non-main screens blind; if it's ever automated again, try
  `AllowSetForegroundWindow`/attaching input queues (`AttachThreadInput`)
  instead of a bare `SetForegroundWindow` call from an unrelated process.
  **Known gaps, not attempted (see REMAINING_TASKS.md 5.3 for the full
  list):** no byte-progress bar on the connecting screen (asset-sync never
  grew progress-fraction accounting, 4.4's pre-existing gap); window
  width/height/vsync changes apply on restart only, not live-resized;
  keybindings are still hardcoded, no rebind UI.

- **2026-09-16 (4th): Phase 5.4 chat** — `C2S_Chat` (100) landed (it was
  reserved in `MessageType` since Phase 4.2 but never given a struct);
  `kEngineProtocolVersion` bumped 7 → 8. The interesting part wasn't the
  codec, it was that `PackRuntime::dispatch_chat`/`vb.on("chat", handler)`
  already existed (Phase 4.2) with **nothing to call it** — same
  shipped-mechanism-ahead-of-content pattern as most of Phase 4/5.1. Closed
  it the same way 4.5's `C2S_UiEvent`/`set_ui_event_handler` did:
  `ServerSession::set_chat_handler(function<bool(NetId, string_view)>)`
  mirrors `set_ui_event_handler`'s shape (optional callback, unset = default
  behavior) but returns a veto bool instead of firing a one-way event —
  `ServerSession::handle_chat` calls it, and only if it returns true (or
  isn't set at all) does `ServerSession` itself format `"<name>: <text>"`
  and broadcast `S2C_Chat` to every playing connection. Formatting the
  "name: text" line server-side (not client-side, not left to Lua) was a
  deliberate small choice: it means `--singleplayer` (no `PackRuntime`, no
  handler ever set) gets working chat for free instead of needing its own
  formatting path — "unset = allow" was picked specifically so chat isn't a
  pack-gated feature.
  `ClientSession` gained `send_chat(text)` (thin wrapper, same shape as
  `send_ui_event`) and `take_chat_messages()` (drains a `vector<string>`,
  same drain-on-call convention as `take_open_ui()`).
  **Client HUD** (`src/client/main.cpp`, `kPlaying` state only): Enter opens
  a `GuiTextBox` in permanent edit mode (not the click-to-toggle pattern
  `UiRenderer`/`MainMenu` use elsewhere, since there's nothing else to click
  — chat only has one field) and releases mouse capture the same way an
  open pack UI already does (`ui_runtime.is_open() || chat_open` now both
  gate it); Enter again sends + closes, Escape cancels without sending. A
  plain `std::deque<std::string>` caps the visible log at 8 lines,
  bottom-left, above the input box when open. No raygui state (edit-mode
  bool, buffer) needed persisting across frames beyond `chat_buf` itself,
  unlike `UiRenderer`'s per-widget-id maps — there's exactly one field.
  **Verified:** `tests/unit/protocol_test.cpp` round-trip;
  `tests/unit/netcode_test.cpp` new case proves a real broadcast over
  `LoopbackTransport` reaches both clients including the sender, and that an
  empty line is silently dropped server-side (not broadcast, not even to
  the sender) rather than sent as a blank `S2C_Chat`;
  `tests/unit/pack_runtime_integration_test.cpp` new case proves the Lua
  veto path end-to-end (a `vb.on("chat", ...)` that checks `player:get_name()`
  actually suppresses one player's line while the other's still gets
  through). Full `ctest` green (4/4) on `build-asan-nonet`; clean
  `-DVB_WARNINGS_AS_ERRORS=ON` build of all three targets; a real windowed
  `--singleplayer` launch ran a few seconds without crashing or an ASan
  report (not a full manual click-through of the chat box itself — same
  `SetForegroundWindow` automation limitation noted in the Phase 5.3 entry
  above applied here too, not re-attempted).
  **Not attempted:** rate limiting / flood guard (`ServerSession` has none —
  a malicious client can spam `C2S_Chat` freely, same unaddressed gap
  `handle_input_batch` has for input flooding, tracked in Phase 1.3/3.2's
  "belongs with `GnsTransport`" notes); `/`-prefixed commands; timestamps;
  chat history isn't part of `S2C_JoinAccept` so a late joiner sees nothing
  said before they connected.

- **2026-09-16 (5th): Phase 5.4 player list / join-leave messages** — three
  new server-generated (no Lua involvement) message types:
  `S2C_PlayerJoin` (104), `S2C_PlayerLeave` (105), `S2C_PlayerList` (106);
  `kEngineProtocolVersion` bumped 8 → 9. Landed in `chat.hpp`/`chat.cpp`
  alongside chat rather than a new file — same "social RPC" grouping those
  files already used. `ServerSession` sends `S2CPlayerList` (everyone else
  already playing) to a connection the instant it finishes joining, then
  `S2CPlayerJoin` to everyone *else* already playing; a disconnect broadcasts
  `S2CPlayerLeave` to whoever's left. Both new broadcast loops sit right next
  to the existing `joins_`/`leaves_.push_back(...)` bookkeeping in
  `ServerSession::tick` (that vector was always server-internal only, for
  `take_joins()`/`take_leaves()` callers like the dedicated server's log
  lines — this is the first time join/leave became a real wire event).
  `ClientSession` keeps a `unordered_map<NetId, string> players_` synced from
  all three message types, exposed as `players()`. Join/leave **share the
  chat log**, not a separate channel: `apply_gameplay_frame`'s new
  `kS2CPlayerJoin`/`kS2CPlayerLeave` cases both push a
  `"* <name> joined/left the game"` line into the same `pending_chat_` that
  backs `take_chat_messages()` — no second HUD widget needed for that half,
  since 5.4's chat log (landed a few hours earlier, same day) already
  existed and a join/leave notice is conceptually the same kind of
  transient line. **Design trap this caused, fixed:** two pre-existing chat
  tests (`netcode_test.cpp`'s broadcast test, `pack_runtime_integration_test
  .cpp`'s veto test) join two clients back-to-back without draining chat in
  between, then assert `take_chat_messages().size() == 1` right after
  sending — the new "* Bob joined the game" line (Bob joins moments after
  Alice, both still mid-`pump()`) was silently included, failing both with
  an off-by-one. Fixed by draining `take_chat_messages()` once right after
  both `REQUIRE(...joined())` and before the real assertions in both tests —
  the correct fix, not a workaround: a real client would already have
  rendered/cleared that system line before the player goes on to type
  anything, so draining it first matches actual usage.
  `src/client/main.cpp`: a small always-visible player list, top-right
  corner (own name highlighted, everyone from `client->players()` below it).
  Deliberately **not** gated behind a hold key — Tab is already bound to
  mouse-capture release (see 5.3's HUD/menu bindings), and there's no
  spare key doing nothing else right now; always-on is simpler than adding
  a new binding for one pass.
  **Verified:** `tests/unit/protocol_test.cpp` new round-trip case for all
  three structs (including an empty `S2CPlayerList`, mirroring
  `S2CBlockRegistry`'s empty-list case); a new `tests/unit/netcode_test.cpp`
  case joins Alice alone first (drains her empty player list), then joins
  Bob and asserts Bob's list already contains Alice, Alice got the join
  broadcast + `"* Bob joined the game"` chat line, and disconnecting Bob
  (`tb.close(*idb, "left")`, the same trigger-the-server's-kDisconnected-
  event pattern `pack_runtime_integration_test.cpp` already used) removes
  him from Alice's `players()` and produces `"* Bob left the game"`. Full
  `ctest` green (4/4) on `build-asan-nonet` after fixing the two pre-existing
  tests above; clean `-DVB_WARNINGS_AS_ERRORS=ON` build of all three targets.
  Not re-verified with a live windowed launch this pass (the HUD list is a
  straightforward `DrawText` loop over an already-tested data source,
  `client->players()` — same judgment call as not re-clicking through
  5.3's menu screens every session).
  **Not attempted:** no distinct "system message" visual styling beyond the
  `"* "` prefix (still the same log/`take_chat_messages()` stream as real
  chat, no separate colour or channel); player list carries no extra
  per-player metadata (ping, idle time, `net_id` itself isn't shown in the
  HUD even though it's in the map).

- **2026-09-16 (6th): Phase 5.4 day/night cycle** — `S2C_TimeOfDay` (46);
  `kEngineProtocolVersion` bumped 9 → 10. `S2C_JoinAccept::time_of_day` has
  existed since Phase 1.3 (spec §8.3's handshake diagram always included it)
  but was pure dead weight until now: no server ever advanced it (`JoinGrant
  ::time_of_day` defaulted to 0 and nothing touched it after), and there was
  no message to keep an already-connected client's copy current even if it
  had. Same "shipped mechanism, no content" pattern as most of Phase 4/5.
  **New pure-logic header**, `inc/vb/world/daynight.hpp` +
  `src/world/daynight.cpp` — deliberately no raylib dependency (same
  reasoning as `vb/render/entity_visual.hpp`: keeps it unit-testable without
  a GL context). `kTicksPerDay = 24000` (0 = sunrise, 1/4 = noon, 1/2 =
  sunset, 3/4 = midnight — picked to match the existing `S2CJoinAccept`
  doc-comment "ticks into the day cycle" and give round numbers, not lifted
  from anywhere in the spec, which doesn't pin an exact tick count).
  `advance_time_of_day(current, dt, day_length_seconds)` does the
  accumulate-and-wrap math; `sky_brightness`/`sky_color_for_time` are a
  simple 4-keyframe (sunrise/noon/sunset/midnight) lerp — "simple sky
  gradient" per the spec line, not a physically based sky model.
  **Server:** `ServerSession` gained a `double time_of_day_ticks_`
  accumulator (double, not the wire `u32`, so slow real-time accrual doesn't
  get truncated to zero every tick), advanced once per `tick()` via
  `advance_time_of_day`; `set_day_length_seconds(seconds)` (default 1200.0
  == 20 real minutes/day, arbitrary but a common game-y pace, no spec
  number to match). The constructor's existing `on_ready` wrapper (the one
  that already fills in `net_id`/`world_seed` if a user hook left them at
  their zero-ish defaults — Phase 1.3) now also unconditionally sets
  `grant.time_of_day` from the live accumulator, so every join gets the
  *current* time, not whatever a test/host happened to return. Broadcasts
  `S2C_TimeOfDay` to every playing connection roughly once a second
  (`kTimeOfDayBroadcastIntervalSeconds`, a plain accumulator next to the
  existing `kAssetSendBudgetPerTick`-style anonymous-namespace constants) —
  deliberately coarser than snapshots/world state, since a clock only needs
  to *look* smooth, doesn't need per-tick precision over the wire.
  **Client:** `ClientSession::time_of_day()` returns
  `join_accept()->time_of_day` until the first `S2C_TimeOfDay` lands, then
  the latest broadcast value (`std::optional<uint32_t> time_of_day_override_`)
  — same "unset falls back to the handshake's own copy" shape as
  `players()`/chat needed no such fallback (those start genuinely empty,
  this one has a real seed value from the handshake itself).
  `src/client/main.cpp`: in the `kPlaying` state only, right before
  `BeginMode3D`, a second `ClearBackground` call (raylib allows multiple
  per frame; only the state actually drawn after it matters) overwrites
  `Window::begin_frame()`'s flat dark clear with
  `sky_color_for_time(client->time_of_day())`. `draw_overlay()` gained an
  "HH:MM" line, computed from the convenient fact that 24000 ticks/24h is
  exactly 1000 ticks/hour (`time_of_day / 1000`, `(time_of_day % 1000) *
  60 / 1000`) — no floating point needed for the readout.
  **Verified:** `tests/unit/daynight_test.cpp` (new, 8 cases: rate matches
  `day_length_seconds`, wraps at `kTicksPerDay`, a misconfigured
  zero/negative day length freezes instead of NaN/dividing by zero,
  brightness peaks at noon and dims toward midnight, both wrap cleanly past
  `kTicksPerDay`, color interpolates smoothly not in discrete jumps);
  `tests/unit/protocol_test.cpp` round-trip; a new
  `tests/unit/netcode_test.cpp` case (day length sped up to 100s/day so the
  test doesn't need to wait 1200s) proving a joined client's `time_of_day()`
  advances past its join-time value once the periodic broadcast lands (with
  a **known sharp edge, deliberately not over-asserted**: the client only
  ever reflects the *last broadcast*, not the server's continuously-ticking
  clock, so the test checks `<=` against `server.time_of_day()`, not `==` —
  an early draft asserted equality and flaked on timing) and that a second
  client joining later gets a strictly later `JoinAccept.time_of_day` than
  the first, proving the grant is live-filled per join. Full `ctest` green
  (4/4) on `build-asan-nonet`; clean `-DVB_WARNINGS_AS_ERRORS=ON` build of
  all three targets. Not re-verified with a live windowed launch this pass
  (same judgment call as the two 2026-09-16 entries above it — the actual
  sky-color math is unit-tested in isolation, and the render-loop glue is a
  two-line `ClearBackground`/`DrawText` call over an already-tested value).
  **Not attempted:** no ambient-light/mob-spawning gameplay coupling
  (cosmetic only this pass); no Lua binding to read/set the day length or a
  starting time_of_day (`set_day_length_seconds` is C++-only,
  `PackRuntime`/`vb.` has nothing for it); the sky is a flat
  `ClearBackground` fill, not a skybox/sun/moon/star render.

- **2026-09-16 (7th): Phase 5.4 death/respawn** — no new `MessageType`, no
  `kEngineProtocolVersion` bump (the only wire traffic is an existing
  `S2C_Chat` sent to one connection instead of broadcast, which the codec
  already supported — `send_message` has always taken a single `ConnId`,
  broadcasting is just something callers choose to loop over, see
  `handle_chat`'s loop vs. this feature's single `send_message` call).
  `ServerSession::Conn` gained two new fields: `spawn_pos` (captured once,
  at the same point `move.position` is already seeded from `JoinGrant` at
  join) and `health` (plain `float`, default 20 to match `ecs::Health`'s
  default — but this is *not* wired to that component; Phase 3.1's EnTT
  registry is still deferred, `ServerSession` still drives players directly,
  so `health` here is just more ad-hoc `Conn` state next to `move`/`look`,
  same category as everything else in that struct).
  **Design choice, and why:** the respawn trigger is `health <= 0`, checked
  generically every tick in a new `check_respawns()` — not a narrower
  `position.y < void_kill_y` branch that respawns directly. The actual only
  producer of damage this pass *is* the void check (`if (position.y <
  void_kill_y) health = 0`), but routing it through the generic health path
  means a future combat/fall-damage system gets a working respawn for free
  by just decrementing `health` — no `check_respawns()` change needed. This
  mirrors the "mechanism ahead of content" pattern the rest of Phase 4/5
  keeps repeating, but inverted: here the *content* (void-kill) arrived
  with a slightly wider *mechanism* (generic low-health respawn) than the
  content alone needed, deliberately, because the wider version was barely
  more code.
  `void_kill_y` is configurable (`ServerConfig::void_kill_y` /
  `server.toml`'s new `void_kill_y` key / `ServerSession::set_void_kill_y`),
  default -64.0 — an arbitrary "comfortably below any sane terrain" guess,
  **not validated against every worldgen seed's actual floor**; wired into
  `src/server/main.cpp` next to the existing `gravity` config wiring.
  `--singleplayer`'s `IntegratedGame`/`ServerSession` never calls
  `set_void_kill_y`, so singleplayer just gets the same -64.0 built-in
  default — untested whether that's ever reachable by falling through
  unloaded terrain (Phase 3.3's ground-load freeze should prevent that
  specific case, per its own entry above).
  On respawn: `health` reset to 20, `move` reset to a fresh `MoveState{}`
  with `position = spawn_pos` (velocity zeroed too, not just position —
  otherwise a player who died mid-fall would respawn still carrying
  terminal-velocity downward momentum and immediately re-trigger the void
  check next tick), `interest_.upsert(...)` updated so *other* players see
  the teleport on their very next snapshot rather than waiting for the
  respawned player's next input batch to refresh `interest_` (input batches
  are the normal path that keeps `interest_` current, per `handle_input_batch`
  — respawn needed its own explicit update since it happens independent of
  any input arriving), and a `S2C_Chat{"* you died and respawned"}` sent
  with a plain single-`ConnId` `send_message` call (not the broadcast loop
  chat/join/leave all use) so only the dying player sees it — deliberately
  no killfeed/broadcast-to-everyone this pass, spec line just says "with
  spawn point", nothing about visibility to others.
  **Client needed zero new code.** `S2C_EntitySnapshot.local` already carries
  whatever `state.move.position` is server-side each tick (Phase 3.4's
  reconciliation, `broadcast_snapshots` in `session.cpp`), and the client
  already unconditionally snaps its predicted position to `local` on every
  snapshot and replays only its *unacked* inputs on top. A respawn is just
  an unusually large position jump through a pipeline that already existed
  for ordinary lag-compensation corrections — confirmed this by not touching
  `src/client/main.cpp` or `ClientSession` at all for this feature, only
  `ServerSession`/`ServerConfig`.
  **Verified:** new `tests/unit/netcode_test.cpp` case — sets
  `void_kill_y` to `spawn.y - 5.0` (not a hardcoded absolute number, so the
  test doesn't depend on whatever height worldgen happened to pick for its
  fixed seed), flies the player straight down past it via `kInputFlyDown`,
  asserts `server.player_move_state(id)->position.y` snapped back to
  spawn height (`Approx(spawn.y)`, not still sitting below the void
  threshold), and asserts `"* you died and respawned"` shows up via
  `take_chat_messages()`. `tests/unit/config_test.cpp` gained a
  `void_kill_y` TOML-parsing assertion in its existing values test. Full
  `ctest` green (4/4) on `build-asan-nonet` (first attempt, no failures to
  fix this time — day/night's two 2026-09-16 entries above both needed a
  fix-up round, this one didn't); clean `-DVB_WARNINGS_AS_ERRORS=ON` build
  of all three targets. Not re-verified with a live windowed launch (same
  judgment call as the three 2026-09-16 entries above it).
  **Not attempted:** no fall damage for a survivable fall (binary
  instant-kill-or-nothing at the void threshold, no damage curve); no
  broadcast/killfeed of someone else's death; `Health`'s `max` field
  (20 here is a bare literal matching `ecs::Health::max`'s default, not
  read from that component or anywhere configurable) — a pack wanting a
  different max HP has no lever to pull yet.

- **2026-09-16 (8th): Phase 5.4 sfx hooks — documented as not implemented,
  closing out Phase 5.4.** Pure documentation, no code: added an "Audio /
  sfx — not implemented" section to `docs/lua-api.md` (right before its
  existing "Sandbox" section). Confirmed, rather than assumed, three facts
  before writing it: `cmake/Dependencies.cmake` really does build raylib
  with `SUPPORT_MODULE_RAUDIO OFF`; `ARCHITECTURE_SPEC.md` §10.5's
  block-break event-flow diagram really does say "sfx trigger" (in the
  `on_break` step) with nothing else in the spec backing it up as a real
  API; and `REMAINING_TASKS.md`'s existing "Deferred (post first-playable)"
  list already had "Audio subsystem + Lua sfx/music API" as its own line —
  this was tracked, just not documented anywhere a Lua-API reader would
  see it. All three now cross-reference each other. This was the last open
  item in Phase 5.4 — see the `### 5.4 Play polish  ✅ done (2026-09-16)`
  status line and paragraph in `REMAINING_TASKS.md` for the four-item
  session summary (chat, player list/join-leave, day/night, death/respawn,
  sfx docs).

- **2026-09-16 (9th): Phase 5.1 real inventory sync + basic hotbar, plus a
  stale-checklist correction for Phase 5.2's Lua veto.** Picked up
  `REMAINING_TASKS.md` 5.1's "no wire message syncing inventory contents to
  the client at all" gap.
  **Correction made along the way, worth flagging so it doesn't get
  re-"discovered" as a task later:** `REMAINING_TASKS.md` 5.2 still read "Lua
  veto + region protection: seam left, waits on Phase 4.2" as an open item.
  Checked the actual code before starting new work on it (per this file's own
  "verify before recommending" discipline) and found it was **already fully
  wired** — `BlockEditHooks` (`inc/vb/net/world_replicator.hpp`),
  `PackRuntime::attach_world`/`on_block_edit_before` (`src/script/
  pack_runtime.cpp`), and `src/server/main.cpp` calling `attach_world`, with
  an existing passing integration test
  (`pack_runtime_integration_test.cpp`'s "pack script vetoes a block break
  and observes on_break"). This was presumably wired during Phase 4.2 itself
  and the 5.2 checklist entry was simply never updated after. No dedicated
  "region protection API" exists (`ARCHITECTURE_SPEC.md §14`'s one mention of
  the phrase) and none was added — a pack veto handler already receives the
  player + block position, so a claims/region check is just Lua reading
  `vb.storage` inside the same `vb.on("block_break"/"block_place")` handler;
  judged not worth a dedicated C++ API unless a real pack needs one.
  Corrected the checklist text and status paragraph in `REMAINING_TASKS.md`
  to match reality; no code changed for this half.
  **New work — real inventory:** `S2C_Inventory` (107) —
  `inc/vb/protocol/inventory.hpp` + `src/protocol/inventory.cpp`
  (`InventorySlot{item, count}`, `varint n` + `n × {u16, u16}`), round-trip
  tested. `kEngineProtocolVersion` bumped 10 → 11 (`cmake/version.hpp.in`,
  `docs/protocol.md`). `PackRuntime::Impl::sync_inventory(NetId)`
  (`src/script/pack_runtime.cpp`) builds a full snapshot from
  `inventories[id]` and sends it via the same `conn_for_player` +
  `net::send_message` pattern `send_message`/`open_ui` already used; called
  from `PlayerHandle::give()` right after mutating the slot vector — the only
  inventory mutator that exists, so "sync after give" covers every current
  write path. Client side: `ClientSession::inventory_` (new member,
  `inc/vb/net/session.hpp`) + a new `kS2CInventory` case in
  `apply_gameplay_frame` (`src/net/session.cpp`), exposed read-only via
  `ClientSession::inventory()`. Deliberately a full-resend snapshot on every
  change, not a delta — same posture `S2C_PlayerList` already established,
  and inventories are small (a handful of slots) so there's no real cost to
  resending the whole thing.
  **New work — hotbar:** `src/client/main.cpp`, drawn right before the
  `ui_runtime.is_open()` block (same place the player list / chat HUD blocks
  already live): one box per `client->inventory()` slot, bottom-center,
  block name (`chunk_store().registry().get(item).name`, falls back to "?"
  for an id the client's current registry doesn't recognize) + count as
  plain `DrawText` — no slot-select input, no icons (every block is still
  textureless per 4.3/5.1's own long-standing gap, so there's nothing to draw
  an icon *from* yet), matching `ui/inventory.lua`'s existing text-only
  posture rather than inventing new visual language ahead of real art.
  **Tests:** `tests/unit/protocol_test.cpp` ("inventory round-trips,
  including an empty snapshot") and a new
  `tests/unit/pack_runtime_integration_test.cpp` case ("player:give() pushes
  a live S2C_Inventory to the client") — drives a real
  `ServerSession`/`ClientSession` pair over `LoopbackTransport`, triggers
  `give()` from a `vb.on("chat", ...)` handler (chosen because it's the one
  existing veto callback that already hands a real `PlayerHandle` — 4.2's own
  noted gap is that `player_join` only hands a name, not a handle, so that
  seam couldn't be reused here), and asserts `client.inventory()` matches
  after a tick pump.
  **Verification, three separate build trees (this repo has several
  pre-configured, see the top of §3):** `build-lua` (`VB_WITH_LUA=ON`,
  `VB_BUILD_CLIENT=OFF`) — full `vb_tests` green, 196/196 cases, including
  both new ones; `build-asan-nonet` (`VB_WITH_LUA=OFF`, `VB_BUILD_CLIENT=ON`,
  ASan) — confirms `voxel_browser`/`voxel_browser_server` compile clean with
  the new `ClientSession::inventory()` member and hotbar drawing code (this
  is the tree that actually has a client target; `build-lua` here doesn't),
  full `ctest` green (`vb_tests` 162/162 + all three smokes). Not yet done:
  no live windowed screenshot of the hotbar (no GL context available in this
  environment, same limitation noted throughout 5.3/5.4) — rests on the
  clean build + the fact its drawing code follows the exact same
  `DrawText`/`DrawRectangle` pattern the already-verified player-list HUD
  block uses right above it.

- **2026-09-16 (10th): Phase 5.2 hold-to-break progress.** Picked up
  `REMAINING_TASKS.md` 5.2's last remaining checklist line, "Break progress
  (hold-to-break): not yet."
  **Change, entirely in `src/client/main.cpp`, no protocol/server change:**
  the block-break click handler swapped `IsMouseButtonPressed` for
  `IsMouseButtonDown` and gates sending `C2S_BlockEdit` behind a new
  `breaking`/`break_target`/`break_progress` trio of locals — LMB must stay
  held on the *same* `look_hit.voxel` for a flat `kBreakSeconds` (0.35,
  chosen to feel roughly like the old instant-break but give the new
  progress bar something to visibly fill) before the edit actually goes out;
  switching to a different voxel or releasing the button resets progress to
  0, and losing mouse capture (menu/chat open) resets it too. Server-side
  validation/veto (already done, this session's 9th-adjacent correction
  above) is completely unaffected: the wire message this sends is byte-for-
  byte the same `C2SBlockEdit`, just deferred until the hold completes, so
  nothing downstream needed touching. Placing (RMB) stays instant, untouched.
  A small filled progress bar (plain `DrawRectangle`, no raygui) is drawn
  screen-space below the would-be crosshair position while `breaking` is
  true — there's still no actual crosshair sprite/reticle anywhere in the
  client; deliberately didn't add one here to keep this change scoped to
  just the one checklist item, though a future pass adding a reticle should
  probably anchor the bar to it instead of a bare screen-center offset.
  **Deliberately NOT attempted (separately tracked, not this item):**
  per-block hardness or tool-dependent break time — every block takes the
  same 0.35s regardless of type; `REMAINING_TASKS.md`'s own "Tool/hardness
  times: not yet" line (right above this one in the same section) already
  covers that as a distinct, larger follow-up (would need a `BlockType`
  field, `S2C_BlockRegistry` wire changes, and a tool/item concept that
  doesn't exist yet — out of scope for a hold-to-break pass).
  **Verification:** `build-asan-nonet` (the tree with `VB_BUILD_CLIENT=ON`)
  — a clean recompile of `voxel_browser` at `/W4` produced no new warnings
  (checked with `-clp:WarningsOnly`, only the pre-existing unrelated ASan
  `/INCREMENTAL`-ignored linker warning appeared); full `ctest` green (all
  four cases, including the three smokes) on the same tree. No windowed
  screenshot of the progress bar itself (no GL context here, same limitation
  as every other HUD element added this session) — rests on the clean build
  plus the fact the drawing code is the same `DrawRectangle`/
  `DrawRectangleLines` shapes the hotbar (9th entry, same session) already
  used successfully.

- **2026-09-16 (11th): Phase 5.1 dropped-item entity.** Picked up
  `REMAINING_TASKS.md` 5.1's last remaining item: `entities/dropped_item.lua`
  registered `base:dropped_item` declaratively but nothing ever spawned one —
  block drops went straight into the breaking player's inventory
  (`ctx.player:give()`), never through a world entity.
  **Key design decision, made before writing any code:** does this need
  Phase 3.1's EnTT registry? No. Re-read `vb::replication::InterestGrid`
  (`inc/vb/replication/interest.hpp`) and `ServerSession::broadcast_
  snapshots()` (`src/net/session.cpp`) closely and confirmed both are
  already fully generic over `NetId` + `EntityKindId` — nothing in the
  interest grid, the snapshot builder, or the client's `EntityRenderer`
  (Phase 3.5) assumes an entity is a player. The "no generic entity system"
  gap `register_entity`/`vb.world.spawn` keep citing is specifically about
  nothing calling a *pack's* `on_spawn`/`on_tick` Lua callbacks — the
  replication/rendering plumbing underneath was already entity-kind-agnostic
  from Phase 1.4/3.5. This meant a hardcoded, non-Lua-driven drop system
  could piggyback on that plumbing for free, with zero protocol changes.
  **New pure module:** `vb::world::ItemDropSystem`
  (`inc/vb/world/item_drops.hpp` + `src/world/item_drops.cpp`) — no net/
  script dependency, so unit-testable standalone
  (`tests/unit/item_drops_test.cpp`, 4 cases: id-space separation, pickup
  radius, lifetime expiry, independent multi-drop tracking). `spawn()`
  allocates ids from `0x8000'0000` upward specifically so they can never
  collide with `ServerSession::next_net_id_`'s player ids (which start at 1
  and count up by one per join) — the two counters never need to
  coordinate. `tick(dt, players)` is a pure function: ages every drop,
  checks position against every given player, returns `{pickups, removed}`
  for the caller to apply; it doesn't touch replication or inventories
  itself.
  **ServerSession wiring:** `spawn_item_drop(pos, item, count)` (new public
  method, `inc/vb/net/session.hpp`) upserts the new drop straight into
  `interest_` with a reserved `world::kItemDropKind` sentinel
  (`EntityKindId{0xFFFF}`, chosen to sit above any pack-registered kind,
  which count up from 1) — from that point on it's indistinguishable from a
  player to the replication/broadcast code, so **no new S2C message was
  needed at all**, and the client's existing `EntityRenderer` draws it as a
  tinted billboard with zero client-side changes. `update_item_drops()`
  (called once per tick, right after `check_respawns()`) builds the
  players-for-pickup-check list from `interest_.get(state.net_id)->pos`
  rather than `Conn::move.position` directly -- deliberate, so pickup
  detection works identically whether a player's position came from real
  input-driven movement (`handle_input_batch`, which upserts both) or the
  test/script-facing `set_player_state()` (which only upserts `interest_`,
  never touches `Conn::move`) — caught this distinction while writing the
  integration test below, before it became a real bug: an early draft read
  `state.move.position` and pickups silently never fired when a test moved
  a player via `set_player_state`. New `set_item_pickup_handler` callback
  (same `std::function`-based, no-Lua-dependency shape as
  `set_chat_handler`/`set_ui_event_handler`) — unset means picked-up items
  just vanish, matching "no handler, no side effect" everywhere else.
  **PackRuntime wiring:** `world_tbl["spawn_item_drop"]` (new Lua binding,
  `src/script/pack_runtime.cpp`) calls `session->spawn_item_drop` directly —
  deliberately a *separate* binding from `world_tbl["spawn"]`, which stays
  the inert `vb.register_entity`-kind path waiting on 3.1; conflating the two
  would have implied item drops are pack-entity-kind-driven, which they
  aren't. `PackRuntime::attach_session` gained a `set_item_pickup_handler`
  wire that pushes straight into `inventories[player]` +
  `sync_inventory(player)` — the exact same two calls `PlayerHandle::give()`
  makes, so a pickup is indistinguishable from a script handing the item to
  you directly (this session's 9th-entry inventory-sync work, reused as-is).
  **Content pack:** all six `content/base/blocks/*.lua` on_break handlers
  (`dirt`/`grass`/`leaves`/`sand`/`stone`/`wood`) swapped `ctx.player:give()`
  for `vb.world.spawn_item_drop({...ctx.pos + 0.5...}, id, 1)` — breaking a
  block now drops a real, visible item at that position instead of an
  instant inventory credit. `entities/dropped_item.lua`'s comment rewritten
  to explain the two systems don't share any code (the registration is
  kept purely as a placeholder for whenever a pack might want custom
  per-drop `on_tick` behavior through the real future EnTT path).
  **Tests:** `item_drops_test.cpp` (pure, 4 cases, see above) plus a new
  `pack_runtime_integration_test.cpp` case ("vb.world.spawn_item_drop
  replicates to a client and is picked up on approach") driving a real
  `ServerSession`/`ClientSession` pair over `LoopbackTransport` through all
  three states: too far to be visible, visible but out of pickup range, and
  collected (entity gone from `remote_entities()`, item credited to
  `client.inventory()`). `content_pack_test.cpp`'s existing "content/base
  loads cleanly" case (unmodified) confirms the six edited Lua files still
  parse/register without needing any change to that test, since it never
  exercised `on_break`'s body, only registration.
  **Verification, both build trees again (see the 9th entry for why two are
  needed):** `build-lua` — full `vb_tests` green, 201/201 (196 baseline + 4
  new `ItemDropSystem` cases + 1 new integration case), including a
  `content/base` re-parse. `build-asan-nonet` (`VB_WITH_LUA=OFF`) — confirms
  `ItemDropSystem`/`ServerSession`'s new non-Lua-gated code (the class
  itself, `spawn_item_drop`, `update_item_drops`, the pickup-handler
  plumbing) compiles clean with the Lua binding code compiled out entirely;
  clean build of `voxel_browser`/`voxel_browser_server`/`vb_tests`, full
  `ctest` green (all 4 cases). Not yet done: no live two-window playtest
  actually watching an item drop render and get picked up (no GL context
  here, same limitation as every other visual feature this session).

- **2026-09-16 (12th): Phase 5.1 simple crafting recipes — deliberately
  engine-agnostic, per explicit user direction.** The user asked for
  crafting but was explicit up front: "it should be in content as example
  since voxel browser is a generic voxel game browser." That framing decided
  the whole design before any code was written — this project's engine is
  meant to stay a generic host, with every actual game rule living in a
  content pack (`content/base` is documented, `ARCHITECTURE_SPEC.md` §16, as
  "the reference implementation of the Lua API," not a hardcoded ruleset).
  **What's engine-side, and why each piece earns that:**
  1. `PlayerHandle::take(itemstack) -> bool` (`src/script/pack_runtime.cpp`)
     — the symmetric counterpart to the already-existing `give()`. Removes
     up to `count` of `item` across however many inventory slots hold it,
     all-or-nothing (no partial consumption on an under-supply — sums
     availability across slots first, only mutates if the full amount is
     confirmed available). This is a generic inventory primitive exactly
     like `give()` already was; the engine has no idea it's being used for
     "crafting" specifically, the same way it has no idea `give()` is used
     for "picking up a mined block."
  2. `src/script/pack_loader.cpp`'s `load_content_pack` now also loads any
     other `*.lua` file sitting directly at a pack's root (sorted,
     `init.lua` excluded and always handled last) — after `blocks/`/
     `entities/`/`biomes/`, before `init.lua`. This is what let
     `crafting.lua` exist as a real, loadable file at all, without engine
     code special-casing "crafting" as a known content category the way
     `blocks`/`entities`/`biomes` already are. A pack could just as easily
     drop `weather.lua` or `economy.lua` there. Purely additive — no
     existing pack had a loose root-level `.lua` file besides `init.lua`,
     so no other pack's load order changes.
  **Everything else — actual game rules — is content, in
  `content/base/crafting.lua`:** the recipe list (a plain local Lua table,
  not read back from the engine's write-only `vb.register_craft` capture —
  `register_craft` is still called per recipe purely so the engine-side
  record exists for a hypothetical future consumer, e.g. a crafting-table
  UI's item grid), the matching logic (sum each required item across
  `player:get_inventory()`, an already-existing primitive), and the trigger
  (`/craft <name>` over chat, vetoing the raw command text so it never
  broadcasts, whether or not the craft actually succeeds). Two new
  craftable-only blocks back the wood → planks → sticks chain this session
  chose as the example (matching `REMAINING_TASKS.md`'s own suggested
  example verbatim): `content/base/blocks/planks.lua` (solid) and
  `blocks/sticks.lua` (`solid = false` — the closest available
  approximation to "not really a wall," since held items and placeable
  blocks still share one `BlockId` space; a separate item-id space is a
  bigger, still-open 5.1 gap this didn't touch).
  **A subtlety worth remembering if this needs revisiting:** `run_veto`
  (`src/script/pack_runtime.cpp`) calls each registered handler for an event
  in registration order and returns `false` **immediately** on the first
  handler that returns `false` — it does not call every handler
  unconditionally. This meant the test-only "give me some wood" chat handler
  added in `content_pack_test.cpp`'s new integration test had to be written
  so it only reacts to its own exact trigger text and returns `true`
  (pass-through) otherwise — if it had returned `false` unconditionally, or
  been registered *before* `crafting.lua`'s handler while both cared about
  overlapping text, one could have silently starved the other. Not a new
  finding (existing chat-veto tests already relied on the same behavior),
  but easy to trip over when stacking a second handler onto a pack that
  already has one, as this test does.
  **Tests:** `tests/unit/pack_runtime_test.cpp` — "player:take() removes
  items across slots, all-or-nothing" exercises the engine primitive in
  isolation via `PackRuntime::dispatch_chat()` directly (no `ServerSession`
  needed at all: `give`/`take`/`get_inventory` never touch the network
  layer). Written so a wrong Lua-side result actually fails the C++ `CHECK`
  — an earlier draft had the Lua handler `assert()` internally and always
  `return true`, which would have silently passed even on a wrong `take()`
  result, since a Lua error inside a veto handler is caught and warned by
  `run_veto`, not treated as a veto itself; rewritten so the handler returns
  the real outcome and the C++ side asserts on that. `tests/unit/
  content_pack_test.cpp` — a new "content/base crafting: wood -> planks ->
  sticks via /craft chat" case loads the *real* `content/base` files through
  a real `ServerSession`/`ClientSession` pair over `LoopbackTransport` and
  drives the full example end-to-end: missing-ingredients rejection (no
  inventory change), a successful two-recipe chain with exact before/after
  counts at each step, an unknown-recipe rejection, and confirms an ordinary
  chat message still broadcasts normally alongside the command handling.
  Also had to update that file's pre-existing "content/base loads cleanly"
  registry-size assertion (`base_size + 2`, was `base_size`) since planks/
  sticks are genuinely new blocks, not `add_or_get` re-declarations of the
  Phase 2 base set the way every other `blocks/*.lua` file's registration
  is.
  **Verification, both build trees again:** `build-lua` — full `vb_tests`
  green, 203/203 (201 baseline + 1 new `take()` unit case + 1 new crafting
  integration case). `build-asan-nonet` (`VB_WITH_LUA=OFF`) — confirms both
  engine changes compile clean: `PlayerHandle::take` is inside the
  Lua-gated half of `pack_runtime.cpp` and compiles out with the rest of it,
  while the `pack_loader.cpp` change is plain `std::filesystem` code with no
  Lua dependency at all and is exercised either way; clean build of all
  three targets, full `ctest` green (all 4 cases). Not yet done: no live
  two-window playtest of `/craft` actually being typed into the real HUD
  chat box (no GL context here, same limitation as every other
  content/gameplay feature this session) — rests on the integration test
  driving the identical `ServerSession`/`ClientSession` code path a live
  client's chat box would.

- **2026-09-16 (13th): `--singleplayer` now runs the real content pack —
  found while writing this session's README update, not asked for
  directly.** Drafting a "try crafting in `--singleplayer`" quick-start line
  for `README.md` prompted actually checking whether that claim was true
  before writing it down (per this file's own "verify before recommending"
  discipline) — it wasn't. `src/client/main.cpp`'s `Singleplayer` struct
  wrapped `vb::net::IntegratedGame` with no `PackRuntime` anywhere in
  sight, meaning `--singleplayer` had *always* run on the hardcoded
  `BlockRegistry::base()` set with zero Lua scripting, going all the way
  back to whichever session first wrote `Singleplayer` — chat, crafting,
  item drops, everything this session and the two before it built, only
  ever worked for real multiplayer (`--server`), never singleplayer,
  despite `--singleplayer` being the easiest way for a first-time user (or
  agent) to try any of it.
  **Root blocker, and why `IntegratedGame` couldn't just grow a
  `PackRuntime` parameter:** a `PackRuntime` needs the raw server-side
  `net::Transport&` (to send chat/give/item-drop messages), which
  `IntegratedGame` never exposes (its `LoopbackNetwork net_` member is
  private, only `server()`/`client()` accessors exist) — and separately,
  `PackRuntime::install_join_veto(host)` must run **before**
  `ServerSession` is constructed (it wraps `host.authenticate`, and
  `ServerSession` copies `host` by value at construction), which
  `IntegratedGame`'s single all-in-one constructor gives no hook for: by
  the time a caller could reach in, `ServerSession` already exists.
  **Fix:** rebuilt `Singleplayer` to construct `LoopbackNetwork`/
  `ServerSession`/`ClientSession` directly — the exact same pieces
  `IntegratedGame` wraps, just not through it — mirroring exactly how
  `tests/unit/pack_runtime_integration_test.cpp` already does this for
  every PackRuntime-involving test (that's *why* those tests don't use
  `IntegratedGame` either, in hindsight — worth remembering next time
  something here reaches for `IntegratedGame` and also wants scripting).
  Member declaration order (which decides C++ initializer-list *execution*
  order, not the order written) had to be gotten right: `net` →
  `registry` → `pack_runtime` (loads + freezes `content/base` into
  `registry` before anything else touches it) → `world`/`pool` (built from
  the now pack-extended `registry`) → `server` (built from a host that
  `make_singleplayer_host` has already run `install_join_veto` +
  `block_registry` on, using the same `pack_runtime`/`registry`). Two new
  free-function factories (`make_singleplayer_pack_runtime`,
  `make_singleplayer_host`) exist specifically so this ordering could
  happen entirely inside a member-initializer list rather than needing a
  two-phase-construction workaround.
  **A second, smaller gap fixed at the same time:** `host.block_registry`
  (`HandshakeServerHost`, Phase 4.3) was never wired for singleplayer
  either — without it, a joining client stays on its own `base()` registry
  regardless of what the server-side one grew to, so `crafting.lua`'s
  `planks`/`sticks` blocks would exist server-side (inventory give/take
  works fine, since that's just numeric ids) but resolve to nothing
  client-side — the hotbar (this session's earlier work) would have shown
  "?" for them. Mirrors `src/server/main.cpp`'s own `host.block_registry`
  callback line-for-line.
  **New `Singleplayer::tick(dt)`** centralizes what used to be scattered
  `sp->game.tick(dt)` calls across `main.cpp` (`run_headless`, the
  windowed connecting-loop, and the main playing loop — 6 call sites) —
  now also drains `take_joins()`/`take_leaves()` into
  `dispatch_player_join_completed`/`dispatch_player_leave` and calls
  `pack_runtime.dispatch_tick(dt)` every tick, exactly mirroring
  `src/server/main.cpp`'s own loop body so a pack behaves identically
  whether a real dedicated server or this in-process one drives it. Every
  `sp->game.tick(dt)`/`sp->game.client()` call site became
  `sp->tick(dt)`/`sp->client()` (mechanical, via `sed`, then verified by
  full rebuild — no behavior change intended or found at any site).
  **Degrades gracefully, not fatally**, if `content/base` isn't found
  relative to the working directory (logs a warning, keeps running with
  the hardcoded base block set) — deliberately different from the
  dedicated server's fatal-on-broken-pack posture (`src/server/main.cpp`
  exits on a bad content pack), since a first `--singleplayer` run
  shouldn't hard-fail just because of where it happened to be launched
  from.
  **Verified with an actual headless run, not just "it compiles":**
  `voxel_browser --headless --frames 5 --singleplayer --name CiBot` now
  prints `[base] content pack loaded (boot #1)` and `received block
  registry (10 blocks)` (10 = the base 8 + `planks`/`sticks`) — neither
  line appeared at all before this fix, confirming the pack genuinely
  loads and the client genuinely receives the extended registry, not just
  that construction succeeds. Full build + `ctest` (all 4 cases) green on
  `build-asan-nonet` (`VB_BUILD_CLIENT=ON`, `VB_WITH_LUA=OFF` — confirms
  the no-Lua stub path, where `PackRuntime`/`load_content_pack` degrade to
  no-ops, still works); `build-lua` reconfigured with
  `-DVB_BUILD_CLIENT=ON` (it previously had the client off) to get a real
  `voxel_browser` binary with `VB_WITH_LUA=ON` — clean build, full
  `vb_tests` green (203/203).
  **Found along the way, confirmed pre-existing and out of scope, left
  alone:** running `ctest` in the freshly-client-enabled `build-lua` tree
  for the first time (previous sessions only ever ran `./vb_tests.exe`
  directly there) turned up `server_smoke` failing —
  `voxel_browser_server`'s asset-manifest builder hits a real I/O error
  because `build-lua` also happens to have `VB_WITH_COMPRESSION=ON` (every
  *other* tree in this repo has it `OFF`) and ctest's working directory
  (the build dir) has no `content/base` at all (`content_pack` is a plain
  relative path, resolved from whatever the process's cwd happens to be).
  Confirmed unrelated to this session's changes: `src/server/main.cpp`
  itself is untouched, the failure reproduces identically regardless of
  any content-pack edit, and no CI workflow currently sets
  `VB_WITH_COMPRESSION=ON` at all, so this exact combination has just never
  been exercised via `ctest` before now. Not fixed here (would mean making
  `content_pack` path resolution cwd-independent, or making the dedicated
  server's manifest-build failure non-fatal like singleplayer's pack-load
  failure now deliberately is — either is a real, separate, small
  follow-up if `VB_WITH_COMPRESSION` ever gets CI coverage).

- **2026-09-16 (14th): Phase 5.5 documentation pass — `CONTRIBUTING.md`
  added, `docs/lua-api.md`/README's remaining staleness fixed.** Closes
  out the last checkbox in `REMAINING_TASKS.md` 5.5 (the section is now
  `✅`) — `CONTRIBUTING.md` didn't exist at all before this. New file
  covers: a module map (every `src/`/`inc/vb/` subdirectory → its target →
  one-line purpose, pulled from each subdirectory's own `CMakeLists.txt`
  header comment rather than invented from scratch); build/test workflow
  tips not already in the README (running more than one build tree side by
  side for `VB_WITH_LUA` on/off rather than reconfiguring back and forth —
  this project's own sessions already do exactly this, see the 13th
  entry's `build-lua`/`build-asan-nonet` pairing; doctest's `--test-case`
  glob-not-substring gotcha, already documented in §4 above, cross-
  referenced rather than duplicated); code style (`.clang-format`, no
  exceptions on engine hot paths, `vb::<module>` namespacing); a concrete
  step-by-step for adding a new wire message (struct → `MessageType` →
  round-trip test → version bump → `docs/protocol.md` entry → integration
  test) synthesized from this backlog's own repeated pattern rather than
  invented; and a "generic primitive, not a game-specific one" guideline
  for new Lua bindings, using the same-session crafting work
  (`player:take()` + a generic `load_content_pack` root-module loader,
  vs. game rules living entirely in `content/base/crafting.lua`) as the
  worked example.
  Also fixed the last checkbox text staleness: `docs/lua-api.md` and
  `README.md` had *already* been substantially rewritten earlier this
  session (see the 12th-adjacent work), but `REMAINING_TASKS.md` 5.5's own
  checkboxes still read `[ ]` for both — updated to `[x]` with notes
  explaining what was actually done and when, so a future session doesn't
  re-do work that already happened. `docs/protocol.md` was checked for
  drift and found already current (confirmed `kEngineProtocolVersion` 11
  matches `cmake/version.hpp.in`) — every prior session in this backlog
  updated it in the same commit as its wire change, so there was nothing
  to fix there, just confirm.
  No code changes this entry — documentation only. Not verified by any
  automated check (`CONTRIBUTING.md` isn't parsed by anything); read
  through once for internal consistency against the actual current
  `CMakeLists.txt`/`tests/CMakeLists.txt` contents before publishing.

- **2026-09-16 (15th): Cellulose evaluated as the `VB_WITH_MESHING` backend,
  then reverted — hand-rolled `vb::world::chunk_mesher` is now the permanent
  choice, not a placeholder.** §19 Q2 and this file's §5 entry above both
  called Cellulose's API shape "unknown, inspect before writing meshing
  code" — that spike finally happened this session. Fixed a real bug first:
  Cellulose's own `CMakeLists.txt` names its CMake target `libcellulose`
  (`LIBRARY_NAME = "lib${PROJECT_NAME}"`), not `cellulose` — `src/render/
  CMakeLists.txt` and `cmake/Dependencies.cmake`'s `NOT TARGET` guard both
  referenced the wrong name, so `target_link_libraries` silently fell back
  to treating `cellulose` as a raw linker item and the linker went looking
  for a `cellulose.lib` that could never exist (it's header-only/
  `INTERFACE`) — `LNK1104`. Also moved the link from `vb_render` to
  `vb_core`, since `chunk_mesh_snapshot.cpp` (the actual `greedy_mesh` call
  site per the header comments) lives there, not in render.
  With that fixed, `mesh_chunk_from_snapshot` was wired to build a
  `cellulose::MeshSample` grid from the existing `ChunkMeshSnapshot` (face
  visibility + per-corner AO computed the same way as the hand-rolled path,
  walked along `cellulose::impl::face_layout`'s axis_u/axis_v so
  `face_occlusion` packs the way `greedy_mesh` expects) and call
  `greedy_mesh(..., ambient_occlusion=true, weld_t_junctions=true)`. Built
  clean, all 166 `vb_tests` cases passed both with the flag on (after
  updating a handful of tests that hardcoded the hand-rolled mesher's
  unmerged quad counts — a full solid chunk's shell is 6144 unit quads
  unmerged vs. 6 once greedy-merged, etc.) and off.
  **Then it crashed in actual play**, reported as the same class of failure
  as §1's/this file's earlier NVIDIA-driver VAO/VBO heap-corruption bug (see
  the entry above this one's sibling investigation, and the 2026-09-15
  `chunk_renderer.cpp` headroom/`UpdateMeshBuffer()` mitigation) — rapid
  `UnloadModel`+`UploadMesh` churn. Root cause: that mitigation only helps
  when a re-mesh's new vertex/index count still fits the chunk's existing
  (25%-headroom) GPU buffers. The hand-rolled mesher emits a fixed 4
  vertices/6 indices per visible face, so an edit's effect on buffer size is
  small and local. Greedy-merged output doesn't have that property — merging
  means a single block edit near a merge boundary can swing a whole face's
  quad count by a lot (splitting or joining large merged rectangles), so
  chunks blow past their headroom and fall back to full unload/recreate far
  more often, which is exactly the churn pattern that reproduces the driver
  bug. The fewer-draw-calls win from greedy meshing came directly at the
  cost of undermining the one thing keeping the renderer stable on affected
  drivers.
  **Reverted in full** (nothing had been committed, so a plain `git
  checkout --` on the touched files restored the pre-spike state exactly):
  `chunk_mesh_snapshot.cpp` is back to the sole hand-rolled implementation,
  `VB_WITH_MESHING` is back to unused/untested (and still carries the
  `cellulose` vs. `libcellulose` target-name bug described above — nobody
  should re-flip this flag without re-applying that fix first), and
  `cmake/Dependencies.cmake`'s Cellulose `FetchContent` block is untouched
  (harmless dead scaffolding while the flag stays off). `README.md`'s Tech
  Stack row for Voxel Meshing now says this outright instead of "not yet
  wired in". This closes out design question #2 in §6 below and
  `ARCHITECTURE_SPEC.md` §19 Q2 with the opposite outcome from what Q2
  originally planned — see that section's updated resolution note.
  **If anyone revisits a greedy mesher here** (Cellulose or otherwise), the
  VAO/VBO churn sensitivity above is the thing to solve first, e.g. giving
  merged chunks proportionally more headroom, or decoupling "AO enabled"
  (the actual source of the volatility, since it's what breaks merge-key
  uniformity at edit boundaries) from whether merging happens at all.

- **2026-09-17: Wire librg in as the interest backend (`VB_WITH_REPLICATION`).**
  `cmake/Dependencies.cmake` already declared a `vb::librg` INTERFACE target
  and `src/core/CMakeLists.txt` already linked it under the flag, but nothing
  compiled `LIBRG_IMPL` or called into the header — the flag turned the
  dependency on and did nothing else (`REMAINING_TASKS.md` still had "Wire
  real librg in" and "Map players ↔ librg network entities" unchecked).
  **Checked the actual v7.4.0 API first** (`code/header/{general,entity,
  query}.h` in a throwaway clone) rather than trusting `docs/replication.md`'s
  existing summary, which turned out to describe an older/different librg
  shape (`librg_world_write`/`LIBRG_WRITE_*` framing callbacks) — the real
  v7.4.0 surface for a pure interest query is `librg_world_create/destroy`,
  `librg_config_chunk{size,amount,offset}_set`, `librg_entity_track/untrack`,
  `librg_entity_owner_set` (self-owned — required for an id to later be
  usable as a query owner; `librg_world_query` always force-includes entities
  owned by the querying id, which is how it returns "my own" objects), the
  per-tick `librg_entity_chunk_set(librg_chunk_from_realpos(...))`, and
  `librg_world_query` itself (grow-and-retry on its overflow return, per its
  own documented contract).
  **Kept the existing public interface exactly.** `InterestGrid` (`inc/vb/
  replication/interest.hpp`) split into a header (declarations only, no
  `librg.h` include — the librg world is stored as an opaque `mutable void*`
  so the header never needs the vendored C99 header) and `src/replication/
  interest.cpp`, which `#ifdef VB_WITH_REPLICATION`s between the librg-backed
  implementation and the original Phase 1 linear scan verbatim. No caller
  (`ServerSession`, both replication tests) needed a single line changed —
  same `upsert`/`remove`/`clear`/`get`/`visible_from` signatures either way.
  One necessary interface narrowing, documented in the header: the librg
  backend ignores `visible_from`'s `eye` parameter and instead uses the
  querying id's own last-`upsert`ed position, since librg's query is
  owner/chunk-based, not a free-floating-point query — true of every existing
  call site already (`ServerSession::broadcast_snapshots` always upserts a
  player before its own first snapshot).
  **librg's `LIBRG_IMPL` isolated in its own target** (`src/replication/
  librg_impl.c` → `vb_librg_impl`, linked `PRIVATE` into `vb_core`), same
  reason and same pattern as `vb_raygui_impl`: librg is vendored C99 and must
  never see the project's `-Werror` flags. New `src/replication/CMakeLists.txt`
  (subdirectory wasn't registered in `src/CMakeLists.txt` before — interest.hpp
  was purely header-only, pulled in via `vb_core`'s include path with no `.cpp`
  attached to any target).
  **Known limitation, documented in `docs/replication.md` and in code:**
  librg chunk ids need `chunkamount.x*y*z` to fit a signed 32-bit int
  internally, and `librg_chunk_from_realpos` casts each axis to `int16_t`
  chunks — `interest.cpp` picked 1024 chunks/axis (product ~1.07e9, safely
  under `INT32_MAX`), covering ±512×`cell_size` world units per axis around
  the origin. An entity straying outside that range gets `LIBRG_CHUNK_INVALID`
  and is silently excluded from everyone's interest until it re-enters — no
  crash. This was flagged as an explicit open item in the original spike doc;
  a bigger world needs a bigger `chunkamount` (traded against the same int32
  overflow risk) or a movable local origin, neither of which exists yet — not
  attempted here since nothing today needs a world bigger than ±16384 units
  per axis (default 32-unit cells).
  **CI**: added `-DVB_WITH_REPLICATION=ON` to all three `build_*.yml` OS legs,
  same posture as `VB_WITH_NET`/`VB_WITH_LUA` — librg needs no new system
  packages (single vendored C99 header, FetchContent only), so this was a
  low-risk addition, unlike the still-off `VB_WITH_NET` on macOS (multi-arch
  protobuf problem, see that workflow's own comment).
  **Verification:** built `-DVB_WITH_REPLICATION=ON` fresh with
  `CC=clang CXX=clang++` (this box's toolchain) — all 166 existing test
  cases / 99875 assertions pass unchanged (including both replication tests
  in `tests/unit/replication_test.cpp`, which exercise `InterestGrid`
  directly and the two-client join/move/leave visibility scenario). Also
  rebuilt with the flag off to confirm the linear-scan path is byte-for-byte
  unaffected (identical 166/99875 pass counts). Confirmed `interest.cpp`
  itself compiles clean under the project's exact `-Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-align -Wunused -Woverloaded-virtual -Wdouble-promotion -Werror` set
  in both configurations (isolated single-file compile, since building all of
  `vb_core` end-to-end with `-Werror` on this box's clang 21 currently fails
  on unrelated EnTT `meta.hpp`/`dense_map.hpp` `-Wsign-conversion` noise —
  confirmed pre-existing on `main` with the same toolchain before touching
  anything, not something this change introduced or can fix from here; CI's
  pinned compiler versions apparently don't hit it, since "CI is fully green"
  per the 2026-09-10 entry above).

- **2026-09-17: Flip `VB_WITH_REPLICATION` default from OFF to ON.** librg is
  now the default entity-replication backend (`CMakeLists.txt` option default),
  not just something CI opts into — it's the more battle-tested option, and
  the hand-rolled linear scan remains available as an explicit opt-out
  (`-DVB_WITH_REPLICATION=OFF`) for anyone who'd rather avoid the extra
  dependency. No source changes; `InterestGrid`'s dual-backend `#ifdef`
  structure (`src/replication/interest.cpp`) is unchanged. Updated
  `README.md` and `docs/replication.md` to describe librg as the default,
  not the opt-in, backend. CI's explicit `-DVB_WITH_REPLICATION=ON` in the
  three `build_*.yml` legs is now redundant but harmless — left as-is rather
  than churned.

- **2026-09-17: Design-only pass, "Phase 6 — Lua-Driven Extensibility."** No
  code changed. Captured a design discussion into `ARCHITECTURE_SPEC.md`
  (§7.1-7.2, §10.3-10.6, §17, §19 Q6) and a new `REMAINING_TASKS.md` Phase 6
  section, covering four systems: (1) entity kinds as Lua "classes" with
  spawned entities as "objects" carrying per-instance state via the
  already-declared-but-unused `ScriptState` component; (2) reframing
  `ui.define` from static declaration to an immediate-mode `render(state)`
  function called every UI frame — leans on `raygui` already being
  immediate-mode, so no virtual-DOM diffing is needed for a React/Svelte-like
  feel; (3) a closed-schema custom-keybind channel
  (`vb.register_keybind`/`vb.on("player_input", ...)`) where the flood
  defense is the wire format itself (bounded bitset, registered set only)
  rather than a post-receipt filter, plus a server-side input-interception
  hook (veto or replace) between `IngestInputSystem` and
  `MovementIntegrationSystem`; (4) a generic per-key `vb.db` store, distinct
  from the existing pack-global `vb.storage`, explicitly designed so the
  engine has no opinion on "logged in" — any auth/identity model is entirely
  pack-implemented on top of it (joining a world ≠ authenticating), with the
  engine only offering a `vb.crypto.hash` primitive so a pack that does build
  login doesn't roll its own credential hashing in pure Lua. None of this is
  implemented; it's a backlog entry, not a code change.

- **2026-09-17: Design-only pass, "Phase 6.5 — Shared block-damage
  breaking."** No code changed. Follow-up to the Phase 6 pass above, adding
  block breaking as a shared damage pool rather than an instant edit:
  `BlockType` gains `max_damage` (0 = today's instant break, default, no
  behavior change) and an optional `crack_texture` override; a sparse
  server-side `pos → damage` map rides the *existing* interest/replication
  system as a transient record (appears/disappears via the same spawn/despawn
  diffing every other replicated object already gets, so nearby players see
  cracks form without a new wire channel); `C2S_BlockBreakBegin`/`...Stop`
  bracket a player contributing, gated by the same reach/tool/protection
  checks `C2S_BlockEdit` already has; two symmetric per-tick Lua hooks split
  mechanism from policy — `block_break_tick` (per contributing player, sums
  concurrently so multiple players can break together) adds damage,
  `block_health_tick` (per damaged block, every tick) lets Lua decide heal
  policy entirely (no heal / full heal / decay / heal-after-idle, engine has
  no default); completion still drives the existing unchanged
  `C2S_BlockEdit`/`BlockEditSystem`/`on_break` pipeline. Crack rendering is
  default-with-override (baseline generic overlay + optional per-block
  texture), explicitly noted as blocked on the still-pending real
  texture/atlas system (4.3/5.1) landing first. Updated
  `ARCHITECTURE_SPEC.md` §5.2, §8.5, §10.3, new §10.7, §17, and added
  `REMAINING_TASKS.md` Phase 6.5. Backlog entry, not a code change.
  (Two further design-only passes landed the same day without their own
  STATE.md entries — `REMAINING_TASKS.md` Phase 6.6-6.13, a codebase audit
  for more Lua-extensibility candidates including a foundational player
  damage/death gap, and a Deferred-section note on rule-based decorative
  structure placement authored via an external tool.)

- **2026-09-17: Redesign worldgen §6 stage 2 (biome selection) to
  Voronoi-cell, adjacency-weighted probability.** No code changed — the
  Phase 4.2/6 worldgen pipeline is still fully unimplemented (base pipeline
  is heightmap-only today). This replaces the originally-sketched continuous
  temperature/humidity noise selection with a discrete Voronoi cellular
  partition where each cell's biome is a weighted draw from
  `vb.register_biome`'d biomes, weighted by that biome's base probability
  **times an adjacency-compatibility factor** against already-resolved
  neighboring cells (e.g. desert next to tundra near-zero, desert next to
  plains normal) — Lua supplies both tables, engine only draws.
  Deliberately **not** textbook wave-function-collapse: cell resolution
  order is fixed by a hash of `(world_seed, cell_id)` rather than
  exploration/entropy order (so two players approaching the same seed from
  different directions can't see different layouts — would violate the
  `(world_seed, chunk_coord, pack_version)`-purity every other stage
  depends on), and adjacency weights are **soft multipliers with a floor,
  never hard exclusions**, so a cell can never hit a contradiction and the
  algorithm never needs backtracking — required for a world generated
  lazily per-chunk that must always succeed, unlike a bounded grid solved
  once. Also added a new **stage 5, vein/scatter pass** for underground
  ore/valuable-block placement (`{block, target_rock, height_range,
  vein_size, spawn_rate}` per biome or pack-global) — this didn't exist
  anywhere in the prior pipeline sketch at all, not even as a stub; decor
  (trees) is stage 6 and lighting stage 7 now, renumbered from the prior
  5/6. Updated `ARCHITECTURE_SPEC.md` §6 and the corresponding
  `REMAINING_TASKS.md` worldgen items (2.2, 4.2) plus a stale `§6 stage 5`
  cross-reference in the Deferred section (now stage 6). Backlog entry, not
  a code change.

- **2026-09-17: Cellulose's dormant `VB_WITH_MESHING` scaffolding removed
  outright, at the user's request** — the 15th entry above (2026-09-16) had
  reverted the actual meshing wiring but deliberately left the build-flag
  and `FetchContent` block in place as documented dead scaffolding, "kept
  only in case a future session wants to re-investigate a different
  greedy-mesh approach." No further investigation is planned, and the
  scaffolding was judged not worth carrying forward just in case. Removed:
  the `VB_WITH_MESHING` option (`CMakeLists.txt`), its `FetchContent`
  declaration (`cmake/Dependencies.cmake`), the conditional
  `target_link_libraries`/`target_compile_definitions` block in
  `src/render/CMakeLists.txt`, the dependency-table row in
  `docs/protocol.md`, and the explanatory comments in `chunk_mesher.hpp`/
  `chunk_mesh_snapshot.hpp`/`chunk_mesh_worker_pool.hpp` that referenced it.
  `ARCHITECTURE_SPEC.md` §19 Q2, `REMAINING_TASKS.md` §2.5, and `README.md`'s
  Tech Stack row were reworded to say the dependency was removed rather than
  left unused. This entry (and the 15th entry it follows) remain as the
  historical record of why Cellulose isn't here — no source code besides
  the flag/comment surface was ever touched by this cleanup, since none of
  it had built or run since the revert. Nothing to test: `VB_WITH_MESHING`
  had no effect on any default build before this change, so removing it
  changes no build output.
