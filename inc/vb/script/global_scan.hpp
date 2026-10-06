#pragma once

#include <string>
#include <string_view>
#include <vector>

// Static global-access scan (architecture_spec/dev-experience.md §3.4.1):
// compiles a Lua chunk and lists every global it reads or writes, with line
// numbers, by walking the bytecode of the chunk and all its nested functions
// (the `luac -l` GETTABUP/SETTABUP _ENV accesses). Nothing is executed.
// Without VB_WITH_LUA the scan reports an error.

namespace vb::script {

struct GlobalAccess {
	std::string name;
	int line = 0;
	bool is_write = false;
};

struct GlobalScan {
	bool ok = false;
	std::string error; // Lua syntax error text ("chunk:3: ...") when !ok
	std::vector<GlobalAccess> accesses;
};

GlobalScan scan_globals(std::string_view source, const std::string &chunk_name);

} // namespace vb::script
