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
-- **Known gaps, not attempted here:** (1) item ids are raw block ids today
-- (`vb.register_item` never allocates its own id space -- see blocks/*.lua's
-- on_break, which gives back the broken block's own id), so this renders
-- numeric ids, not item names, until that's resolved. (2) the item-grid
-- widget the spec describes for this screen doesn't exist (`ui_runtime.cpp`'s
-- `WidgetType` has no grid variant) -- a `list` stands in.
ui.define("base:inventory", function(state)
	local items = {}
	local slots = state and state.slots or {}
	for _, slot in ipairs(slots) do
		table.insert(items, "item " .. tostring(slot.item) .. " x" .. tostring(slot.count))
	end
	if #items == 0 then
		items = { "(empty)" }
	end

	local title = "Inventory"
	if state.selected then
		title = title .. " (#" .. tostring(state.selected) .. ")"
	end

	return {
		widgets = {
			{
				id = "title",
				type = "label",
				x = 24,
				y = 24,
				w = 200,
				h = 28,
				text = title,
			},
			{
				id = "slots",
				type = "list",
				x = 24,
				y = 60,
				w = 220,
				h = 160,
				items = items,
				on_change = function(idx)
					state.selected = idx
				end,
			},
			{
				id = "close",
				type = "button",
				x = 24,
				y = 232,
				w = 140,
				h = 32,
				text = "Close",
				on_click = function()
					ui.close()
				end,
			},
		},
	}
end)
