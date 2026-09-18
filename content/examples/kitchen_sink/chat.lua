-- REMAINING_TASKS.md 6.15's "a chat filter" bullet. `content/base/
-- crafting.lua` already demonstrates `vb.on("chat", ...)` as a veto-only
-- command trigger (`/craft <name>`) -- this is the *text-rewriting* half
-- (Phase 6.10) that file doesn't touch: returning a string instead of
-- `false`/`true` replaces what the *next* handler sees and, if this is the
-- last handler to touch it, what actually gets broadcast in `S2C_Chat`.
--
-- Two behaviors chained in registration order (docs/lua-api.md: "the same
-- veto-or-replace chaining shape as player_input"):
-- 1. A message wrapped in "!!...!!" is treated as a shout: strip the
--    markers and upper-case the rest.
-- 2. A tiny profanity-lite filter censors one placeholder word regardless
--    of shout-wrapping -- runs second, so it sees shout's already-rewritten
--    text, demonstrating that chaining actually composes rather than each
--    handler only ever seeing the client's raw original string.
vb.on("chat", function(player, text)
	local shout = text:match("^!!(.+)!!$")
	if shout then
		return shout:upper()
	end
	return true -- pass through unchanged
end)

vb.on("chat", function(player, text)
	local censored, n = text:gsub("darn", "d***")
	if n > 0 then
		return censored
	end
	return true
end)
