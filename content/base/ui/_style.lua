-- base_ui -- the one place content/base's UI colors/spacing live, shared by
-- hud.lua, inventory.lua, pause.lua and death.lua so a restyle is a one-file
-- change instead of a hunt through four.
--
-- It's a plain global table. The client sorts `ui/*.lua` by path before
-- loading (src/client/client_app.cpp), and "_" sorts ahead of every letter,
-- so this file is always loaded first. Screens should still read `base_ui`
-- inside their render functions rather than copying values into top-level
-- locals, which keeps them correct even if a pack loads files differently.
--
-- Colors are `{ r, g, b, a }`, the same shape `rect`/`text` widgets take.
base_ui = {
	backdrop = { 0, 0, 0, 120 },
	panel_bg = { 24, 24, 30, 240 },
	panel_border = { 110, 110, 125, 255 },
	container_bg = { 20, 20, 26, 190 },
	container_border = { 110, 110, 125, 230 },
	slot_bg = { 40, 40, 46, 220 },
	slot_border = { 90, 90, 100, 230 },
	slot_selected = { 255, 220, 80, 255 },
	bar_bg = { 30, 30, 34, 200 },
	bar_border = { 90, 90, 100, 230 },
	title = { 235, 235, 245, 255 },
	text = { 220, 220, 220, 230 },
	text_bright = { 255, 255, 255, 255 },
	text_muted = { 170, 170, 180, 255 },
	tooltip_bg = { 16, 16, 22, 245 },
}

-- "base:oak_planks" -> "Oak planks". Shared by the inventory tooltip and the
-- hotbar's selected-item label.
function base_ui.pretty_name(name)
	local bare = (name:match("([^:]+)$") or name):gsub("_", " ")
	return bare:sub(1, 1):upper() .. bare:sub(2)
end
