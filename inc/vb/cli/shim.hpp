#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vb/cli/layout.hpp"
#include "vb/cli/store.hpp"

// `vb shim install`: tiny launcher scripts in <data>/bin so `voxel_browser` and
// `voxel_browser_server` work from any shell (once <data>/bin is on PATH) and
// always run the *default* version -- no PATH edits when a version changes.

namespace vb::cli {

std::filesystem::path shim_dir(const Layout &layout);

// Writes the shims pointing at `vb_exe`; returns the files created.
Status install_shims(const Layout &layout, const std::filesystem::path &vb_exe,
		std::vector<std::filesystem::path> &created);

// Removes the shims `install_shims` creates (only those; never other files).
Status remove_shims(const Layout &layout, std::vector<std::filesystem::path> &removed);

// Script text, exposed for tests. `server` = the server shim, else the client.
std::string shim_script(const std::filesystem::path &vb_exe, bool server, bool windows);

} // namespace vb::cli
