#pragma once

#if VB_WITH_LUA

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <sol/sol.hpp>

#include "vb/script/vm.hpp"

// Internal to vb_core's VB_WITH_LUA-gated .cpp files (vm.cpp, pack_runtime.cpp
// and friends). Exposes the real sol::state behind Vm's pImpl boundary so
// Phase 4 binding code can register vb.* tables/functions without the public
// vm.hpp header ever including sol2.

namespace vb::script {

// Lua allocator with a hard ceiling (spec §10.2), reused as the count-hook's
// own state (reachable from inside the hook via lua_getallocf, which hands
// back this same userdata pointer) since it's the one piece of state Lua's C
// API already threads through to both places. `instructions_run`/`deadline`
// are reset by Vm::begin_call_budget() before each do_string()/pack callback.
struct AllocState {
	std::size_t used = 0;
	std::size_t limit;
	std::size_t instruction_budget; // 0 = uncapped
	std::chrono::milliseconds wall_clock_budget; // <= 0 = uncapped
	std::size_t instructions_run = 0;
	std::chrono::steady_clock::time_point deadline{};
	bool time_boxed = false; // false until the first begin_call_budget() call

	AllocState(std::size_t mem_limit, std::size_t instr_budget,
			std::chrono::milliseconds wall_budget)
			: limit(mem_limit), instruction_budget(instr_budget),
			  wall_clock_budget(wall_budget) {}
};

void *vm_alloc(void *ud, void *ptr, std::size_t osize, std::size_t nsize);

struct Vm::Impl {
	AllocState alloc;
	sol::state lua;

	// `require`'s virtual module filesystem (Vm::install_require), its
	// package.loaded-equivalent cache, and an in-progress stack for circular-
	// dependency detection -- see Impl::require_module in vm.cpp.
	std::unordered_map<std::string, std::string> require_sources;
	std::unordered_map<std::string, sol::object> require_cache;
	std::vector<std::string> require_stack;

	explicit Impl(VmLimits lim);

	sol::object require_module(const std::string &name);
};

} // namespace vb::script

#endif // VB_WITH_LUA
