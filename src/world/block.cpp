#include "vb/world/block.hpp"

namespace vb::world {

namespace {
const BlockType kAirType{ .name = "base:air", .solid = false, .opaque = false, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" };
} // namespace

BlockRegistry BlockRegistry::base() {
	BlockRegistry r;
	r.add({ .name = "base:air", .solid = false, .opaque = false, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" });
	r.add({ .name = "base:stone", .solid = true, .opaque = true, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" });
	r.add({ .name = "base:dirt", .solid = true, .opaque = true, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" });
	r.add({ .name = "base:grass", .solid = true, .opaque = true, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" });
	r.add({ .name = "base:sand", .solid = true, .opaque = true, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" });
	BlockType water{ .name = "base:water", .solid = false, .opaque = false, .liquid = true, .light_emission = 0, .texture = "", .crack_texture = "" };
	water.region = true; // Phase 7.3: first user of the generic region hook
	r.add(std::move(water));
	r.add({ .name = "base:wood", .solid = true, .opaque = true, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" });
	r.add({ .name = "base:leaves", .solid = true, .opaque = false, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" });
	return r;
}

core::BlockId BlockRegistry::add(BlockType type) {
	const auto id = static_cast<core::BlockId>(types_.size());
	types_.push_back(std::move(type));
	return id;
}

core::BlockId BlockRegistry::add_or_get(std::string_view name, BlockType type) {
	for (std::size_t i = 0; i < types_.size(); ++i) {
		if (types_[i].name == name) {
			return static_cast<core::BlockId>(i);
		}
	}
	type.name = std::string(name);
	return add(std::move(type));
}

void BlockRegistry::set_texture(core::BlockId id, std::string texture) {
	const auto idx = static_cast<std::size_t>(id);
	if (idx < types_.size()) {
		types_[idx].texture = std::move(texture);
	}
}

void BlockRegistry::set_replaceable(core::BlockId id, bool replaceable) {
	const auto idx = static_cast<std::size_t>(id);
	if (idx < types_.size()) {
		types_[idx].replaceable = replaceable;
	}
}

void BlockRegistry::set_crack_texture(core::BlockId id, std::string crack_texture) {
	const auto idx = static_cast<std::size_t>(id);
	if (idx < types_.size()) {
		types_[idx].crack_texture = std::move(crack_texture);
	}
}

const BlockType &BlockRegistry::get(core::BlockId id) const { return prop(id); }

const BlockType &BlockRegistry::prop(core::BlockId id) const {
	const auto idx = static_cast<std::size_t>(id);
	return idx < types_.size() ? types_[idx] : kAirType;
}

core::BlockId BlockRegistry::find(std::string_view name) const {
	for (std::size_t i = 0; i < types_.size(); ++i) {
		if (types_[i].name == name) {
			return static_cast<core::BlockId>(i);
		}
	}
	return core::BlockId::kAir;
}

} // namespace vb::world
