#pragma once

#include <string>
#include <vector>

#include "vb/script/vm.hpp"

// Lua API surface inventory (architecture_spec/dev-experience.md §3.2). Walks
// the engine globals of a live VM and lists every dotted name reachable from
// them, so a unit test can compare the bound API against the hand-written
// stubs in sdk/lua/. A build without VB_WITH_LUA returns an empty list.

namespace vb::script {

// Dotted names of everything reachable from the global tables named in
// `roots` (e.g. {"vb"} or {"ui", "client"}), sorted and de-duplicated.
// Functions and callable usertype members are listed as `vb.world.get_block`;
// sol2 usertypes are listed as `Type:method` (instance methods) and
// `Type.name` (static). Plain data values (numbers, strings, booleans) are
// listed with a trailing `=`-less name too, so constants are covered.
std::vector<std::string> describe_lua_surface(Vm &vm, const std::vector<std::string> &roots);

// Names of every string-keyed global currently in the VM (`_G`), e.g. to learn which
// globals pack code defined at run time (`_G[name] = ...`). Empty without VB_WITH_LUA.
std::vector<std::string> list_lua_globals(Vm &vm);

} // namespace vb::script
