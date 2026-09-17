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
-- **Known gaps, not attempted here (same category as ui/pause.lua):**
-- (1) nothing calls `open_ui` for this screen yet -- there's no client
-- gesture or C2S message requesting "open my inventory"
-- (REMAINING_TASKS.md 5.4 tracks chat/interact messages generally; an
-- inventory-open request is the same shape of gap). (2) item ids are raw
-- block ids today (`vb.register_item` never allocates its own id space --
-- see blocks/*.lua's on_break, which gives back the broken block's own id),
-- so this renders numeric ids, not item names, until that's resolved.
-- (3) the item-grid widget the spec describes for this screen doesn't exist
-- (`ui_runtime.cpp`'s `WidgetType` has no grid variant) -- a `list` stands in.
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
