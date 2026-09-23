-- base:pause -- a Lua-defined pause screen (spec §16/§10.4/§6.2).
--
-- Opened via the Escape key: `content/base/keybinds.lua` registers
-- "base:pause" and calls `player:open_ui("base:pause", {})` on its rising
-- edge (Phase 7.4) -- `src/client/main.cpp`'s `kCustomKeybinds` table binds
-- that name to `KEY_ESCAPE` client-side.
--
-- `render(state)` (Phase 6.2) is called once per UI frame while this screen
-- is open; this screen has no local state to demonstrate, so `state` is
-- unused here.
ui.define("base:pause", function(state)
	return {
		widgets = {
			{
				id = "title",
				type = "label",
				x = 24,
				y = 24,
				w = 200,
				h = 28,
				text = "Paused",
			},
			{
				id = "resume",
				type = "button",
				x = 24,
				y = 64,
				w = 140,
				h = 32,
				text = "Resume",
				on_click = function()
					ui.close()
				end,
			},
		},
	}
end)
