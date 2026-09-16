-- base:planks -- solid, opaque, crafted from base:wood (see crafting.lua).
-- Never appears from worldgen or breaking terrain directly, same as
-- base:wood itself -- the only way to get one is to craft it (or place one
-- another player already crafted and dropped). Registered as a real block
-- like every other item this pack knows about: `entity:get_inventory()`/
-- `give()`/`take()` are all BlockId-keyed today (REMAINING_TASKS.md 5.1's
-- own noted gap: "vb.register_item never allocates its own id space"), so
-- anything a player can hold has to be a registered block, crafted
-- materials included.
base_planks_id = vb.register_block({
	name = "base:planks",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			base_planks_id, 1)
	end,
})
