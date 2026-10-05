-- content/base/death.lua -- shows ui/death.lua's "you died" notice.
--
-- Returns nothing, so the engine's own respawn behavior (full heal, teleport
-- to the join spawn, its chat message) is unchanged -- this handler only adds
-- presentation. (`player_death` is a decision hook: the first handler that
-- returns a table wins, and none returning one falls back to the default.)
vb.on("player_death", function(player, cause, health_before)
	player:open_ui("base:death", { cause = cause })
end)
