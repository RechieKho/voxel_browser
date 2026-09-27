-- base:hud -- the always-on client HUD (Phase 6.16).
--
-- "Engine provides raw state, Lua deals with presentation": the engine still
-- owns the actual hold-to-break *timing* (input, reach, target tracking --
-- that's gameplay logic, and predicting it locally matters for
-- responsiveness), but it has no concept of a "progress bar" at all -- the
-- only primitive it offers is `type = "rect"`, a plain filled/outlined
-- rectangle with no baked-in meaning. Everything about *this* being a
-- breaking-progress indicator -- its position, size, colors, and the fact
-- that it's two rectangles (a background/border, and a fill sized by the
-- fraction) rather than one -- is decided entirely here, not by the engine.
--
-- `client.break_progress()` (src/script/ui_runtime.cpp) returns nil when
-- nothing is being broken, or a 0..1 fraction while it is.
--
-- `ui.define_hud` (distinct from `ui.define` above) registers the one
-- always-on overlay: unlike a modal screen, it's never open()/close()'d --
-- it's just always evaluated, every UI frame, on top of (or under) whatever
-- modal screen ui.define/open_ui might also have up.
--
-- `client.screen_size()` is the other piece of raw state a HUD typically
-- needs: widgets take absolute pixel positions like every other UiRuntime
-- widget, so centering something requires knowing the real window size
-- rather than guessing a fixed resolution.
-- Player list (spec §5.4), chat log (spec §5.4), and hotbar (spec §5.1) used
-- to be drawn directly by src/client/main.cpp; they're plain `rect`/`text`
-- widgets here now (Phase 6.16 follow-up), same reasoning as the break-
-- progress bar above -- the engine only ever exposes the raw state
-- (`client.players()`, `client.chat_log()`, `client.inventory()`, ...) and a
-- `type = "text"` primitive (a raw, colored, alignable text draw -- distinct
-- from `type = "label"`, which goes through raygui's un-colorable
-- `GuiLabel`), never a "this is a player list" concept of its own.
--
-- `align = "right"`/`"center"` matter here specifically because a HUD can't
-- pre-measure a string's own rendered pixel width the way the engine's
-- raylib-linked render layer can -- see `TextAlign`'s own comment in
-- inc/vb/script/ui_runtime.hpp. `x` is the anchor edge/point `align` is
-- relative to, not always the left edge.

local function push_player_list(widgets, screen)
	local y = 12
	local line_h = 18
	local others = client.players()
	table.insert(widgets, {
		id = "player_list_header",
		type = "text",
		x = screen.width - 12,
		y = y,
		align = "right",
		text = string.format("players (%d)", #others + 1),
		color = { 200, 200, 210, 230 },
	})
	y = y + line_h

	local own_name = client.player_name()
	if own_name ~= "" then
		table.insert(widgets, {
			id = "player_list_self",
			type = "text",
			x = screen.width - 12,
			y = y,
			align = "right",
			text = own_name,
			color = { 170, 220, 170, 230 },
		})
		y = y + line_h
	end

	for i, name in ipairs(others) do
		table.insert(widgets, {
			id = "player_list_" .. i,
			type = "text",
			x = screen.width - 12,
			y = y,
			align = "right",
			text = name,
			color = { 200, 200, 210, 230 },
		})
		y = y + line_h
	end
end

local function push_chat_log(widgets, screen)
	local line_h = 18
	local log = client.chat_log()
	local box_bottom = screen.height - 16
	local y = box_bottom - (client.chat_open() and (line_h + 8) or 0) -
			#log * line_h
	for i, line in ipairs(log) do
		table.insert(widgets, {
			id = "chat_log_" .. i,
			type = "text",
			x = 12,
			y = y,
			text = line,
			color = { 220, 220, 220, 230 },
		})
		y = y + line_h
	end
end

local function push_hotbar(widgets, screen)
	local inv = client.inventory()
	if #inv == 0 then
		return
	end
	local slot_w, slot_h, gap = 96, 40, 6
	local total_w = #inv * (slot_w + gap) - gap
	local x = (screen.width - total_w) / 2
	local y = screen.height - slot_h - 16
	local selected = client.selected_slot()

	for i, slot in ipairs(inv) do
		table.insert(widgets, {
			id = "hotbar_bg_" .. i,
			type = "rect",
			x = x, y = y, w = slot_w, h = slot_h,
			color = { 30, 30, 34, 200 },
			border = (i == selected) and { 230, 220, 120, 255 } or { 90, 90, 100, 230 },
		})
		table.insert(widgets, {
			id = "hotbar_label_" .. i,
			type = "text",
			x = x + 6,
			y = y + 12,
			font_size = 14,
			text = string.format("%s x%d", slot.name, slot.count),
			color = { 220, 220, 220, 230 },
		})
		x = x + slot_w + gap
	end
end

ui.define_hud(function(state)
	local widgets = {}
	local screen = client.screen_size()

	push_player_list(widgets, screen)
	push_chat_log(widgets, screen)
	push_hotbar(widgets, screen)

	local progress = client.break_progress()
	if progress then
		local screen = client.screen_size()
		local bar_w = 120
		local bar_h = 10
		local x = (screen.width - bar_w) / 2
		local y = screen.height / 2 + 24

		-- Background + border, full width.
		table.insert(widgets, {
			id = "break_progress_bg",
			type = "rect",
			x = x,
			y = y,
			w = bar_w,
			h = bar_h,
			color = { 30, 30, 34, 200 },
			border = { 90, 90, 100, 230 },
		})
		-- Fill, sized by the raw fraction -- this is the only place the
		-- 0..1 value from the engine actually turns into a pixel width.
		table.insert(widgets, {
			id = "break_progress_fill",
			type = "rect",
			x = x,
			y = y,
			w = bar_w * progress,
			h = bar_h,
			color = { 220, 220, 220, 230 },
		})
	end

	return { widgets = widgets }
end)
