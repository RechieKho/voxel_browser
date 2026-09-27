#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "vb/core/error.hpp"

// Thin C++ wrapper around an embedded Lua 5.4 state (spec §10.1/§10.2). sol2 is
// the binding layer but stays behind this pImpl so the rest of the engine never
// includes sol2 and non-Lua builds still compile against the header.
//
// The VM is created sandboxed: no os/io/debug(*)/package, no bytecode `load`, a
// memory ceiling, a per-call instruction-count hook, and a per-call wall-clock
// hook. `require` is reinstated as a safe, sandboxed function that resolves
// only against an in-memory virtual module filesystem (install_require) --
// never the real filesystem. The pack API (§10.3) and the client UI API
// (§10.4) are layered on top in later Phase 4 steps.

namespace vb::script {

struct VmLimits {
	std::size_t memory_bytes = 64u * 1024u * 1024u; // hard heap ceiling
	int instruction_budget = 20'000'000; // per do_string / per pack callback
	std::int64_t wall_clock_budget_ms = 250; // per do_string / per pack callback
};

struct ScriptResult {
	bool ok = false;
	core::ScriptError error = core::ScriptError::kNone;
	std::string message; // Lua error text + traceback, empty on success

	explicit operator bool() const { return ok; }
	static ScriptResult success() { return { true, core::ScriptError::kNone, {} }; }
};

class Vm {
public:
	explicit Vm(VmLimits limits = {});
	~Vm();
	Vm(Vm &&) noexcept;
	Vm &operator=(Vm &&) noexcept;
	Vm(const Vm &) = delete;
	Vm &operator=(const Vm &) = delete;

	// Compile + run a chunk of Lua *source* (bytecode is rejected). `chunk_name`
	// appears in error messages / tracebacks.
	ScriptResult do_string(std::string_view code,
			std::string_view chunk_name = "chunk");

	// True if a global that should have been stripped is reachable — a cheap
	// post-construction sandbox assertion for tests.
	bool sandbox_intact() const;

	std::size_t memory_used() const;
	std::size_t memory_limit() const;

	// Re-arm the per-call instruction counter and wall-clock deadline. The
	// tick loop calls this before dispatching each pack callback so one slow
	// frame can't starve the next.
	void begin_call_budget();

	// Install (or replace) the pack's virtual module filesystem for `require`
	// (module name -> Lua source). A dotted module name resolves to
	// name.replace('.', '/') + ".lua" inside this map only -- there is no
	// real filesystem access, and a name containing ".." or starting with '/'
	// is rejected. A module's return value is cached across repeated
	// require() calls exactly like stock Lua's package.loaded (no return
	// value caches as `true`). Safe to call with an empty map (every require
	// then fails with "not found"); calling it again drops the cache, so a
	// hot-swapped module is re-run on its next require().
	void install_require(std::unordered_map<std::string, std::string> modules);

	struct Impl;

	// Escape hatch for Phase 4 binding code (pack_runtime.cpp) that needs the
	// raw sol::state. Only usable by TUs that also include vm_internal.hpp,
	// which defines the real Impl; this header never includes sol2.
	Impl &native_impl();

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace vb::script
