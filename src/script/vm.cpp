#include "vb/script/vm.hpp"

#if !VB_WITH_LUA

// Stub build: scripting compiled out. Every entry point reports kDisabled.

namespace vb::script {

struct Vm::Impl {};

Vm::Vm(VmLimits) : impl_(nullptr) {}
Vm::~Vm() = default;
Vm::Vm(Vm &&) noexcept = default;
Vm &Vm::operator=(Vm &&) noexcept = default;

ScriptResult Vm::do_string(std::string_view, std::string_view) {
	return { false, core::ScriptError::kDisabled,
		"scripting disabled (built without VB_WITH_LUA)" };
}
bool Vm::sandbox_intact() const { return true; }
std::size_t Vm::memory_used() const { return 0; }
std::size_t Vm::memory_limit() const { return 0; }
void Vm::begin_call_budget() {}

} // namespace vb::script

#else

#include <cstdlib>
#include <string>
#include <vector>

#include <sol/sol.hpp>

namespace vb::script {

namespace {

// Lua allocator with a hard ceiling. Allocation failure returns nullptr, which
// Lua turns into a catchable "not enough memory" error (spec §10.2).
struct AllocState {
	std::size_t used = 0;
	std::size_t limit;
	explicit AllocState(std::size_t l) : limit(l) {}
};

void *vm_alloc(void *ud, void *ptr, std::size_t osize, std::size_t nsize) {
	auto *st = static_cast<AllocState *>(ud);
	if (nsize == 0) {
		if (ptr != nullptr) {
			st->used -= osize;
			std::free(ptr);
		}
		return nullptr;
	}
	const std::size_t old_contribution = (ptr != nullptr) ? osize : 0;
	const std::size_t projected = st->used - old_contribution + nsize;
	if (projected > st->limit) {
		return nullptr;
	}
	void *np = std::realloc(ptr, nsize);
	if (np == nullptr) {
		return nullptr;
	}
	st->used = projected;
	return np;
}

// Fires once the per-call instruction count is reached; raising here longjmps
// back into the protected call.
void count_hook(lua_State *L, lua_Debug *) {
	luaL_error(L, "vb:instruction-budget-exceeded");
}

constexpr const char *kBudgetMarker = "vb:instruction-budget-exceeded";

core::ScriptError classify(sol::call_status status, const std::string &msg) {
	if (msg.find(kBudgetMarker) != std::string::npos) {
		return core::ScriptError::kBudgetExceeded;
	}
	switch (status) {
		case sol::call_status::memory:
			return core::ScriptError::kOutOfMemory;
		case sol::call_status::syntax:
			return core::ScriptError::kSyntax;
		default:
			return core::ScriptError::kRuntime;
	}
}

void strip_sandbox(sol::state &L) {
	// debug: keep only traceback.
	if (L["debug"].is<sol::table>()) {
		sol::table dbg = L["debug"];
		std::vector<std::string> keys;
		for (const auto &kv : dbg) {
			if (kv.first.is<std::string>()) {
				keys.push_back(kv.first.as<std::string>());
			}
		}
		for (const std::string &k : keys) {
			if (k != "traceback") {
				dbg[k] = sol::lua_nil;
			}
		}
	}
	// Base-environment globals that reach the host or load bytecode.
	for (const char *g : { "dofile", "loadfile", "load", "loadstring",
				 "collectgarbage", "require", "package", "os", "io" }) {
		L[g] = sol::lua_nil;
	}
}

} // namespace

struct Vm::Impl {
	AllocState alloc;
	int instruction_budget;
	sol::state lua;

	explicit Impl(VmLimits lim) : alloc(lim.memory_bytes),
								  instruction_budget(lim.instruction_budget),
								  lua(sol::default_at_panic, &vm_alloc, &alloc) {
		lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table,
				sol::lib::math, sol::lib::coroutine, sol::lib::utf8,
				sol::lib::debug);
		strip_sandbox(lua);
	}
};

Vm::Vm(VmLimits limits) : impl_(std::make_unique<Impl>(limits)) {}
Vm::~Vm() = default;
Vm::Vm(Vm &&) noexcept = default;
Vm &Vm::operator=(Vm &&) noexcept = default;

void Vm::begin_call_budget() {
	const int n = impl_->instruction_budget > 0 ? impl_->instruction_budget : 0;
	lua_sethook(impl_->lua.lua_state(), &count_hook, LUA_MASKCOUNT, n);
}

ScriptResult Vm::do_string(std::string_view code, std::string_view chunk_name) {
	sol::state &L = impl_->lua;

	sol::load_result loaded =
			L.load(code, std::string(chunk_name), sol::load_mode::text);
	if (!loaded.valid()) {
		const sol::error e = loaded;
		return { false, core::ScriptError::kSyntax, e.what() };
	}

	sol::function traceback = L["debug"]["traceback"];
	sol::protected_function fn = loaded;
	fn.set_default_handler(traceback);

	begin_call_budget();
	const sol::protected_function_result r = fn();
	// Disable the hook again so ambient code (GC finalizers) isn't clipped.
	lua_sethook(L.lua_state(), nullptr, 0, 0);

	if (!r.valid()) {
		const sol::error e = r;
		std::string msg = e.what();
		return { false, classify(r.status(), msg), std::move(msg) };
	}
	return ScriptResult::success();
}

bool Vm::sandbox_intact() const {
	sol::state &L = impl_->lua;
	auto absent = [&](const char *g) { return !L[g].valid(); };
	const bool globals_gone = absent("os") && absent("io") && absent("load") &&
			absent("dofile") && absent("loadfile") && absent("package") &&
			absent("require");
	const bool debug_trimmed = L["debug"]["traceback"].valid() &&
			!L["debug"]["getinfo"].valid() && !L["debug"]["sethook"].valid();
	return globals_gone && debug_trimmed;
}

std::size_t Vm::memory_used() const { return impl_->alloc.used; }
std::size_t Vm::memory_limit() const { return impl_->alloc.limit; }

} // namespace vb::script

#endif // VB_WITH_LUA
