#pragma once

#if VB_WITH_LUA

#include <sol/sol.hpp>

#include "vb/world/block.hpp"

// Table -> BlockType parsing, shared by `vb.register_block` (PackRuntime) and
// block data scripts (data_script.hpp, docs/structure-editor.md §G) so both
// read exactly the same fields with the same defaults.

namespace vb::script {

// Reads the engine-owned fields of a `vb.register_block` table (name, solid,
// opaque, liquid, region, light, texture, max_damage, crack_texture,
// max_stack, pickup_radius, item_lifetime_seconds, replaceable). Keys it
// doesn't know -- handlers, a pack's own `drops` field -- are ignored.
// Throws sol::error if `name` is missing or empty.
world::BlockType parse_block_type(const sol::table &def);

} // namespace vb::script

#endif // VB_WITH_LUA
