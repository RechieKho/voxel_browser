-- REMAINING_TASKS.md 6.15's "`vb.db`/`vb.crypto.hash` used for a small
-- persistent counter" and "a `max_damage` block with a `block_break_tick`
-- handler" bullets.

-- `vb.db`/`vb.crypto` (Phase 6.4, docs/lua-api.md's "Generic per-key
-- storage") -- distinct from `vb.storage` (init.lua's boot counter): `key`
-- is whatever the script picks, and reads/writes hit disk immediately (no
-- dirty-flag/flush step). `vb.crypto.hash` is SHA-256 hex, offered so a
-- pack doesn't have to roll its own hashing in sandboxed Lua (no `os`/`io`).
-- Here: a per-player break counter, keyed by a hash of the player's name
-- rather than the raw name -- not real security (a name isn't a secret),
-- just demonstrating the shape a pack's own login/session system
-- (docs/lua-api.md: "the engine has no notion of 'logged in'") would
-- actually use `vb.crypto.hash` for (hashing a token/password, not a name).
local function player_break_count_key(name)
	return "kitchen_sink:breaks:" .. vb.crypto.hash(name)
end

-- Shared block-damage breaking (Phase 6.5, spec §10.7) -- fires once per
-- tick per player currently holding on a `max_damage > 0` target (only
-- kitchen_sink:unstable_ore here, blocks/unstable_ore.lua). No handler at
-- all means damage never accrues (docs/lua-api.md), so this is the piece
-- that actually makes that block breakable via the hold-to-break system
-- instead of just sitting there at max_damage forever.
vb.on("block_break_tick", function(player, pos, max_damage)
	local name = player:get_name()
	local key = player_break_count_key(name)
	local count = (vb.db.get(key) or 0) + 1
	vb.db.set(key, count)
	if count % 10 == 0 then
		player:send_message(string.format(
			"[kitchen_sink] %s has chipped away at unstable ore %d times total",
			name, count))
	end
	-- One damage point per tick this handler is called -- max_damage=6 means
	-- ~6 ticks of continuous holding breaks it (docs/lua-api.md: "multiple
	-- registered handlers for the same call also sum", so this stays the
	-- only source of damage for this pack).
	return 1.0
end)
