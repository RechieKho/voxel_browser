#include "vb/script/global_scan.hpp"

#if !VB_WITH_LUA

namespace vb::script {
GlobalScan scan_globals(std::string_view, const std::string &) {
	GlobalScan s;
	s.error = "built without VB_WITH_LUA";
	return s;
}
} // namespace vb::script

#else

extern "C" {
#include <lauxlib.h>
#include <lua.h>

using VbGlobalCb = void (*)(void *ud, const char *name, int line, int is_write);
void vb_scan_chunk_globals(lua_State *L, VbGlobalCb cb, void *ud);
}

namespace vb::script {

namespace {

void collect(void *ud, const char *name, int line, int is_write) {
	static_cast<GlobalScan *>(ud)->accesses.push_back({ name, line, is_write != 0 });
}

} // namespace

GlobalScan scan_globals(std::string_view source, const std::string &chunk_name) {
	GlobalScan out;
	lua_State *L = luaL_newstate();
	if (L == nullptr) {
		out.error = "out of memory";
		return out;
	}
	const std::string name = "@" + chunk_name;
	if (luaL_loadbufferx(L, source.data(), source.size(), name.c_str(), "t") != LUA_OK) {
		out.error = lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "syntax error";
		lua_close(L);
		return out;
	}
	vb_scan_chunk_globals(L, &collect, &out);
	out.ok = true;
	lua_close(L);
	return out;
}

} // namespace vb::script

#endif
