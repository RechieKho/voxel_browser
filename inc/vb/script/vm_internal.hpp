#pragma once

#if VB_WITH_LUA

#include <cstddef>

#include <sol/sol.hpp>

#include "vb/script/vm.hpp"

// Internal to vb_core's VB_WITH_LUA-gated .cpp files (vm.cpp, pack_runtime.cpp
// and friends). Exposes the real sol::state behind Vm's pImpl boundary so
// Phase 4 binding code can register vb.* tables/functions without the public
// vm.hpp header ever including sol2.

namespace vb::script {

// Lua allocator with a hard ceiling (spec §10.2). Allocation failure returns
// nullptr, which Lua turns into a catchable "not enough memory" error.
struct AllocState {
	std::size_t used = 0;
	std::size_t limit;
	explicit AllocState(std::size_t l) : limit(l) {}
};

void *vm_alloc(void *ud, void *ptr, std::size_t osize, std::size_t nsize);

struct Vm::Impl {
	AllocState alloc;
	int instruction_budget;
	sol::state lua;

	explicit Impl(VmLimits lim);
};

} // namespace vb::script

#endif // VB_WITH_LUA
