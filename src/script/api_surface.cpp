#include "vb/script/api_surface.hpp"

#if !VB_WITH_LUA

namespace vb::script {
std::vector<std::string> describe_lua_surface(Vm &, const std::vector<std::string> &) {
	return {};
}
std::vector<std::string> list_lua_globals(Vm &) {
	return {};
}
} // namespace vb::script

#else

#include <algorithm>
#include <set>

#include "vb/script/vm_internal.hpp"

namespace vb::script {

namespace {

// Recursively lists `tbl`'s string-keyed members under `prefix`. `seen`
// guards against cycles (a table reachable through two paths is listed under
// the first one only).
void walk_table(lua_State *L, int idx, const std::string &prefix,
		std::set<const void *> &seen, std::set<std::string> &out, int depth) {
	idx = lua_absindex(L, idx);
	if (depth > 6 || !seen.insert(lua_topointer(L, idx)).second) {
		return;
	}
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			const std::string name = prefix + "." + lua_tostring(L, -2);
			if (lua_type(L, -1) == LUA_TTABLE) {
				// A table with a metatable is a dynamic proxy (vb.storage):
				// list it as a leaf too.
				if (lua_getmetatable(L, -1) != 0) {
					out.insert(name);
					lua_pop(L, 1);
				}
				walk_table(L, -1, name, seen, out, depth + 1);
			} else {
				out.insert(name);
			}
		}
		lua_pop(L, 1);
	}
}

// sol2 keeps each usertype's metatable in the registry under "sol.<C++ name>"
// (alongside "sol.<name>.user", "sol.<name>♻", ... companions). The instance
// methods are in the metatable's __index table (or the metatable itself).
std::string short_type_name(const std::string &cpp_name) {
	const auto pos = cpp_name.rfind("::");
	return pos == std::string::npos ? cpp_name : cpp_name.substr(pos + 2);
}

void walk_usertypes(lua_State *L, std::set<std::string> &out) {
	lua_pushnil(L);
	while (lua_next(L, LUA_REGISTRYINDEX) != 0) {
		if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TTABLE) {
			const std::string key = lua_tostring(L, -2);
			// Only the primary metatable: "sol.<type>" with no suffix marker.
			const bool primary = key.rfind("sol.", 0) == 0 && key.find(".user") == std::string::npos &&
					key.find("\xE2\x99\xBB") == std::string::npos && key.find("*") == std::string::npos && key.find(">") == std::string::npos &&
					key.find("(") == std::string::npos && key.find("const") == std::string::npos &&
					key.find("sol.") == 0 && key.find("sol.", 4) == std::string::npos &&
					key.find("vb::") != std::string::npos;
			if (primary) {
				const std::string type = short_type_name(key.substr(4));
				lua_getfield(L, -1, "__index");
				const int tbl = lua_istable(L, -1) ? lua_gettop(L) : lua_gettop(L) - 1;
				lua_pushnil(L);
				while (lua_next(L, tbl) != 0) {
					if (lua_type(L, -2) == LUA_TSTRING) {
						const std::string member = lua_tostring(L, -2);
						if (member.rfind("__", 0) != 0 && member != "class_cast" &&
								member != "class_check" && member != "new") {
							out.insert(type + ":" + member);
						}
					}
					lua_pop(L, 1);
				}
				lua_pop(L, 1); // __index
			}
		}
		lua_pop(L, 1);
	}
}

} // namespace

std::vector<std::string> describe_lua_surface(Vm &vm, const std::vector<std::string> &roots) {
	lua_State *L = vm.native_impl().lua.lua_state();
	std::set<std::string> out;
	std::set<const void *> seen;
	for (const auto &root : roots) {
		lua_getglobal(L, root.c_str());
		if (lua_type(L, -1) == LUA_TTABLE) {
			walk_table(L, -1, root, seen, out, 0);
		}
		lua_pop(L, 1);
	}
	walk_usertypes(L, out);
	return { out.begin(), out.end() };
}

std::vector<std::string> list_lua_globals(Vm &vm) {
	lua_State *L = vm.native_impl().lua.lua_state();
	std::vector<std::string> out;
	lua_pushglobaltable(L);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			out.emplace_back(lua_tostring(L, -2));
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	std::sort(out.begin(), out.end());
	return out;
}

} // namespace vb::script

#endif
