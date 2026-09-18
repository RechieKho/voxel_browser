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
ui.define_hud(function(state)
	local widgets = {}

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
