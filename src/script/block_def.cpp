#include "vb/script/block_def.hpp"

#if VB_WITH_LUA

#include <string>

namespace vb::script {

world::BlockType parse_block_type(const sol::table &def) {
	world::BlockType type;
	type.name = def.get_or("name", std::string{});
	if (type.name.empty()) {
		throw sol::error("block definition: 'name' is required");
	}
	type.solid = def.get_or("solid", true);
	type.opaque = def.get_or("opaque", true);
	type.liquid = def.get_or("liquid", false);
	// Phase 7.3: generic per-tick occupancy tracking opt-in (region_enter/
	// region_exit) -- independent of `liquid`, but every liquid block
	// defaults to opting in (matches base:water); non-liquid custom
	// blocks default false and must opt in explicitly.
	type.region = def.get_or("region", type.liquid);
	type.light_emission = static_cast<std::uint8_t>(def.get_or("light", 0));
	type.texture = def.get_or("texture", std::string{});
	// Phase 6.5 (spec §10.7): 0 (default) = today's instant break.
	type.max_damage = static_cast<std::uint16_t>(def.get_or("max_damage", 0));
	// Phase 6.5 (spec §5.2/§10.7): optional crack-stage spritesheet
	// override, empty = engine's own built-in generic crack overlay.
	type.crack_texture = def.get_or("crack_texture", std::string{});
	// Phase 6.9 (spec §11.1): stack cap for this item, engine default
	// unless overridden.
	type.max_stack = static_cast<std::uint16_t>(
			def.get_or("max_stack", static_cast<int>(world::kDefaultMaxStackSize)));
	// Phase 6.11: per-item dropped-instance overrides, ItemDropSystem's own
	// construction-time defaults unless set (negative = no override).
	type.pickup_radius = def.get_or("pickup_radius", -1.0);
	type.drop_lifetime_seconds = def.get_or("item_lifetime_seconds", -1.0);
	// Structure editor S0: a structure's `replace = "air_and_plants"` rule may
	// overwrite this block.
	type.replaceable = def.get_or("replaceable", false);
	return type;
}

} // namespace vb::script

#endif // VB_WITH_LUA
