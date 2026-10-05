-- base:death -- a "you died" notice, opened by content/base/death.lua's
-- `player_death` handler with `{ cause = <string> }`.
--
-- The engine respawns a player on the same tick they die (there's no
-- separate dead state a client could wait in), so by the time this shows the
-- player is already back at spawn with full health. It's a notice with a
-- Continue button, not a respawn prompt. Opening it also frees the cursor,
-- same as every other modal.
local kPanelW = 300
local kPanelH = 140
local kPad = 16
local kButtonW = 140
local kButtonH = 32

-- Keys are the `cause` strings the engine/packs pass to player:damage() and
-- the death hook ("fall", "void", "hunger", "pvp", ...).
local kCauseText = {
	fall = "You fell from a high place.",
	void = "You fell out of the world.",
	hunger = "You starved.",
	pvp = "You were slain.",
	zombie = "You were killed by a zombie.",
}

ui.define("base:death", function(state)
	local screen = client.screen_size()
	local panel_x = math.floor((screen.width - kPanelW) / 2)
	local panel_y = math.floor((screen.height - kPanelH) / 2)
	local cause = state and state.cause
	local reason = kCauseText[cause] or "You died."

	return {
		widgets = {
			{
				id = "backdrop",
				type = "rect",
				x = 0, y = 0, w = screen.width, h = screen.height,
				color = { 90, 10, 10, 150 },
			},
			{
				id = "panel",
				type = "rect",
				x = panel_x, y = panel_y, w = kPanelW, h = kPanelH,
				color = base_ui.panel_bg,
				border = { 170, 60, 60, 255 },
			},
			{
				id = "title",
				type = "text",
				x = panel_x + math.floor(kPanelW / 2),
				y = panel_y + kPad,
				align = "center",
				text = "You died",
				font_size = 24,
				color = { 230, 90, 90, 255 },
			},
			{
				id = "reason",
				type = "text",
				x = panel_x + math.floor(kPanelW / 2),
				y = panel_y + kPad + 40,
				align = "center",
				text = reason,
				font_size = 14,
				color = base_ui.text,
			},
			{
				id = "continue",
				type = "button",
				x = panel_x + math.floor((kPanelW - kButtonW) / 2),
				y = panel_y + kPanelH - kPad - kButtonH,
				w = kButtonW,
				h = kButtonH,
				text = "Continue",
				on_click = function()
					ui.close()
				end,
			},
		},
	}
end)
