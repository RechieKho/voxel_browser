-- init.lua runs last, in the server pack VM (`vb` is available; `ui`/`client` are not).
-- API reference: .vb/lua/*.lua, or `vb docs lua-reference/README.md`.

vb.on("player_join", function(name, login)
	print(name .. " joined {{name}}")
end)

-- Type `!hello` in chat. Returning false hides the line from other players.
vb.on("chat", function(player, text)
	if text == "!hello" then
		player:send_message("hello from {{name}}, " .. player:get_name() .. "!")
		return false
	end
end)
