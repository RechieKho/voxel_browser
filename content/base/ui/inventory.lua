-- base:inventory -- a Lua-defined inventory screen (spec §16/§10.4/§6.2).
--
-- Reads `state.slots` (a list of `{item, count}`), the exact shape
-- `entity:get_inventory()` (src/script/pack_runtime.cpp's `PlayerHandle::
-- get_inventory`) already returns server-side -- a script opening this with
-- `player:open_ui("base:inventory", { slots = player:get_inventory() })`
-- shows real, if snapshot-only (not live-updating), inventory contents.
--
-- `render(state)` is called once per UI frame while this screen stays open.
-- The panel, backdrop and grid are all positioned from `client.screen_size()`
-- (the same raw state hud.lua uses), so the screen stays centered at any
-- window size. The currently selected hotbar slot gets the same yellow
-- border here as in the hotbar (`client.selected_slot()`, 1-based).
--
-- Opened via the E key: `content/base/keybinds.lua` registers
-- "base:inventory" and calls `player:open_ui("base:inventory", { slots =
-- player:get_inventory() })` on its rising edge (Phase 7.4) --
-- `src/client/main.cpp`'s `kCustomKeybinds` table binds that name to
-- `KEY_E` client-side.
--
-- **Known gap, not attempted here:** item ids are raw block ids today
-- (`vb.register_item` never allocates its own id space -- see blocks/*.lua's
-- on_break, which gives back the broken block's own id), so each slot's
-- count label is the only text; there's no item *name* to show yet.
--
-- The item grid itself is composed from the engine's generic `icon`
-- primitive (draws one registered block/item id's real atlas texture at
-- x/y/w/h, nothing else -- REMAINING_TASKS.md's old "item grid widget for
-- UiRuntime" gap) plus `rect` for the slot background/border and `text` for
-- the count -- the same "compose it in Lua" posture Phase 6.16 already gave
-- the hold-to-break progress bar out of `rect`. An empty slot (item == 0,
-- i.e. air) draws just the background, no icon/count.
local kSlotSize = 40
local kSlotGap = 4
local kCols = 8
local kPad = 16
local kTitleH = 28
local kSectionGap = 8
local kButtonW = 140
local kButtonH = 32

ui.define("base:inventory", function(state)
	local slots = state and state.slots or {}
	local screen = client.screen_size()
	local selected = client.selected_slot()

	local rows = math.max(1, math.ceil(#slots / kCols))
	local grid_w = kCols * kSlotSize + (kCols - 1) * kSlotGap
	local grid_h = rows * kSlotSize + (rows - 1) * kSlotGap
	local panel_w = grid_w + 2 * kPad
	local panel_h = kPad + kTitleH + kSectionGap + grid_h + kSectionGap + kButtonH + kPad
	local panel_x = math.floor((screen.width - panel_w) / 2)
	local panel_y = math.floor((screen.height - panel_h) / 2)
	local grid_x = panel_x + kPad
	local grid_y = panel_y + kPad + kTitleH + kSectionGap

	local widgets = {
		{
			id = "backdrop",
			type = "rect",
			x = 0,
			y = 0,
			w = screen.width,
			h = screen.height,
			color = { 0, 0, 0, 120 },
		},
		{
			id = "panel",
			type = "rect",
			x = panel_x,
			y = panel_y,
			w = panel_w,
			h = panel_h,
			color = { 24, 24, 30, 240 },
			border = { 110, 110, 125, 255 },
		},
		{
			id = "title",
			type = "text",
			x = panel_x + math.floor(panel_w / 2),
			y = panel_y + kPad,
			align = "center",
			text = "Inventory",
			font_size = 20,
			color = { 235, 235, 245, 255 },
		},
	}

	for i, slot in ipairs(slots) do
		local col = (i - 1) % kCols
		local row = math.floor((i - 1) / kCols)
		local x = grid_x + col * (kSlotSize + kSlotGap)
		local y = grid_y + row * (kSlotSize + kSlotGap)

		table.insert(widgets, {
			id = "slot_bg_" .. i,
			type = "rect",
			x = x,
			y = y,
			w = kSlotSize,
			h = kSlotSize,
			color = { 40, 40, 46, 220 },
			border = i == selected and { 255, 220, 80, 255 } or { 90, 90, 100, 230 },
		})

		if slot.item and slot.item ~= 0 then
			table.insert(widgets, {
				id = "slot_icon_" .. i,
				type = "icon",
				x = x + 2,
				y = y + 2,
				w = kSlotSize - 4,
				h = kSlotSize - 4,
				item = slot.item,
			})
			table.insert(widgets, {
				id = "slot_count_" .. i,
				type = "text",
				x = x + kSlotSize - 4,
				y = y + kSlotSize - 14,
				text = tostring(slot.count),
				font_size = 12,
				align = "right",
				color = { 255, 255, 255, 255 },
			})
		end
	end

	if #slots == 0 then
		table.insert(widgets, {
			id = "empty",
			type = "text",
			x = panel_x + math.floor(panel_w / 2),
			y = grid_y,
			align = "center",
			text = "(empty)",
			color = { 170, 170, 180, 255 },
		})
	end

	table.insert(widgets, {
		id = "close",
		type = "button",
		x = panel_x + math.floor((panel_w - kButtonW) / 2),
		y = grid_y + grid_h + kSectionGap,
		w = kButtonW,
		h = kButtonH,
		text = "Close",
		on_click = function()
			ui.close()
		end,
	})

	return { widgets = widgets }
end)
