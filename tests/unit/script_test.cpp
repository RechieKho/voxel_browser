#include <doctest/doctest.h>

#include <ostream>

#include <string>

#include "vb/script/vm.hpp"

#if !VB_WITH_LUA

TEST_CASE("script VM reports kDisabled when built without VB_WITH_LUA") {
	vb::script::Vm vm;
	const auto r = vm.do_string("return 1");
	CHECK_FALSE(r);
	CHECK(r.error == vb::core::ScriptError::kDisabled);
}

#else

using vb::core::ScriptError;
using vb::script::Vm;
using vb::script::VmLimits;

TEST_CASE("Vm runs a simple chunk") {
	Vm vm;
	CHECK(vm.do_string("x = 1 + 2"));
}

TEST_CASE("Vm classifies a syntax error") {
	Vm vm;
	const auto r = vm.do_string("this is not lua =");
	CHECK_FALSE(r);
	CHECK(r.error == ScriptError::kSyntax);
}

TEST_CASE("Vm surfaces a runtime error message + traceback") {
	Vm vm;
	const auto r = vm.do_string("error('boom')", "unit");
	CHECK_FALSE(r);
	CHECK(r.error == ScriptError::kRuntime);
	CHECK(r.message.find("boom") != std::string::npos);
}

TEST_CASE("Vm sandbox strips host-reaching globals and trims debug") {
	Vm vm;
	CHECK(vm.sandbox_intact());
	CHECK(vm.do_string(
			"assert(os == nil); assert(io == nil); assert(load == nil); "
			"assert(require == nil); assert(package == nil)"));
	CHECK(vm.do_string(
			"assert(debug.traceback ~= nil); assert(debug.getinfo == nil)"));
}

TEST_CASE("Vm instruction budget aborts a runaway loop") {
	VmLimits lim;
	lim.instruction_budget = 200000;
	Vm vm(lim);
	const auto r = vm.do_string("while true do end");
	CHECK_FALSE(r);
	CHECK(r.error == ScriptError::kBudgetExceeded);
}

TEST_CASE("Vm memory ceiling is enforced, not fatal") {
	VmLimits lim;
	lim.memory_bytes = 2u * 1024u * 1024u;
	lim.instruction_budget = 100'000'000;
	Vm vm(lim);
	const auto r =
			vm.do_string("local t = {} for i = 1, 1e9 do t[i] = i * 2 end");
	CHECK_FALSE(r);
	CHECK(r.error == ScriptError::kOutOfMemory);
	CHECK(vm.memory_used() <= vm.memory_limit());
	// The VM is still usable afterwards.
	CHECK(vm.do_string("return 1 + 1"));
}

TEST_CASE("Vm keeps string / math / table but not the whole stdlib") {
	Vm vm;
	CHECK(vm.do_string("assert(string.upper('ab') == 'AB')"));
	CHECK(vm.do_string("assert(math.floor(1.9) == 1)"));
	CHECK(vm.do_string("assert(#({1,2,3}) == 3)"));
}

#endif // VB_WITH_LUA
