-- base:pause -- a Lua-defined pause screen (spec §16/§10.4/§6.2).
--
-- **Known gap, not attempted here:** nothing in the engine can open this yet.
-- `player:open_ui(name, ctx)` (src/script/pack_runtime.cpp) is the only way
-- a screen ever opens, and it's server-push-only -- there is no client
-- gesture (keybind, menu button) that asks the server to open one, and no
-- client-local "open my own UI" call either (`ui.define` only registers a
-- render function; `vb::script::UiRuntime::open` is called exclusively from
-- `src/client/main.cpp` in response to a `S2C_OpenUi`). REMAINING_TASKS.md
-- 5.3 (main menu / settings) is the natural place a pause-key trigger
-- belongs. Defined here so the layout exists and loads the moment that
-- wiring lands.
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
