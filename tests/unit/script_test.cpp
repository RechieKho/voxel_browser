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
			"assert(package == nil); assert(type(require) == 'function')"));
	CHECK(vm.do_string(
			"assert(debug.traceback ~= nil); assert(debug.getinfo == nil)"));
}

TEST_CASE("Vm require resolves a module from the installed virtual FS only") {
	Vm vm;
	vm.install_require(
			{ { "util.lua", "return { double = function(x) return x * 2 end }" } });
	CHECK(vm.do_string("local u = require('util'); assert(u.double(21) == 42)"));
}

TEST_CASE("Vm require accepts dotted module names against nested paths") {
	Vm vm;
	vm.install_require({ { "lib/util.lua", "return 7" } });
	CHECK(vm.do_string("assert(require('lib.util') == 7)"));
}

TEST_CASE("Vm require caches a module's result like package.loaded") {
	Vm vm;
	vm.install_require({ { "counter.lua", "COUNT = (COUNT or 0) + 1; return COUNT" } });
	CHECK(vm.do_string("local a = require('counter'); local b = require('counter'); "
						"assert(a == 1); assert(b == 1)"));
}

TEST_CASE("Vm require defaults a no-return module to true") {
	Vm vm;
	vm.install_require({ { "sideeffect.lua", "TOUCHED = true" } });
	CHECK(vm.do_string("assert(require('sideeffect') == true); assert(TOUCHED == true)"));
}

TEST_CASE("Vm require rejects a module outside the virtual FS") {
	Vm vm;
	const auto r = vm.do_string("require('nope')");
	CHECK_FALSE(r);
	CHECK(r.error == ScriptError::kRuntime);
	CHECK(r.message.find("not found") != std::string::npos);
}

TEST_CASE("Vm require rejects path traversal / absolute module names") {
	Vm vm;
	vm.install_require({ { "secret.lua", "return 1" } });
	CHECK_FALSE(vm.do_string("require('../secret')"));
	CHECK_FALSE(vm.do_string("require('/secret')"));
}

TEST_CASE("Vm require detects a circular dependency") {
	Vm vm;
	vm.install_require({
			{ "a.lua", "return require('b')" },
			{ "b.lua", "return require('a')" },
	});
	const auto r = vm.do_string("require('a')");
	CHECK_FALSE(r);
	CHECK(r.message.find("circular") != std::string::npos);
}

TEST_CASE("Vm install_require replacing the map drops the old cache") {
	Vm vm;
	vm.install_require({ { "m.lua", "return 1" } });
	CHECK(vm.do_string("assert(require('m') == 1)"));
	vm.install_require({ { "m.lua", "return 2" } });
	CHECK(vm.do_string("assert(require('m') == 2)"));
}

TEST_CASE("Vm instruction budget aborts a runaway loop") {
	VmLimits lim;
	lim.instruction_budget = 200000;
	Vm vm(lim);
	const auto r = vm.do_string("while true do end");
	CHECK_FALSE(r);
	CHECK(r.error == ScriptError::kBudgetExceeded);
}

TEST_CASE("Vm wall-clock budget aborts a runaway callback under a huge instruction budget") {
	VmLimits lim;
	lim.instruction_budget = 2'000'000'000; // effectively uncapped for this test
	lim.wall_clock_budget_ms = 50;
	Vm vm(lim);
	const auto r = vm.do_string("local x = 0; while true do x = x + 1 end");
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
