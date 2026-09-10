#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vb/core/ids.hpp"

// The block registry (spec §5.2). Phase 2 ships a hardcoded base set; Phase 4
// replaces `BlockRegistry::base()` with Lua `vb.register_block{...}` and freezes
// it after pack load. `BlockId` 0 is always air. Everything gameplay-facing that
// needs block properties (worldgen, lighting, meshing, physics) goes through
// this, not through hardcoded id checks.

namespace vb::world {

struct BlockType {
	std::string name; // "base:stone"
	bool solid = true; // AABB collision participation
	bool opaque = true; // face-culling + full light occlusion
	bool liquid = false; // flows; non-solid; partial light occlusion
	std::uint8_t light_emission = 0; // 0..15
};

// Well-known ids in the Phase 2 base registry. Do not assume these hold once
// Phase 4's Lua registration is in — look ids up by name instead.
namespace base_block {
inline constexpr core::BlockId air = core::BlockId::kAir;
inline constexpr auto stone = static_cast<core::BlockId>(1);
inline constexpr auto dirt = static_cast<core::BlockId>(2);
inline constexpr auto grass = static_cast<core::BlockId>(3);
inline constexpr auto sand = static_cast<core::BlockId>(4);
inline constexpr auto water = static_cast<core::BlockId>(5);
inline constexpr auto wood = static_cast<core::BlockId>(6);
inline constexpr auto leaves = static_cast<core::BlockId>(7);
} // namespace base_block

class BlockRegistry {
public:
	// The hardcoded Phase 2 set (air, stone, dirt, grass, sand, water, wood,
	// leaves). Order matches base_block:: above.
	static BlockRegistry base();

	core::BlockId add(BlockType type);

	std::size_t size() const { return types_.size(); }
	bool contains(core::BlockId id) const {
		return static_cast<std::size_t>(id) < types_.size();
	}

	const BlockType &get(core::BlockId id) const;
	core::BlockId find(std::string_view name) const; // kAir if unknown

	bool is_solid(core::BlockId id) const { return prop(id).solid; }
	bool is_opaque(core::BlockId id) const { return prop(id).opaque; }
	bool is_liquid(core::BlockId id) const { return prop(id).liquid; }
	std::uint8_t light_emission(core::BlockId id) const {
		return prop(id).light_emission;
	}

private:
	const BlockType &prop(core::BlockId id) const;

	std::vector<BlockType> types_;
};

} // namespace vb::world
