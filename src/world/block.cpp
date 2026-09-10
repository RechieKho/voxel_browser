#include "vb/world/block.hpp"

namespace vb::world {

namespace {
const BlockType kAirType{ "base:air", /*solid*/ false, /*opaque*/ false,
	/*liquid*/ false, /*light*/ 0 };
} // namespace

BlockRegistry BlockRegistry::base() {
	BlockRegistry r;
	r.add({ "base:air", false, false, false, 0 });
	r.add({ "base:stone", true, true, false, 0 });
	r.add({ "base:dirt", true, true, false, 0 });
	r.add({ "base:grass", true, true, false, 0 });
	r.add({ "base:sand", true, true, false, 0 });
	r.add({ "base:water", false, false, true, 0 });
	r.add({ "base:wood", true, true, false, 0 });
	r.add({ "base:leaves", true, false, false, 0 });
	return r;
}

core::BlockId BlockRegistry::add(BlockType type) {
	const auto id = static_cast<core::BlockId>(types_.size());
	types_.push_back(std::move(type));
	return id;
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
