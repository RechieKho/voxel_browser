-- base:inventory -- a Lua-defined inventory screen (spec §16/§10.4/§6.2).
--
-- Reads `state.slots` (a list of `{item, count}`), the exact shape
-- `entity:get_inventory()` (src/script/pack_runtime.cpp's `PlayerHandle::
-- get_inventory`) already returns server-side -- a script opening this with
-- `player:open_ui("base:inventory", { slots = player:get_inventory() })`
-- shows real, if snapshot-only (not live-updating), inventory contents.
--
-- `state` is the same Lua table for every frame this screen stays open
-- (Phase 6.2: `render(state)` is now called once per UI frame, not once at
-- open time), so `state.selected` set by the list's `on_change` below is
-- purely local/cosmetic -- no `ui.send_event` -- and just shows up in the
-- title on the very next frame.
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

ui.define("base:inventory", function(state)
	local slots = state and state.slots or {}

	local widgets = {
		{
			id = "title",
			type = "label",
			x = 24,
			y = 24,
			w = 200,
			h = 28,
			text = "Inventory",
		},
	}

	for i, slot in ipairs(slots) do
		local col = (i - 1) % kCols
		local row = math.floor((i - 1) / kCols)
		local x = 24 + col * (kSlotSize + kSlotGap)
		local y = 60 + row * (kSlotSize + kSlotGap)

		table.insert(widgets, {
			id = "slot_bg_" .. i,
			type = "rect",
			x = x,
			y = y,
			w = kSlotSize,
			h = kSlotSize,
			color = { 40, 40, 46, 220 },
			border = { 90, 90, 100, 230 },
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
			type = "label",
			x = 24,
			y = 60,
			w = 220,
			h = 24,
			text = "(empty)",
		})
	end

	local rows = math.max(1, math.ceil(#slots / kCols))
	local close_y = 60 + rows * (kSlotSize + kSlotGap) + 8

	table.insert(widgets, {
		id = "close",
		type = "button",
		x = 24,
		y = close_y,
		w = 140,
		h = 32,
		text = "Close",
		on_click = function()
			ui.close()
		end,
	})

	return { widgets = widgets }
end)
