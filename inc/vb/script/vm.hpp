#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "vb/core/error.hpp"

// Thin C++ wrapper around an embedded Lua 5.4 state (spec §10.1/§10.2). sol2 is
// the binding layer but stays behind this pImpl so the rest of the engine never
// includes sol2 and non-Lua builds still compile against the header.
//
// The VM is created sandboxed: no os/io/debug(*)/package, no bytecode `load`, a
// memory ceiling and a per-call instruction-count hook. The pack API (§10.3) and
// the client UI API (§10.4) are layered on top in later Phase 4 steps.

namespace vb::script {

struct VmLimits {
	std::size_t memory_bytes = 64u * 1024u * 1024u; // hard heap ceiling
	int instruction_budget = 20'000'000; // per do_string / per pack callback
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

	// Re-arm the per-call instruction counter. The tick loop calls this before
	// dispatching each pack callback so one slow frame can't starve the next.
	void begin_call_budget();

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace vb::script
