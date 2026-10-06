#pragma once

#if VB_WITH_LUA

#include <filesystem>
#include <memory>
#include <string>

#include <sol/sol.hpp>

#include "vb/script/vm.hpp"

// Block data scripts (docs/structure-editor.md §G): a Lua file that returns a
// table of pure data, e.g. `data/blocks.lua` returning a list of the tables
// `vb.register_block` takes. Evaluated in a bare sandboxed state with no
// PackRuntime, transport, storage or config, so tools (the structure editor)
// can read pack data without loading the pack.
//
// The state has a stub `vb` whose every field raises
// kDataScriptVbError, and `require` is limited to the pack's own `.lua`
// files (collect_requirable_modules), so a data script can pull in shared
// constants but can't register anything.

namespace vb::script {

inline constexpr const char *kDataScriptVbError =
		"block data scripts must only return data; register blocks from pack code";

struct DataScript {
	bool ok = false;
	// Names the file on failure ("<path>: <lua error>"); empty on success.
	std::string error;
	// The Lua state `value` belongs to. Keep this alive while using `value`.
	std::shared_ptr<Vm> vm;
	// The table the script returned. Valid only when `ok`. Declared after `vm`
	// so it is destroyed first (it holds a reference into the state).
	sol::table value;

	DataScript() = default;
	DataScript(DataScript &&) noexcept = default;
	// Member-wise move assignment would replace `vm` (freeing the old state)
	// before releasing the old `value` that points into it.
	DataScript &operator=(DataScript &&other) noexcept {
		if (this != &other) {
			value = sol::table();
			vm = std::move(other.vm);
			value = std::move(other.value);
			ok = other.ok;
			error = std::move(other.error);
		}
		return *this;
	}
};

// Evaluates `path` and returns the table it returns. Fails (ok = false) if the
// file can't be read, doesn't compile, raises, or returns something other than
// a table.
DataScript eval_data_script(
		const std::filesystem::path &path, const std::filesystem::path &pack_root);

} // namespace vb::script

#endif // VB_WITH_LUA
