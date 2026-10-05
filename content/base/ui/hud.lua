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
	local line_h = 22
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
	local line_h = 22
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
			color = base_ui.text,
		})
		y = y + line_h
	end
end

-- The hotbar is the first kHotbarSlots inventory slots -- the ones the
-- number keys 1-9 can select (src/client/client_app.cpp) -- as square icon
-- slots on a shared container.
local kHotbarSlots = 9

-- Everything below is authored for a 1280x720 window and multiplied by this,
-- so the HUD keeps its proportions on small and large viewports alike.
local kRefWidth, kRefHeight = 1280, 720
local function ui_scale(screen)
	local s = math.min(screen.width / kRefWidth, screen.height / kRefHeight)
	return math.min(math.max(s, 1), 4)
end

-- Shared by the hotbar and the status bars that sit on top of it. `x/y/w`
-- describe the row of slots, not the container behind them; the rest are the
-- scaled metrics the other HUD pieces size themselves by.
local function hotbar_layout(screen)
	local s = ui_scale(screen)
	local slot, gap = math.floor(64 * s), math.floor(8 * s)
	local margin, pad = math.floor(20 * s), math.floor(8 * s)
	local n = math.min(math.max(#client.inventory(), 1), kHotbarSlots)
	local total_w = n * (slot + gap) - gap
	-- Full-row width at kHotbarSlots, regardless of how many slots are
	-- actually filled -- the status bars anchor to this instead of `w` so a
	-- nearly-empty inventory doesn't squeeze their "cur / max" text together.
	-- Capped to the screen width minus a side margin so it can't run past
	-- the edges on a narrow window.
	local side_margin = math.floor(16 * s)
	local full_w = math.min(kHotbarSlots * (slot + gap) - gap,
			screen.width - 2 * side_margin)
	return {
		scale = s,
		slot = slot,
		gap = gap,
		pad = pad,
		x = math.floor((screen.width - total_w) / 2),
		y = screen.height - slot - margin - pad,
		w = total_w,
		full_x = math.floor((screen.width - full_w) / 2),
		full_w = full_w,
	}
end

-- One horizontal bar: dark background + border, fill sized by current/max,
-- and a centered "cur / max" readout. `fill` is the fill color.
local function push_bar(widgets, id, x, y, w, h, font, current, max, fill)
	local frac = max > 0 and math.min(math.max(current / max, 0), 1) or 0
	table.insert(widgets, {
		id = id .. "_bg",
		type = "rect",
		x = x, y = y, w = w, h = h,
		color = base_ui.bar_bg,
		border = base_ui.bar_border,
	})
	if frac > 0 then
		table.insert(widgets, {
			id = id .. "_fill",
			type = "rect",
			x = x + 1, y = y + 1,
			w = (w - 2) * frac, h = h - 2,
			color = fill,
		})
	end
	table.insert(widgets, {
		id = id .. "_text",
		type = "text",
		x = x + w / 2,
		y = y + (h - font) / 2,
		align = "center",
		font_size = font,
		text = string.format("%d / %d", math.ceil(current), math.ceil(max)),
		color = base_ui.text_bright,
	})
end

-- Health (left) and hunger (right) bars just above the hotbar, each taking
-- half its width. Nothing is drawn until the server's first status arrives.
local function push_status_bars(widgets, screen)
	local health, hunger = client.health(), client.hunger()
	if not health or not hunger then
		return
	end
	local bar = hotbar_layout(screen)
	local s = bar.scale
	local gap, h = math.floor(14 * s), math.floor(28 * s)
	local font = math.floor(18 * s)
	-- Full hotbar-row width, not the current (possibly few-slot) width, so
	-- the bars stay wide enough for their text no matter the inventory size.
	local w = (bar.full_w - gap) / 2
	local y = bar.y - bar.pad - h - math.floor(6 * s)

	-- Green -> yellow -> red as health drops.
	local frac = health.max > 0 and health.current / health.max or 0
	local fill = { 80, 200, 90, 240 }
	if frac <= 0.25 then
		fill = { 220, 60, 60, 240 }
	elseif frac <= 0.5 then
		fill = { 230, 190, 60, 240 }
	end
	push_bar(widgets, "health", bar.full_x, y, w, h, font, health.current, health.max, fill)
	push_bar(widgets, "hunger", bar.full_x + w + gap, y, w, h, font,
			hunger.current, hunger.max, { 200, 140, 70, 240 })
end

local function push_hotbar(widgets, screen)
	local inv = client.inventory()
	if #inv == 0 then
		return
	end
	local layout = hotbar_layout(screen)
	local selected = client.selected_slot()
	local n = math.min(#inv, kHotbarSlots)
	local slot_size, pad = layout.slot, layout.pad
	local inset = math.floor(4 * layout.scale)
	local font = math.floor(16 * layout.scale)

	table.insert(widgets, {
		id = "hotbar_container",
		type = "rect",
		x = layout.x - pad,
		y = layout.y - pad,
		w = layout.w + 2 * pad,
		h = slot_size + 2 * pad,
		color = base_ui.container_bg,
		border = base_ui.container_border,
	})

	for i = 1, n do
		local slot = inv[i]
		local x = layout.x + (i - 1) * (slot_size + layout.gap)
		local y = layout.y
		table.insert(widgets, {
			id = "hotbar_bg_" .. i,
			type = "rect",
			x = x, y = y, w = slot_size, h = slot_size,
			color = base_ui.slot_bg,
			border = (i == selected) and base_ui.slot_selected or base_ui.slot_border,
		})
		if slot.item ~= 0 and slot.count > 0 then
			table.insert(widgets, {
				id = "hotbar_icon_" .. i,
				type = "icon",
				x = x + inset, y = y + inset, w = slot_size - 2 * inset, h = slot_size - 2 * inset,
				item = slot.item,
			})
			table.insert(widgets, {
				id = "hotbar_count_" .. i,
				type = "text",
				x = x + slot_size - inset - 1,
				y = y + slot_size - font - inset,
				align = "right",
				font_size = font,
				text = tostring(slot.count),
				color = base_ui.text_bright,
			})
		end
	end
end

-- The selected item's name, shown above the status bars for a moment when the
-- selection (or the item in the selected slot) changes, fading over the last
-- part. `state` is the HUD's persistent table. The first frame only records
-- what's selected, so joining doesn't flash a label.
local kNameSeconds = 2.0
local kNameFadeSeconds = 0.5

local function push_item_name(widgets, screen, state)
	local inv = client.inventory()
	local selected = client.selected_slot()
	local slot = inv[selected]
	local item = slot and slot.item or 0
	local key = selected .. ":" .. item
	local now = client.time()

	if state.name_key ~= nil and state.name_key ~= key then
		state.name_until = (item ~= 0) and (now + kNameSeconds) or nil
	end
	state.name_key = key

	if item == 0 or not state.name_until or now >= state.name_until then
		return
	end
	local remaining = state.name_until - now
	local alpha = 255
	if remaining < kNameFadeSeconds then
		alpha = math.floor(255 * remaining / kNameFadeSeconds)
	end
	local bar = hotbar_layout(screen)
	-- Above the status bars (28px tall, 8px gap, over the container padding).
	local s = bar.scale
	local y = bar.y - bar.pad - math.floor((28 + 8 + 30) * s)
	table.insert(widgets, {
		id = "item_name",
		type = "text",
		x = math.floor(screen.width / 2),
		y = y,
		align = "center",
		font_size = math.floor(20 * s),
		text = base_ui.pretty_name(slot.name),
		color = { 255, 255, 255, alpha },
	})
end

-- A small "+" at screen center; the block-highlight outline alone isn't
-- enough to aim by.
local function push_crosshair(widgets, screen)
	local cx, cy = math.floor(screen.width / 2), math.floor(screen.height / 2)
	local s = ui_scale(screen)
	local arm, thick = math.floor(9 * s), math.max(2, math.floor(3 * s))
	local color = { 255, 255, 255, 200 }
	table.insert(widgets, {
		id = "crosshair_h",
		type = "rect",
		x = cx - arm, y = cy - thick / 2, w = arm * 2, h = thick,
		color = color,
	})
	table.insert(widgets, {
		id = "crosshair_v",
		type = "rect",
		x = cx - thick / 2, y = cy - arm, w = thick, h = arm * 2,
		color = color,
	})
end

-- Brief red full-screen tint when health drops. `state` is the HUD's own
-- persistent table, so the previous health and the flash deadline live there;
-- `client.time()` drives the fade. A heal never flashes, and neither does the
-- first status after joining (there's no previous value yet).
local kFlashSeconds = 0.35
local kFlashMaxAlpha = 110

local function push_damage_flash(widgets, screen, state)
	local health = client.health()
	local now = client.time()
	if health then
		if state.last_health and health.current < state.last_health - 0.001 then
			state.flash_until = now + kFlashSeconds
		end
		state.last_health = health.current
	end
	if state.flash_until and now < state.flash_until then
		local alpha = math.floor(kFlashMaxAlpha * (state.flash_until - now) / kFlashSeconds)
		table.insert(widgets, {
			id = "damage_flash",
			type = "rect",
			x = 0, y = 0, w = screen.width, h = screen.height,
			color = { 200, 30, 30, alpha },
		})
	end
end

ui.define_hud(function(state)
	local widgets = {}
	local screen = client.screen_size()

	-- First, so everything else draws over the tint.
	push_damage_flash(widgets, screen, state)
	push_crosshair(widgets, screen)

	push_player_list(widgets, screen)
	push_chat_log(widgets, screen)
	push_hotbar(widgets, screen)
	push_status_bars(widgets, screen)
	push_item_name(widgets, screen, state)

	local progress = client.break_progress()
	if progress then
		local s = ui_scale(screen)
		local bar_w = math.floor(160 * s)
		local bar_h = math.floor(14 * s)
		local x = (screen.width - bar_w) / 2
		local y = screen.height / 2 + math.floor(32 * s)

		-- Background + border, full width.
		table.insert(widgets, {
			id = "break_progress_bg",
			type = "rect",
			x = x,
			y = y,
			w = bar_w,
			h = bar_h,
			color = base_ui.bar_bg,
			border = base_ui.bar_border,
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
			color = base_ui.text,
		})
	end

	return { widgets = widgets }
end)
