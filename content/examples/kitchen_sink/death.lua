-- REMAINING_TASKS.md 6.15's "a `player_death` handler with custom
-- respawn/drop behavior". Fires once per respawn -- health reaching 0, any
-- cause, void-kill included (Phase 6.6) -- as `function(player, cause,
-- health_before) -> table?`; the *first* handler that returns a table wins
-- (not a veto chain). No handler at all falls back to the engine's own
-- built-in behavior (full heal, teleport to join spawn, a generic message)
-- -- this pack always returns a table, so that fallback never actually
-- triggers here, but it's what every other pack in this repo (content/base
-- included) still relies on.
vb.on("player_death", function(player, cause, health_before)
	-- `player:get_pos()` inside this handler still reads the *death*
	-- position (docs/lua-api.md: "the engine hasn't moved the player yet at
	-- this point") -- useful for logging/drops even though `drop_inventory`
	-- below already handles the actual item drop.
	local death_pos = player:get_pos()
	print(string.format(
		"[kitchen_sink] %s died at (%.1f, %.1f, %.1f) from '%s' (health was %.1f)",
		player:get_name(), death_pos.x, death_pos.y, death_pos.z, cause, health_before))

	return {
		heal = 20.0, -- custom respawn health, not necessarily "full"
		message = "You died from '" .. cause .. "' -- your items are where you fell.",
		-- Spawns every inventory slot as a real dropped-item entity at the
		-- *death* position and empties the inventory -- distinct from a
		-- "keep your items" pack, which would simply omit this field.
		drop_inventory = true,
		-- `pos` omitted: falls back to the engine's own join-spawn teleport
		-- (`ServerSession::spawn_point`) rather than this handler inventing
		-- a different respawn point.
	}
end)
