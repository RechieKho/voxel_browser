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

// Adds `type` to `registry` the way `vb.register_block` does: idempotent by
// name (`add_or_get`), then the fields add_or_get leaves alone on an already
// registered block (texture, crack_texture, replaceable) are applied on top,
// so a pack re-declaring a built-in block to attach a texture works.
// Shared by PackRuntime and the structure editor's block catalog so both
// build identical registries from the same data. Returns the block's id.
core::BlockId register_block_type(world::BlockRegistry &registry, const world::BlockType &type);

} // namespace vb::script

#endif // VB_WITH_LUA
