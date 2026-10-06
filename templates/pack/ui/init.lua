-- init.lua runs last, in the server pack VM (`vb` is available; `ui`/`client` are not).
-- The screen itself lives in ui/hello.lua, which runs in the client UI VM.

vb.on("player_join", function(name, login)
	print(name .. " joined {{name}}")
end)

-- Type `!hello` in chat to open the screen. The second argument becomes the screen's `state`.
vb.on("chat", function(player, text)
	if text == "!hello" then
		player:open_ui("{{name}}:hello", { name = player:get_name() })
		return false
	end
end)

-- Events sent by the screen with ui.send_event(kind, value) arrive here.
vb.on("ui_event", function(player, ui_name, widget_id, kind, value)
	if kind == "wave" then
		player:send_message("you waved at {{name}}")
	end
end)
