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
void Vm::install_require(std::unordered_map<std::string, std::string>) {}
Vm::Impl &Vm::native_impl() { return *impl_; }

} // namespace vb::script

#else

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vb/script/vm_internal.hpp"

namespace vb::script {

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

namespace {

// How often (in Lua VM instructions) the hook fires -- deliberately much
// finer-grained than any real instruction_budget so the wall-clock deadline
// (which only the hook can check) gets sampled often regardless of how large
// the caller's own instruction budget is.
constexpr std::size_t kHookPeriod = 1000;

constexpr const char *kInstructionBudgetMarker = "vb:instruction-budget-exceeded";
constexpr const char *kWallClockBudgetMarker = "vb:wall-clock-budget-exceeded";

// Fires every kHookPeriod instructions. Checks the wall-clock deadline first
// (it's the tighter, more time-sensitive bound) then the running instruction
// total; raising either here longjmps back into the protected call.
void count_hook(lua_State *L, lua_Debug *) {
	void *ud = nullptr;
	lua_getallocf(L, &ud);
	auto *st = static_cast<AllocState *>(ud);
	if (st == nullptr || !st->time_boxed) {
		return;
	}
	if (st->wall_clock_budget.count() > 0 &&
			std::chrono::steady_clock::now() >= st->deadline) {
		luaL_error(L, "%s", kWallClockBudgetMarker);
	}
	st->instructions_run += kHookPeriod;
	if (st->instruction_budget > 0 && st->instructions_run >= st->instruction_budget) {
		luaL_error(L, "%s", kInstructionBudgetMarker);
	}
}

core::ScriptError classify(sol::call_status status, const std::string &msg) {
	if (msg.find(kInstructionBudgetMarker) != std::string::npos ||
			msg.find(kWallClockBudgetMarker) != std::string::npos) {
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

Vm::Impl::Impl(VmLimits lim)
		: alloc(lim.memory_bytes,
				  lim.instruction_budget > 0
						  ? static_cast<std::size_t>(lim.instruction_budget)
						  : 0,
				  std::chrono::milliseconds(lim.wall_clock_budget_ms)),
		  lua(sol::default_at_panic, &vm_alloc, &alloc) {
	lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table,
			sol::lib::math, sol::lib::coroutine, sol::lib::utf8,
			sol::lib::debug);
	strip_sandbox(lua);
	// Reinstate a safe `require` on top of the freshly-stripped nil: resolves
	// only against install_require's virtual module map, never the real
	// filesystem (REMAINING_TASKS.md Phase 4.1).
	lua.set_function("require", [this](const std::string &name) -> sol::object {
		return require_module(name);
	});
}

Vm::Vm(VmLimits limits) : impl_(std::make_unique<Impl>(limits)) {}
Vm::~Vm() = default;
Vm::Vm(Vm &&) noexcept = default;
Vm &Vm::operator=(Vm &&) noexcept = default;

void Vm::begin_call_budget() {
	AllocState &st = impl_->alloc;
	st.instructions_run = 0;
	st.deadline = std::chrono::steady_clock::now() + st.wall_clock_budget;
	st.time_boxed = true;
	lua_sethook(impl_->lua.lua_state(), &count_hook, LUA_MASKCOUNT,
			static_cast<int>(kHookPeriod));
}

void Vm::install_require(std::unordered_map<std::string, std::string> modules) {
	impl_->require_sources = std::move(modules);
	impl_->require_cache.clear();
}

// require(name): resolves a dotted or slash-separated module name against
// the virtual module map only (install_require) -- never std::filesystem.
// Mirrors stock Lua's package.loaded caching (a module runs at most once; a
// chunk with no explicit `return` caches as `true`) and rejects a name that
// tries to escape the map (".."/leading '/') or forms a require cycle.
sol::object Vm::Impl::require_module(const std::string &name) {
	if (name.empty() || name.front() == '/' ||
			name.find("..") != std::string::npos) {
		throw sol::error("require: invalid module name '" + name + "'");
	}
	const auto cached = require_cache.find(name);
	if (cached != require_cache.end()) {
		return cached->second;
	}
	if (std::find(require_stack.begin(), require_stack.end(), name) !=
			require_stack.end()) {
		throw sol::error("require: circular dependency on module '" + name + "'");
	}

	std::string path = name;
	for (char &c : path) {
		if (c == '.') {
			c = '/';
		}
	}
	if (path.size() < 4 || path.compare(path.size() - 4, 4, ".lua") != 0) {
		path += ".lua";
	}
	const auto src_it = require_sources.find(path);
	if (src_it == require_sources.end()) {
		throw sol::error("require: module '" + name + "' not found");
	}

	sol::load_result loaded = lua.load(src_it->second, name, sol::load_mode::text);
	if (!loaded.valid()) {
		const sol::error e = loaded;
		throw sol::error("require: module '" + name + "' failed to compile: " +
				std::string(e.what()));
	}
	sol::function traceback = lua["debug"]["traceback"];
	sol::protected_function fn = loaded;
	fn.set_default_handler(traceback);

	require_stack.push_back(name);
	const sol::protected_function_result r = fn();
	require_stack.pop_back();

	if (!r.valid()) {
		const sol::error e = r;
		throw sol::error("require: module '" + name + "' failed: " +
				std::string(e.what()));
	}

	sol::object result = r.return_count() > 0 ? r.get<sol::object>(0)
											   : sol::make_object(lua, true);
	require_cache.emplace(name, result);
	return result;
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
			absent("dofile") && absent("loadfile") && absent("package");
	// require is deliberately *not* absent -- it's reinstated as the safe,
	// virtual-FS-only function installed by Impl::Impl (see require_module).
	const bool require_is_sandboxed = L["require"].get_type() == sol::type::function;
	const bool debug_trimmed = L["debug"]["traceback"].valid() &&
			!L["debug"]["getinfo"].valid() && !L["debug"]["sethook"].valid();
	return globals_gone && require_is_sandboxed && debug_trimmed;
}

std::size_t Vm::memory_used() const { return impl_->alloc.used; }
std::size_t Vm::memory_limit() const { return impl_->alloc.limit; }

Vm::Impl &Vm::native_impl() { return *impl_; }

} // namespace vb::script

#endif // VB_WITH_LUA
