## Phase 5 — Minimum Playable Base

> Full history for this phase; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.

Goal: a small, coherent, playable multiplayer sandbox.

### 5.1 Base content pack (`content/base`)  🚧 (registration + wiring done; art/inventory-sync deferred)

- [x] `pack.toml`, `init.lua`. **`entry` field is not actually read** — real
      `require` still doesn't exist (4.1), so a pack's `init.lua` can't pull
      in its own `blocks/*.lua` itself. New `vb::script::load_content_pack`
      (`inc/vb/script/pack_loader.hpp` + `src/script/pack_loader.cpp`) is the
      substitute: the *host* walks `blocks/*.lua` (sorted) → `entities/*.lua`
      (sorted) → `biomes/*.lua` (sorted) → `init.lua`, loading each as its
      own chunk into the same `PackRuntime`/Lua state — behaviourally one
      concatenated script, since every file shares `vb.register_*`'s globals.
      `pack.toml`'s `entry` is kept only as the spec-documented (§16) field
      for whenever real `require` lands.
- [x] **Closed a real gap, not just content:** neither binary ever loaded a
      pack before this — `src/server/main.cpp` constructed a `PackRuntime`
      and immediately `freeze()`d it with nothing registered (every Phase 4.2
      test exercised `PackRuntime` directly, never through the server's own
      main). Now wired: `load_content_pack(pack_runtime, config.content_pack)`
      before `freeze()`, fatal-erroring the server on a real pack syntax/
      runtime error (same "broken pack is fatal" pattern as a bad asset
      manifest), non-fatal + once-logged on a `VB_WITH_LUA`-off build.
- [x] Blocks: dirt, grass, wood, leaves, stone, sand — `blocks/*.lua`, each
      `vb.register_block{...}` **re-declaring the exact name** the Phase 2
      hardcoded `BlockRegistry::base()` (`src/world/block.cpp`) already used
      (`add_or_get` is idempotent by name), so ids are unchanged unless the
      pack adds something new. `on_break` gives the broken block back to the
      breaking player via `player:give(...)` — a real, working drop (not the
      still-unmaterialized `on_break` *return-value* path
      `pack_runtime.cpp`'s `on_block_edit_after` only logs). `blocks/grass.lua`
      drops dirt (`base_dirt_id`, a plain Lua global blocks/dirt.lua sets —
      the cross-file-shared-globals mechanism the loader above relies on).
      **No textures** — nothing in the client consumes a per-block texture
      yet (4.3's known gap: "still just untextured cubes"), so no `.png`
      files were added; would be inert weight until a texture/atlas loader
      exists.
- [~] Biome(s): `biomes/plains.lua` / `biomes/forest.lua` call
      `vb.register_biome{...}` with surface/filler/stone (+ a `decoration`
      hint on forest) — **purely declarative**, same as `register_biome`
      already was: nothing reads a biome back (`WorldGenerator` is still the
      Phase 2 hardcoded fBm pipeline, `vb.worldgen.set_pipeline` doesn't
      exist). No tree decoration — the worldgen decoration pass itself is
      unbuilt (2.2).
- [x] Client UI screens wired end-to-end for real multiplayer:
      `src/client/main.cpp` now loads every synced `ui/*.lua` file from
      `ClientSession::virtual_pack_fs()` (Asset Sync, 4.4) into `UiRuntime`
      right after join — the same "mechanism shipped, nothing called it"
      gap 4.5 flagged is closed for *loading*. `ui/pause.lua` and
      `ui/inventory.lua` (`base:pause` / `base:inventory`) are real,
      loadable `ui.define` screens.
      **Known gap, not attempted:** nothing can *open* either screen in
      real gameplay yet — `player:open_ui` is server-push-only and there is
      no client gesture or C2S message requesting "open my inventory" /
      "pause" (5.4 tracks chat/interact messages generally; an open-UI
      request is the same shape of gap). `ui/inventory.lua` also renders
      numeric item ids, not names (`vb.register_item` never allocates its
      own id space — see below), and uses a `list` widget in place of the
      spec's item-grid (still doesn't exist, 4.5).
      **`--singleplayer` still doesn't asset-sync** (no manifest/
      `ClientAssetCache` on the loopback path), so `ui/*.lua` specifically
      still only loads for real multiplayer connections — **but
      `--singleplayer` now runs a real server-side `PackRuntime`** (fixed
      2026-09-16, see 5.1's own entry below), so this gap is now scoped
      down to "no synced client-side asset files," not "no scripting at
      all" the way it read before.
- [ ] Player + dropped-item billboard sprite atlases (§11.3 / 3.5) — replaces the
      Phase 3 flat-placeholder quad with real directional art. Not started.
- [x] Dropped-item entity (2026-09-16): a real, working world item drop —
      `vb::world::ItemDropSystem` (`inc/vb/world/item_drops.hpp` +
      `src/world/item_drops.cpp`), pure/no net dependency, unit-tested
      standalone (`tests/unit/item_drops_test.cpp`: id-space separation from
      player NetIds, pickup-radius collection, lifetime despawn, multiple
      independent drops). `ServerSession::spawn_item_drop(pos, item, count)`
      allocates one and upserts it into the *same* `InterestGrid` a player
      already lives in (`kItemDropKind` sentinel, `EntityKindId{0xFFFF}`) —
      no new wire message needed at all: it replicates through the existing
      `S2C_EntitySnapshot` path, and the client's Phase 3.5 `EntityRenderer`
      already draws any remote entity generically (nothing in it branches on
      `kind`), so a drop just shows up as a tinted billboard for free.
      `ServerSession::update_item_drops()` (called once per tick) ages every
      drop, checks it against every playing connection's *current* interest
      position (works whether that position came from real input-driven
      movement or the test/script-facing `set_player_state`), and reports
      pickups + removals for the caller to apply — `PackRuntime::
      attach_session` wires the pickup handler straight to
      `inventories[player]` + `sync_inventory()` (5.1's real-inventory work,
      above), so picking an item up looks identical to a script calling
      `player:give()`. New Lua binding `vb.world.spawn_item_drop(pos, item,
      count)` (`world_tbl`, `src/script/pack_runtime.cpp`) — deliberately its
      own binding, not routed through the still-inert generic
      `vb.world.spawn`/`vb.register_entity` path (that still waits on 3.1's
      EnTT registry; `entities/dropped_item.lua`'s registration is unchanged,
      kept as the pack-format placeholder for when a script wants custom
      per-drop behavior). `content/base/blocks/*.lua`'s six `on_break`
      handlers now call `vb.world.spawn_item_drop` at the broken block's
      position instead of `ctx.player:give()` directly — breaking a block
      drops a real, visible, walk-over-to-collect item in the world instead
      of an instant inventory credit. Integration test
      (`pack_runtime_integration_test.cpp`, "vb.world.spawn_item_drop
      replicates to a client and is picked up on approach"): a real
      `ServerSession`/`ClientSession` pair over `LoopbackTransport` — spawns
      a drop far from the player (not yet visible), moves the player into
      interest range but short of pickup radius (visible, not collected),
      then onto it (collected: entity disappears from `remote_entities()`,
      `client.inventory()` gains the stack). `content_pack_test.cpp` confirms
      the real edited `content/base` files still parse/register cleanly.
      Verified across `build-lua` (full `vb_tests`, 201/201) and
      `build-asan-nonet` (`VB_WITH_LUA=OFF`, confirming the Lua-gated code
      compiles out cleanly; clean build of all three targets + full `ctest`).
      **Not attempted:** no physics on a drop (it sits exactly where it
      spawned — no gravity/settling, no bounce); no visual distinction
      between drops or from a player (same flat hardcoded billboard 3.5
      already ships, tinted only by a NetId hash — real per-item icons wait
      on textures, same long-standing 4.3/5.1 gap); no stacking/merging of
      nearby same-item drops; no despawn warning/blink before the 120s
      lifetime expires.
- [x] Real inventory (2026-09-16): `S2C_Inventory` (107) —
      `inc/vb/protocol/inventory.hpp` + `src/protocol/inventory.cpp`,
      round-trip tested. `kEngineProtocolVersion` bumped 10 → 11. Closes the
      "no wire message syncing inventory contents to the client at all" gap:
      `PackRuntime::Impl::sync_inventory` pushes a full slot snapshot to a
      player's connection after every `player:give()` (the only mutator that
      exists); `ClientSession::inventory()` holds the latest copy
      (`src/net/session.cpp`'s `apply_gameplay_frame` applies it, same
      posture as `S2C_PlayerList`: always a full resend, no delta-tracking
      machinery for a handful of slots). `ui/inventory.lua`'s one-shot
      `player:open_ui(..., {slots = player:get_inventory()})` snapshot is
      unaffected/still works; this adds a second, always-live path a hotbar
      can read without a script pushing anything. Tests:
      `tests/unit/protocol_test.cpp` (round-trip) and a new
      `tests/unit/pack_runtime_integration_test.cpp` case ("player:give()
      pushes a live S2C_Inventory to the client") driving a real
      `ServerSession`/`ClientSession` pair over `LoopbackTransport`. Not
      attempted: no C++/Lua API to *remove*/consume a stack (nothing needs
      it yet — crafting/consumption are separately unbuilt), no slot-index
      addressing (append-only `slots` vector, same shape `get_inventory()`
      already exposed).
- [x] Basic hotbar (2026-09-16): `src/client/main.cpp` draws
      `client->inventory()` bottom-center, one box per slot, block name (via
      `chunk_store().registry().get(item).name`) + count as plain text — no
      slot-select input, no icons/atlas (waits on real per-block textures,
      4.3/5.1's own still-open gap), same text-only posture
      `ui/inventory.lua` already had. Not visually verified against a live
      window this session (no GL context available here — same limitation
      noted throughout 5.3/5.4); rests on a clean
      `-DVB_WARNINGS_AS_ERRORS=ON` build of `voxel_browser` + the full
      `ctest` suite green.
- [x] Simple crafting recipes (2026-09-16): wood → planks → sticks, exactly
      the example this line originally suggested — and, per explicit user
      direction, implemented **entirely as content, not engine**: the only
      C++ addition is one generic, game-agnostic primitive,
      `player:take(itemstack) -> bool` (`PlayerHandle::take`,
      `src/script/pack_runtime.cpp` — the symmetric counterpart to the
      already-existing `give()`, removing up to `count` of an item across
      however many slots hold it, all-or-nothing, no partial consumption).
      Every actual game rule — what a recipe is, which recipes exist, how a
      player triggers one, what happens on success/failure — lives in a new
      `content/base/crafting.lua`, loaded as a generic top-level pack module
      (see the `load_content_pack` change below). The engine still has zero
      concept of "crafting"; `vb.register_craft` remains a write-only
      registration call as far as C++ is concerned (nothing in
      `src/script/pack_runtime.cpp` reads the `crafts` vector back) — the
      recipe *data* the working feature actually reads lives in a plain Lua
      table inside `crafting.lua` itself, with `vb.register_craft` called
      alongside purely so the engine-side record exists for whenever a real
      consumer (a crafting-table UI's item grid, 4.5's still-missing widget)
      wants it.
      **Generic engine addition #2:** `src/script/pack_loader.cpp`'s
      `load_content_pack` now also loads any other `*.lua` file sitting
      directly at the pack root (sorted, excluding `init.lua`) after
      `blocks/`/`entities`/`biomes/` but before `init.lua` — this is what
      lets `crafting.lua` exist at all without engine code knowing anything
      about crafting specifically; a pack could just as easily drop an
      unrelated `weather.lua` or `economy.lua` there and it would load the
      same way. Purely additive: no existing pack had any other root-level
      `.lua` file, so no other pack's load order changes.
      **Content:** two new craftable-only blocks,
      `content/base/blocks/planks.lua` (solid) and `blocks/sticks.lua`
      (non-solid — the closest approximation to "not a real building
      block" available today, since held items and placeable blocks still
      share one `BlockId` space, see 5.1's own "`vb.register_item` never
      allocates its own id space" gap, unaffected by this change).
      `crafting.lua` triggers off chat: `/craft <name>` checks the
      player's summed inventory against the recipe's inputs
      (`player:get_inventory()`, already existed), `take()`s each input only
      if every one is confirmed sufficient first (all-or-nothing at the
      recipe level too, not just per-`take()` call), then `give()`s the
      output — vetoing (not broadcasting) any `/craft `-prefixed message
      whether or not the craft actually succeeds, so command text never
      shows up as a chat line. All six block `on_break` handlers already
      routed through `vb.world.spawn_item_drop` (this session's earlier
      dropped-item-entity work) rather than `give()` directly, so a crafted
      item is acquired the exact same way a mined one is.
      **Tests:** a new focused unit test
      (`tests/unit/pack_runtime_test.cpp`, "player:take() removes items
      across slots, all-or-nothing") exercises the engine primitive alone,
      via `PackRuntime::dispatch_chat()` directly (no `ServerSession`
      needed — `give`/`take`/`get_inventory` never touch the network layer).
      A new full end-to-end test
      (`tests/unit/content_pack_test.cpp`, "content/base crafting: wood ->
      planks -> sticks via /craft chat") loads the *real* `content/base`
      files through a real `ServerSession`/`ClientSession` pair over
      `LoopbackTransport` and drives the whole example: missing-ingredients
      rejection, a successful two-step craft chain with exact before/after
      counts, an unknown-recipe rejection, and confirms a normal (non-
      `/craft`) chat message still broadcasts untouched. The pre-existing
      "content/base loads cleanly" test's registry-size assertion was
      updated (`base_size + 2`, not `base_size`) since planks/sticks are
      genuinely new blocks, not re-declarations of the Phase 2 base set.
      Verified across both build trees (`build-lua`: full `vb_tests`,
      203/203; `build-asan-nonet`, `VB_WITH_LUA=OFF`: confirms the two
      non-Lua-gated engine changes — `PlayerHandle::take` compiling out with
      the rest of the Lua-gated file, and the plain-filesystem
      `pack_loader.cpp` change — build clean, full `ctest` green).
- [x] `--singleplayer` now runs the real content pack (2026-09-16): closes a
      gap noted since Phase 4.3/5.1 — `src/client/main.cpp`'s `Singleplayer`
      previously wrapped `vb::net::IntegratedGame` with no `PackRuntime`
      involved at all, so `--singleplayer` always ran on the hardcoded
      `BlockRegistry::base()` set with zero scripting, chat commands, or
      item drops, even though the same session's `crafting.lua`/item-drop
      work only actually did anything server-side. Rebuilt `Singleplayer` to
      construct its own `LoopbackNetwork`/`ServerSession`/`ClientSession`
      directly (the same pieces `IntegratedGame` wraps) instead of going
      through `IntegratedGame`, because a `PackRuntime` needs the raw
      server-side `Transport&` (which `IntegratedGame` doesn't expose) and
      needs `install_join_veto()` to run *before* `ServerSession` is
      constructed (order `IntegratedGame`'s single all-in-one constructor
      can't accommodate). Loads `content/base` (hardcoded default, matching
      `server.toml.example`'s own default — no client-side config for this
      exists), wires `attach_world`/`attach_session`/`install_join_veto`
      exactly like `src/server/main.cpp` does, and also wires
      `HandshakeServerHost::block_registry` (previously singleplayer-only
      gap: without it a joining client stays on its own `base()` registry
      and pack-added blocks like `planks`/`sticks` resolve to nothing
      client-side, showing "?" in the hotbar even though give/take work
      fine regardless). A new `Singleplayer::tick()` centralizes
      server/client ticking plus `dispatch_player_join_completed`/
      `dispatch_player_leave`/`dispatch_tick`, mirroring the dedicated
      server's own tick loop, so every one of `main.cpp`'s several
      `sp->game.tick(dt)` call sites collapses to one `sp->tick(dt)`.
      Degrades gracefully, not fatally, if `content/base` can't be found
      (e.g. run from an unexpected working directory) — logs a warning and
      falls back to the hardcoded base block set, deliberately different
      from the dedicated server's "broken pack is fatal" posture, since a
      first-run `--singleplayer` should still just work.
      **Verified with a real headless run, not just a build:**
      `voxel_browser --headless --frames 5 --singleplayer --name CiBot`
      now prints `[base] content pack loaded (boot #1)` and `received block
      registry (10 blocks)` — confirms the pack actually loads, registers
      the two new crafting blocks, and the client receives them, where
      before this fix neither line appeared at all. Full `ctest` green on
      `build-asan-nonet` (the `VB_BUILD_CLIENT=ON` tree, `VB_WITH_LUA=OFF` —
      confirms the refactor doesn't regress the no-Lua stub path either) and
      a clean `voxel_browser`/`vb_tests` build + full `vb_tests` pass
      (203/203) on `build-lua` (`VB_WITH_LUA=ON`, reconfigured with
      `VB_BUILD_CLIENT=ON` to exercise this). **Aside, discovered but out of
      scope:** `server_smoke` fails in a from-scratch `build-lua` ctest run
      specifically because that tree also has `VB_WITH_COMPRESSION=ON` and
      ctest's working directory has no `content/base` (relative-path
      manifest build hits a real I/O error there, unlike the graceful
      degrade above) — pre-existing, unrelated to this fix (`src/server/
      main.cpp` untouched), and not something CI exercises today (no
      workflow turns `VB_WITH_COMPRESSION` on), so left alone; see `STATE.md`.
- [x] New regression coverage: `tests/unit/content_pack_test.cpp` loads the
      *real* `content/base` files (not inline Lua strings, unlike every
      other pack_runtime test) through `load_content_pack`, asserting they
      parse/register cleanly and re-declare the base block ids unchanged;
      plus a broken-pack-file-is-fatal case. Nothing else in the suite
      exercises the shipped files themselves.

### 5.2 Block breaking / placing over the network (§8.5)  🚧

- [x] `C2S_BlockEdit` (break/place, `predicted_seq`, world `pos`, `block_id`) +
      `S2C_BlockEditResult` — `vb/protocol/world.{hpp,cpp}`, round-trip tested.
      Proto version 2 → 3.
- [x] Client optimistic apply + rollback on reject — `ClientSession::push_block_edit`
      / `handle_block_edit_result`; `ClientChunkStore::edit_block` re-meshes the
      chunk + its border neighbours.
- [x] Server validation: reach (≤5.5 m), target validity, non-floating placement
      — `WorldReplicator::apply_block_edit`. **Lua veto: done** — `BlockEditHooks`
      (`inc/vb/net/world_replicator.hpp`) + `PackRuntime::attach_world` wire
      `vb.on("block_break"|"block_place", handler)` as a real pre-apply veto
      (`on_block_edit_before`, `src/script/pack_runtime.cpp`), wired into
      the dedicated server (`src/server/main.cpp`); tested end-to-end over
      `LoopbackTransport` in `pack_runtime_integration_test.cpp`. **No
      dedicated region-protection API** (`ARCHITECTURE_SPEC.md §14`'s
      "per-region protection API" mention) — a pack veto handler already
      receives the player + block position, so a claims/region check is
      just a Lua-side lookup against `vb.storage` inside the same veto;
      nothing further needed C++-side unless a pack wants one built in.
      Tool/hardness times: not yet.
- [x] Apply + bump `revision` + dirty light/mesh + whole-chunk `relight_chunk`.
      `on_break`/`on_place` callbacks: wired (`PackRuntime::Impl::
      on_block_edit_after`). Drops: waits on items (5.1) — the callback's
      return value (a would-be drop) is still logged, not materialized.
- [x] `S2C_BlockEditResult` to the editor + `S2C_ChunkDelta` (block + diffed
      light) fan-out to every player mirroring the chunk.
- [~] Relight on edit: per-chunk from scratch each edit; cross-chunk propagation
      (breaking a floor lets light into the chunk below) still TODO.
- [x] Selection raycast (Amanatides–Woo) + wire-cube highlight; LMB break /
      RMB place stone. Break progress (hold-to-break, 2026-09-16):
      `src/client/main.cpp` now requires LMB held on the *same* voxel for a
      flat `kBreakSeconds` (0.35s) before `C2S_BlockEdit` is actually sent —
      `IsMouseButtonDown` instead of the old instant `IsMouseButtonPressed`;
      switching targets or releasing the button resets progress to zero. A
      small screen-space progress bar (below the would-be crosshair -- none
      exists yet, out of scope here) fills while breaking. Placing is
      unaffected (still an instant `IsMouseButtonPressed`). Purely
      client-side timing gate: the server-side validation/veto path (already
      done, above) is unaffected, since the wire message is identical, just
      sent later. **No per-block hardness/tool system** — one flat duration
      for every block; `REMAINING_TASKS.md`'s "Tool/hardness times: not yet"
      note above is the separately-tracked follow-up for varying it by
      block/tool. Not covered by an automated test (no GL context available
      in this environment, same limitation as the rest of the HUD); verified
      by a clean `/W4` build of `voxel_browser` and the full `ctest` suite
      staying green.

### 5.2 status (2026-09-16): playable over loopback, Lua veto now wired,
hold-to-break landed. `voxel_browser --singleplayer` can break (after a short
hold) and place blocks; a second client sees the change via `S2C_ChunkDelta`;
out-of-reach edits roll back; a pack's
`vb.on("block_break"/"block_place")` can veto a real edit end-to-end (was
already implemented before this session, this pass corrected the checklist
to match — see `STATE.md`'s note the prior status text was stale); breaking
now takes a short hold instead of an instant click. Remaining: drops/tools/
items (5.1), per-block hardness/tool break-time variation, cross-chunk
relight.

### 5.3 Main menu (raygui, engine-level, not pack)  ✅ (keybindings screen deferred)

- [x] `vb::render::MainMenu` (`inc/vb/render/main_menu.hpp` +
      `src/render/main_menu.cpp`, new `vb_render` sibling to `ChunkRenderer`/
      `UiRenderer` — plain raygui calls, no sol2/Lua, no `ClientSession`
      dependency of its own) draws four screens; `src/client/main.cpp` was
      restructured around an explicit `AppState{kMenu, kSettings,
      kConnecting, kPlaying, kError}` state machine that owns which of
      `Singleplayer`/`RemoteConnection` is alive. The window now opens
      *before* any connection attempt in windowed mode (previously
      `main.cpp` blocked on a full connect+handshake before ever creating a
      `Window`).
- [x] Main screen: player name + server address/port text fields (raygui
      `GuiTextBox`/`GuiValueBox`, same edit-mode-toggle pattern
      `UiRenderer::draw`'s `kTextBox` case already used), **Connect**,
      **Play Singleplayer**, **Settings**, **Quit**.
- [x] Connecting/progress screen: shows the live `ClientHandshakeStatus`
      as text (`connecting_status_text()` in `main.cpp` — Connecting /
      Authenticating / Requesting content manifest / Downloading content
      pack / Syncing world) + a **Cancel** button that tears down the
      in-flight `Singleplayer`/`RemoteConnection` and returns to the menu.
      **No byte-progress bar** — `ClientHandshake`/asset-sync (4.4) never
      grew progress-fraction accounting (4.4's own known gap: "no
      connect-screen UI exists yet" — now one does, but the underlying
      counter still doesn't), so this is status-text-only, not a filled bar.
      Real multiplayer connects are pumped one `tick(dt)` per frame with a
      10 s wall-clock deadline (was a blocking `sleep`-based loop before the
      window existed); singleplayer's loopback join is ticked in small
      batches per frame (was a single blocking up-to-128-tick loop) since
      it's synchronous/in-process and finishes in a handful of frames
      regardless.
- [x] Error screen: shows `ClientSession::failure_reason()` (or "connection
      timed out" / a pre-handshake connect failure), **Back to menu** button
      — connect failures no longer exit the process in windowed mode (they
      used to: the old code `return EXIT_FAILURE`d straight out of `main`).
- [x] Recent servers list: `ClientConfig::recent_servers` (already existed
      as an unused field, `client.toml`'s documented `recent_servers = []`)
      is now actually read/written — a `GuiListView` on the main screen
      fills the address/port fields on click; a successful non-singleplayer
      join pushes `"host:port"` to the front (dedup, capped at 8) and
      persists via a new `vb::core::save_client_config()`
      (`inc/vb/core/config.hpp` + `src/core/config.cpp`, toml++ serializer —
      regenerates the file from scratch, doesn't preserve
      `client.toml.example`-style comments in a real `client.toml`).
- [x] Settings screen: window width/height (persisted for next launch, not
      live-resized — labelled "applies on restart"), vsync (same), FOV /
      render distance / mouse sensitivity (`GuiSlider`), asset cache MB
      (`GuiValueBox`); **Save** writes through `save_client_config` and
      returns to the menu, **Back** discards edits.
      **Keybindings: not attempted** — WASD/jump/sprint/break/place are
      still hardcoded in `sample_input_cmd()`/the block-edit block in
      `main.cpp`; out of scope for this pass, no rebinding storage or UI
      exists.
- [x] Integrated-server singleplayer path: unchanged mechanism from Phase 1
      (`Singleplayer` struct, in-process `IntegratedGame` over
      `LoopbackTransport`) — now reachable from the **Play Singleplayer**
      button instead of only `--singleplayer` on the CLI.
- [x] `--headless` deliberately untouched: `run_headless()` in `main.cpp` is
      the pre-5.3 blocking connect-then-run body, byte-for-byte behaviourally
      unchanged, so `server_smoke`/`client_smoke`/`singleplayer_smoke` (CI's
      only coverage of this file) keep passing without modification — the
      new menu code is only reachable in windowed mode. `--singleplayer` or
      an explicit `--server` on the CLI in *windowed* mode skips the menu
      and calls the same `begin_connect()` the Connect/Play Singleplayer
      buttons use, landing on the Connecting/Error screens instead of
      exiting the process on failure (a behavior change from before, judged
      strictly better: a bad `--server` used to hard-exit).
- [ ] Not covered by an automated test (no windowed GL context in CI/tests,
      same limitation as `ChunkRenderer`) — verified by: a clean
      `-DVB_WARNINGS_AS_ERRORS=ON` build across `voxel_browser`/
      `voxel_browser_server`/`vb_tests`, the full `ctest` suite green
      (`vb_tests` unaffected — nothing in `vb_core`/`vb_render`'s public
      surface changed shape besides the additive `MainMenu`/
      `save_client_config`), and a real windowed launch + screenshot showing
      the main menu laid out correctly (player name / address / port /
      Connect / Play Singleplayer / Settings / Quit). Clicking through
      Settings/Connecting/Error wasn't exercised interactively this session
      (an automated-input attempt via a background PowerShell couldn't
      reliably focus the raylib window to deliver clicks — a host/tooling
      limitation, not a sign of an app bug) — worth a manual pass before
      calling 5.3 fully verified.

### 5.4 Play polish  ✅ done (2026-09-16)

- [x] Day/night `time_of_day` from `JoinAccept`, advanced server-side, simple sky
      gradient client-side: `S2C_TimeOfDay` (46) — `inc/vb/protocol/world.hpp`
      + `src/protocol/world.cpp`, round-trip tested. `kEngineProtocolVersion`
      bumped 9 → 10. `S2C_JoinAccept::time_of_day` existed since Phase 1.3
      but nothing ever advanced it or kept an already-connected client in
      sync — closed both gaps: new pure (no raylib) `vb::world::daynight.hpp`
      + `.cpp` (`kTicksPerDay = 24000`, `advance_time_of_day`,
      `sky_brightness`, `sky_color_for_time`; unit-tested,
      `tests/unit/daynight_test.cpp`), `ServerSession` advances its own
      `time_of_day` every tick (`set_day_length_seconds`, default
      1200s/day == 20 real minutes), fills a joining player's `JoinGrant
      ::time_of_day` from it, and broadcasts `S2C_TimeOfDay` to every playing
      connection about once a second (coarser than snapshots — the clock
      only needs to look smooth). `ClientSession::time_of_day()` returns the
      join-time value until the first update lands, then tracks the latest
      broadcast. `src/client/main.cpp`: `ClearBackground` behind the 3D view
      uses `sky_color_for_time(client->time_of_day())` (a 4-keyframe
      sunrise/noon/sunset/midnight gradient, not a physically based sky),
      plus an "HH:MM" readout added to the existing debug overlay. Tests:
      `tests/unit/protocol_test.cpp` (round-trip),
      `tests/unit/daynight_test.cpp` (rate/wrap/brightness/color math), and a
      new `tests/unit/netcode_test.cpp` case (sped-up day length; asserts a
      joined client's clock advances past its join-time value via the
      periodic broadcast, and a second client joining later gets a later
      `JoinAccept.time_of_day` than the first — proving the grant is
      live-filled per join, not fixed at server startup). Full `ctest` green
      (4/4) on `build-asan-nonet`; clean `-DVB_WARNINGS_AS_ERRORS=ON` build
      of all three targets.
      Not attempted: no ambient-light/mob-spawning gameplay coupling (purely
      cosmetic this pass); no ability for a pack to set/override the day
      length or a specific starting time_of_day (`set_day_length_seconds` is
      C++-only, no `vb.` Lua binding); the sky gradient is a flat
      `ClearBackground` fill, not a real skybox/sun/moon/star render.
- [x] Chat: `C2S_Chat` (100) + `S2C_Chat` (101) — `inc/vb/protocol/chat.hpp` +
      `src/protocol/chat.cpp`, round-trip tested. `kEngineProtocolVersion`
      bumped 7 → 8. `ServerSession::set_chat_handler` (mirrors
      `set_ui_event_handler`'s shape) routes a playing connection's
      `C2S_Chat` to an optional `bool(NetId, string_view)` veto before
      `ServerSession` itself formats `"<name>: <text>"` and broadcasts
      `S2C_Chat` to every playing connection (sender included) —
      `PackRuntime::attach_session` wires it to the already-existing
      `dispatch_chat`/`vb.on("chat", ...)` seam (was captured but never
      reachable — `C2S_Chat` didn't exist yet). No handler set (e.g.
      `--singleplayer`, no `PackRuntime`) = default-allow, chat still works
      without a pack. `ClientSession::send_chat`/`take_chat_messages()`.
      `src/client/main.cpp`: a small HUD chat box (`kPlaying` state only) —
      Enter opens a `GuiTextBox` (plain raygui, no Lua, same posture as
      `MainMenu`) and releases mouse capture like an open pack UI does,
      Enter again sends + closes, Escape cancels; a bottom-left scrolling
      log (last 8 lines) shows incoming `S2C_Chat`. Tests:
      `tests/unit/protocol_test.cpp` (round-trip),
      `tests/unit/netcode_test.cpp` ("chat: a broadcast reaches every
      playing client, including the sender" — also asserts an empty line
      is dropped server-side, not broadcast), and
      `tests/unit/pack_runtime_integration_test.cpp` ("pack script vetoes
      chat from a specific player" — end-to-end over `LoopbackTransport`).
      Not attempted: rate limiting / flood guard, `/`-prefixed commands,
      per-message timestamps, chat history persisted across a reconnect.
- [x] Player list / join-leave messages: `S2C_PlayerJoin` (104) /
      `S2C_PlayerLeave` (105) / `S2C_PlayerList` (106) —
      `inc/vb/protocol/chat.hpp` + `src/protocol/chat.cpp`, round-trip tested.
      `kEngineProtocolVersion` bumped 8 → 9. `ServerSession` generates these
      itself (no Lua involvement, same posture as chat's server-side
      formatting): a freshly-joined connection gets one `S2CPlayerList` of
      everyone else already playing, then every other playing connection
      gets `S2CPlayerJoin`; a disconnect broadcasts `S2CPlayerLeave` to
      everyone remaining. `ClientSession::players()` keeps a live
      `net_id -> name` map from these; join/leave also land as
      `"* <name> joined/left the game"` lines through the existing
      `take_chat_messages()` seam (5.4's chat log), so no second HUD widget
      was needed for that half. `src/client/main.cpp` draws a small
      always-visible player list, top-right (own name highlighted, no toggle
      key -- avoids clashing with Tab, already bound to mouse-capture
      release). Tests: `tests/unit/protocol_test.cpp` (round-trip) and a new
      `tests/unit/netcode_test.cpp` case asserting a newcomer's player list
      contains the existing player, the existing player gets the join
      broadcast + system chat line, and both clear on disconnect. Also fixed
      two pre-existing chat tests that joined two clients simultaneously and
      didn't drain the now-also-arriving join system line before asserting
      on `take_chat_messages()`.
      Not attempted: player list persists no extra metadata (ping, idle
      time); no distinct "system message" channel from real chat (join/leave
      share the same log/take_chat_messages() stream, colour-coded only by
      the "* " prefix).
- [x] Death/respawn (fall out of world, `Health` at 0) with spawn point:
      no new wire message — reuses `S2C_Chat` (no protocol version bump).
      `ServerSession::Conn` gained `spawn_pos` (captured once at join, from
      the same `JoinGrant` that already seeded `move.position`) and `health`
      (`float`, default 20 — matches `ecs::Health`'s default, though the ECS
      component itself remains unused per Phase 3.1's "session drives
      movement directly" deferral; this is plain `ServerSession` state, not
      an EnTT component). New `ServerSession::check_respawns()`, called once
      per tick before `broadcast_snapshots()`: if a player's feet are below
      a configurable `void_kill_y` (`set_void_kill_y`, `ServerConfig::
      void_kill_y` in `server.toml`, default -64.0), `health` is forced to
      0 (instant kill, no partial fall damage this pass); whenever `health
      <= 0` for *any* reason, the player is teleported back to `spawn_pos`,
      `health` reset to 20, their `physics::MoveState` reset (velocity
      zeroed, not just position), `interest_` updated so other players see
      the teleport immediately (not just next input tick), and a private
      `S2C_Chat{"* you died and respawned"}` sent only to that connection
      (not broadcast — reuses the existing chat pipeline/HUD log, same
      "system message via chat" pattern 5.4's join/leave notices already
      established). The `health <= 0` check (not just a direct
      `position.y < void_kill_y` branch) is deliberately generic: nothing
      else decrements health yet (no combat system exists), but any future
      damage source gets working respawn for free by just setting `health`
      to 0. Client-side position correction needs **no new code**: the
      existing prediction/reconciliation pipeline (Phase 3.4) already snaps
      a client to whatever `S2C_EntitySnapshot.local` says next tick, and a
      respawn is just an unusually large snap.
      Tests: a new `tests/unit/netcode_test.cpp` case flies a client
      downward past a `void_kill_y` set 5m below its actual spawn height
      (not a hardcoded absolute Y, so it doesn't depend on what worldgen
      picked for the test seed), asserts the server's authoritative position
      snapped back to spawn height, and asserts the client's chat log
      received `"* you died and respawned"`. `tests/unit/config_test.cpp`
      gained a `void_kill_y` case in its existing TOML-values test. Full
      `ctest` green (4/4) on `build-asan-nonet`; clean
      `-DVB_WARNINGS_AS_ERRORS=ON` build of all three targets.
      Not attempted: no fall damage for a *survivable* fall (only the void
      threshold kills — no minimum-safe-fall-height/damage curve); no death
      message broadcast to *other* players (only the dying player sees the
      notice, matching how the feature is scoped as "with spawn point", not
      a killfeed); `--singleplayer`'s `IntegratedGame` doesn't call
      `set_void_kill_y` explicitly so it just gets `ServerSession`'s
      built-in -64.0 default, untested that this is a sensible number for
      every worldgen seed's actual terrain floor.
- [x] Basic sfx hooks are stubbed (no audio subsystem in v0) — documented in
      `docs/lua-api.md`'s new "Audio / sfx — not implemented" section: no
      `vb.`/`ui.` sound API exists, raylib is built with
      `SUPPORT_MODULE_RAUDIO OFF` (`cmake/Dependencies.cmake`), and
      `ARCHITECTURE_SPEC.md` §10.5's "sfx trigger" mention in the
      block-break event-flow diagram was always illustrative, not a real
      hook. Cross-referenced to this doc's own "Deferred" §'s "Audio
      subsystem + Lua sfx/music API" line, which was already tracking this
      — no code changed, documentation only.

**5.4 status (2026-09-16): all four items landed.** Chat, player list/
join-leave, day/night, and death/respawn — one focused commit each, in that
order, over a single session. None needed cross-cutting changes to the
others; chat's send-to-one-connection pattern got reused as-is by
death/respawn's private notice, and player list's "reuse the chat log for
system messages" precedent is exactly what death/respawn's notice does too.
Everything is verified by unit/integration tests over `LoopbackTransport`
plus a clean full build; none of the four had a live two-window manual
playtest this session (see each entry's own "not re-verified" note) — worth
one before calling Phase 5 itself done.

### 5.5 Documentation  ✅ (2026-09-16)

- [x] `docs/lua-api.md`: refreshed to match reality (2026-09-16) — dropped
      the stale "no base pack yet" header, fixed several outdated claims
      (chat/`send_message`/`open_ui` *are* handled client-side now; `chat`
      fires from a real `C2S_Chat`), documented `vb.world.spawn_item_drop`
      and `player:take()`, and added a "Worked example — content/base"
      table mapping every file in the pack to the API it demonstrates.
      Example-driven per §10.3–10.4 via that table rather than inline
      code samples for every call — `content/base` itself is the running
      example.
- [x] `docs/protocol.md`: already kept in lockstep with every version bump
      throughout Phases 1–5 (every entry in this backlog that touched a
      wire message updated it in the same commit); confirmed current
      (`kEngineProtocolVersion` 11) while working on this item, no drift
      found.
- [x] README "Getting Started" (2026-09-16): replaced the stale, purely
      forward-looking "Implementation Strategy" phase plan with an
      accurate "Project Status" table (Phase 0–5 done/substantially done,
      linking to this file for the real backlog); corrected the Tech Stack
      table to distinguish what's actually driving the game today from
      still-gated future backends (Cellulose/librg/EnTT/FastNoise2, each
      behind its own `VB_WITH_*` flag); added a real "try it now"
      `--singleplayer` quick-start (which prompted actually fixing
      `--singleplayer` to run the real content pack — see 4.3/5.1's
      "`--singleplayer` now runs the real content pack" entry above).
      Build steps + `VB_WITH_NET`'s protobuf system deps were already
      present from Phase 0/1.2 and needed no further work.
- [x] `content/base` as tutorial pack: every file already carries
      "why, not just what" comments (established well before this pass —
      see e.g. `blocks/dirt.lua`'s explanation of `add_or_get` idempotency,
      or `crafting.lua`'s explanation of why crafting logic lives in
      content, not the engine); confirmed still true while writing the
      `docs/lua-api.md` worked-example table above, no gaps found.
- [x] `CONTRIBUTING.md` (new, 2026-09-16): module map (every `src/`/`inc/vb/`
      subdirectory → target → purpose), build/test workflow tips beyond
      what's in the README (multiple build trees for `VB_WITH_LUA` on/off,
      doctest's glob-not-substring `--test-case` filter, which smoke tests
      need which `VB_BUILD_*` flags), code style (`.clang-format`, no
      exceptions on engine hot paths, `vb::<module>` namespacing), a
      step-by-step for adding a new wire message (struct → `MessageType` →
      round-trip test → version bump → `docs/protocol.md` entry →
      integration test), and a "generic primitive, not a game-specific
      one" guideline for new Lua bindings using this session's crafting
      work as the worked example.

**Phase 5 exit:** build from source on all 3 platforms; run a server with the
base pack; two players connect, mine and place blocks, see each other, chat, and
open the inventory UI. Nothing gameplay-facing is hardcoded in C++.
**Status:** met over `LoopbackTransport`/single-process verification
throughout this backlog (see each phase's own status paragraph); the literal
"two players, two real windows" manual playtest across all 3 platforms has
not been run by a human yet — every session so far has verified through
automated tests + `--headless` runs (no GL context in this environment). Real
multiplayer over `GnsTransport` itself *is* covered by a real-UDP unit test
(`gns_transport_test.cpp`) and real client/server processes were smoke-tested
manually earlier in the project (see `STATE.md`'s Phase 1 notes) — what
hasn't specifically been re-verified live is the *combination*: two real
windows, chatting, crafting, and seeing each other, all at once.

