#include "vb/script/data_script.hpp"

#if VB_WITH_LUA

#include <fstream>
#include <sstream>

#include "vb/script/pack_loader.hpp"
#include "vb/script/vm_internal.hpp"

namespace vb::script {

namespace {

DataScript fail(const std::filesystem::path &path, const std::string &msg) {
	DataScript out;
	out.error = path.generic_string() + ": " + msg;
	return out;
}

} // namespace

DataScript eval_data_script(
		const std::filesystem::path &path, const std::filesystem::path &pack_root) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return fail(path, "could not open file");
	}
	std::ostringstream ss;
	ss << in.rdbuf();

	auto vm = std::make_shared<Vm>();
	vm->install_require(collect_requirable_modules(pack_root));
	sol::state &lua = vm->native_impl().lua;

	// Stub `vb`: any read or write raises, so `vb.register_block(...)` and
	// `vb.storage.x` fail the same way.
	sol::table stub = lua.create_table();
	sol::table meta = lua.create_table();
	meta["__index"] = [](sol::this_state, sol::object, sol::object) -> sol::object {
		throw sol::error(kDataScriptVbError);
	};
	meta["__newindex"] = [](sol::this_state, sol::object, sol::object, sol::object) {
		throw sol::error(kDataScriptVbError);
	};
	stub[sol::metatable_key] = meta;
	lua["vb"] = stub;

	sol::load_result loaded =
			lua.load(ss.str(), path.generic_string(), sol::load_mode::text);
	if (!loaded.valid()) {
		const sol::error e = loaded;
		return fail(path, e.what());
	}
	sol::protected_function fn = loaded;
	fn.set_default_handler(lua["debug"]["traceback"]);

	vm->begin_call_budget();
	const sol::protected_function_result r = fn();
	lua_sethook(lua.lua_state(), nullptr, 0, 0);
	if (!r.valid()) {
		const sol::error e = r;
		return fail(path, e.what());
	}
	if (r.return_count() < 1 || r.get_type(0) != sol::type::table) {
		return fail(path, "data script must return a table");
	}

	DataScript out;
	out.ok = true;
	out.vm = std::move(vm);
	out.value = r.get<sol::table>(0);
	return out;
}

} // namespace vb::script

#endif // VB_WITH_LUA
