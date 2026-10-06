-- ui/*.lua runs in the client UI VM: `ui` and `client` are available, `vb` is NOT.
-- `render` runs every frame while the screen is open; `state` is the same table every frame.
ui.define("{{name}}:hello", function(state)
	return {
		widgets = {
			{ id = "title", type = "label", x = 24, y = 24, w = 280, h = 28, text = "Hello, " .. (state.name or "?") },
			{
				id = "wave", type = "button", x = 24, y = 64, w = 120, h = 32, text = "Wave",
				on_click = function() ui.send_event("wave", true) end,
			},
			{ id = "close", type = "button", x = 156, y = 64, w = 120, h = 32, text = "Close", on_click = function() ui.close() end },
		},
	}
end)
