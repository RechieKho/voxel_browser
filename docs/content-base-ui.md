# `content/base` UI/UX — investigation and phased plan

> Status: **plan only, nothing implemented yet.** Covers the three reported
> problems (no health bar, inventory not centered, inventory has no
> container background) and the related gaps found while looking into them.
> Same layout as `docs/content-base-testing.md`: findings first, then phases
> (U0–U5) with checkbox tasks.

## Findings

### 1. Health bar: Lua alone can't fix it, the client never receives health

- Player health exists only on the server: `ecs::Health{20, 20}` is attached
  at join (`src/net/session.cpp`, join path, next to `ecs::Hunger{100, 100}`).
  It's read through `ServerSession::player_health()` /
  `set_player_health()`, which are server-side only.
- No wire message carries it. `inc/vb/protocol/` has `S2CInventory`,
  `S2CPlayerList`, chat, snapshots and so on, but nothing for the player's
  own health or hunger. `EntityRecord` in `snapshot.hpp` has no health field
  either.
- `UiRuntime`'s `client` table (`src/script/ui_runtime.cpp`, around
  `client_tbl[...]`) offers `break_progress`, `screen_size`, `player_name`,
  `players`, `chat_log`, `chat_open`, `inventory` and `selected_slot`. There
  is no `health()`.
- The engine already expects content to draw this itself:
  `inc/vb/script/ui_runtime.hpp`'s `kRect` comment names "a health bar" as
  something to compose from `rect` in Lua. The drawing primitives exist. What's
  missing is the data reaching the client.
- Related: `PlayerHandle` has no `get_health()` (already listed in
  `content-base-testing.md`'s findings), so server Lua can't read it either.

So a health bar needs **engine work first** (a protocol message, client
state, and a `client.*` accessor), and only then a Lua HUD widget.

### 2. Inventory screen (`content/base/ui/inventory.lua`)

- **Not centered.** The grid origin is hardcoded to `x = 24, y = 60`, and
  the title and Close button are pinned at `x = 24`. It never calls
  `client.screen_size()`, which `hud.lua` already uses to center the hotbar
  and break-progress bar.
- **No container background.** Only each slot gets a `rect`. Nothing sits
  behind the whole grid, so the slots float over the 3D world. There is no
  dim backdrop behind the modal either.
- **Title uses `type = "label"`**, which goes through raygui's `GuiLabel`.
  That can't be colored and doesn't match the HUD's `text` style.
- **The contents are a snapshot, not live.** `keybinds.lua` passes
  `{ slots = player:get_inventory() }` once at open time. Picking up or
  crafting while the screen is open leaves it stale. The client already
  keeps a live copy (`S2CInventory` goes into `client.inventory()`), but that
  accessor returns only `{name, count}`, not the item id that the `icon`
  widget needs.
- **The selected slot isn't shown**, and the header comment still mentions a
  `state.selected` / list `on_change` that the grid version no longer has.

### 3. Hotbar (`content/base/ui/hud.lua`, `push_hotbar`)

- Slots are 96×40 boxes showing the text `"<name> x<count>"`, with no icons.
  The reason is the same: `client.inventory()` has no item id, so `icon`
  can't be used.
- It draws **every** inventory slot, but number keys only select slots 1–9
  (`client_app.cpp`, around the `selected_slot` handling). With more than 9
  slots the bar gets wider than intended and shows slots you can't select.
- Empty slots probably show as `"air x0"` (the registry name for id 0).
  Verify this.
- There's no container behind the bar, unlike the inventory problem above.

### 4. Other UX gaps noticed (lower priority)

- **No crosshair.** Nothing in `src/client` or `src/render` draws one, and
  `hud.lua` doesn't either. The block-highlight outline is the only aiming
  aid. It's a cheap win as two `rect`s in `hud.lua`.
- **The pause screen (`ui/pause.lua`)** has the same problems as the
  inventory: pinned top-left, no panel, no backdrop, raygui `label` title.
- **No hunger indicator.** It's the same data-path problem as health. The
  server ticks hunger (`update_hunger`), but the client never sees it.
- **No damage or death feedback** (no hurt flash, no "you died" screen). It
  depends on health reaching the client.
- Layout constants (colors, padding, slot size) are copied between
  `hud.lua` and `inventory.lua`. There's no shared style table. Real
  `require` doesn't exist yet (`init.lua` header), so sharing needs a global
  defined in a file that loads first, or a duplicated small table.

## Phased plan

Each phase can ship on its own. U1 is pure Lua and can start immediately.
U2 is the engine prerequisite for U3.

### U0 — Verify assumptions (small, do first)

- [ ] Confirm `client.*` is callable from a `ui.define` modal render, not
      just from `ui.define_hud`. They share one Lua state, so it should work,
      but add a test. U1 depends on `client.screen_size()` there.
- [ ] Confirm what an empty inventory slot looks like on the wire and in
      `client.inventory()` (item 0 / name `"air"` / count 0?).
- [ ] Confirm the inventory size (fixed? grows on `give`?) so the grid and
      hotbar can be sized deliberately.

### U1 — Inventory screen layout (Lua only, fixes 2 of the 3 reported issues)

- [ ] `inventory.lua`: compute the grid's total width and height from
      `kCols`/`kSlotSize`/`kSlotGap` plus padding, and center the panel
      with `client.screen_size()`.
- [ ] Add a full-screen semi-transparent backdrop `rect` (for example
      `{0, 0, 0, 120}`) and a panel `rect` with a border behind the title,
      grid and button. Panel first in the widget list so it draws underneath.
- [ ] Swap the title `label` for a centered `text` widget (colorable,
      matches the HUD).
- [ ] Center the Close button in the panel's footer.
- [ ] Highlight the currently selected hotbar slot in the grid (same
      yellow border as the hotbar) using `client.selected_slot()`.
- [ ] Apply the same panel, backdrop and centering to `pause.lua`.
- [ ] Update `content_base_ui_test.cpp`: assert the panel and backdrop
      widgets exist, and that the panel is centered for a given
      `set_screen_size()` (for example 1280×720 and 800×600).
- [ ] Fix the stale header comments in `inventory.lua`.

### U2 — Engine: send player status to the client (prerequisite for health/hunger UI)

- [ ] Protocol: add `S2CPlayerStatus { float health, max_health, hunger,
      max_hunger }` in `inc/vb/protocol/` with encode/decode and a
      `MessageType`. Send only to the owning player, only when a value
      changes (dirty flag in `apply_damage`, `set_player_health`, the
      respawn heal, and `update_hunger`), plus once at join. This follows
      `S2CInventory`'s "full snapshot, no deltas" approach.
- [ ] Client session: store the latest status, and pass it to `UiRuntime`
      each frame next to `set_inventory` in `client_app.cpp`.
- [ ] `UiRuntime`: add `client.health()` returning `{current, max}` and
      `client.hunger()` returning `{current, max}`, or nil before the first
      status arrives.
- [ ] Optional: add `PlayerHandle:get_health()` for server Lua, which closes
      the `content-base-testing.md` finding and lets the fall-damage tests
      run without `VB_WITH_AUTOMATION`.
- [ ] Tests: protocol round-trip unit test, session test that damage
      produces a status message, `UiRuntime` test for the new accessors.
- [ ] Docs: `docs/protocol.md` (new message) and `docs/lua-api.md` (new
      `client.*` functions).
- [ ] Automation (optional): expose health in the automation state, so e2e
      tests can assert on it.

### U3 — Health bar (and hunger) in the HUD (Lua, needs U2)

- [ ] `hud.lua`: add `push_health_bar`. Background, border and fill `rect`s
      above the hotbar, left-aligned to the hotbar's left edge. Fill width
      is `current / max`, and the color shifts from green to yellow to red as
      it drops. Optional `text` reading `"14 / 20"`.
- [ ] Mirror it with a hunger bar right-aligned to the hotbar's right edge.
- [ ] Hide both when `client.health()` returns nil (not joined yet).
- [ ] Tests in `content_base_ui_test.cpp`: the bar exists, the fill width
      scales with the value, and nothing is drawn when status is nil.
- [ ] E2E (optional): take fall damage, then check the bar shrinks, through
      the automation widget cache.

### U4 — Hotbar polish (needs a small engine change)

- [ ] Engine: add `item` (the raw block/item id) to each
      `client.inventory()` entry. `InventorySlotView` gets an id field, and
      `client_app.cpp` already has `slot.item` in hand.
- [ ] `hud.lua`: switch the hotbar to square icon slots (`icon` + count
      `text`, same composition as `inventory.lua`). Cap it at 9 slots to
      match the 1–9 selection keys. Add a container `rect` behind the bar
      and skip drawing empty slots' labels.
- [ ] `inventory.lua`: optionally switch to live `client.inventory()`
      instead of the open-time `state.slots` snapshot, so the screen updates
      while open. Keep `state.slots` as a fallback for packs that pass their
      own.
- [ ] Optionally show the selected item's name as a fading `text` above the
      hotbar when the selection changes.

### U5 — Extra polish (each item independent, optional)

- [ ] Crosshair: two small `rect`s at screen center in `hud.lua`.
- [ ] Shared style table (colors, padding, slot size) defined once in a
      file that loads before `ui/*.lua`, or documented as duplicated until
      `require` exists. Verify client-side load order first.
- [ ] Damage feedback: a brief red full-screen `rect` flash when
      `client.health().current` drops, with the timer kept in HUD-local
      state. Needs U2.
- [ ] Death screen: a `base:death` modal with a centered panel and a
      Respawn button. Depends on how respawn is triggered today; investigate
      `RespawnDecision` and kitchen_sink's `death.lua` first.
- [ ] Hover tooltip with the item name in the inventory grid. Needs mouse
      position exposed to Lua, which isn't available today. Engine change.

## Suggested order

1. **U0 + U1** in one PR. Pure Lua plus tests. Fixes the centering and
   background issues right away.
2. **U2** as its own engine PR. Protocol, client state, accessor, docs.
3. **U3** right after U2. A small Lua PR that adds the health bar.
4. **U4**, then cherry-pick from **U5**.
