#pragma once

#include <filesystem>

// OS-appropriate user directories. Small and dependency-free -- lives in
// vb_core proper rather than any single subsystem.

namespace vb::core {

// `VB_HOME=<dir>` (when set and non-empty) overrides all three roots below, for
// portable installs, CI and tests: data = <dir>, config = <dir>/config,
// cache = <dir>/cache.

// Per-user data root (installed versions, server instances, worlds -- the
// developer CLI's tree, see architecture_spec/dev-cli.md §5):
//   Windows: %LOCALAPPDATA%\voxel_browser (falls back to %TEMP%)
//   macOS:   ~/Library/Application Support/voxel_browser
//   other:   $XDG_DATA_HOME/voxel_browser or ~/.local/share/voxel_browser
std::filesystem::path user_data_dir();

// Per-user config root (cli.toml, client.toml):
//   Windows: %APPDATA%\voxel_browser (falls back to %TEMP%)
//   macOS:   ~/Library/Application Support/voxel_browser/config
//   other:   $XDG_CONFIG_HOME/voxel_browser or ~/.config/voxel_browser
std::filesystem::path user_config_dir();

// Per-user cache directory root (Phase 4.4 asset cache default):
//   Windows: %LOCALAPPDATA%\voxel_browser\cache (falls back to %TEMP%)
//   macOS:   ~/Library/Caches/voxel_browser
//   other:   $XDG_CACHE_HOME/voxel_browser or ~/.cache/voxel_browser
std::filesystem::path user_cache_dir();

// `relative` unchanged when it exists relative to the working directory;
// otherwise `<directory of program>/<relative>` when that exists; otherwise
// `relative` unchanged (so a later "not found" error names what the user
// would expect). Lets an installed client find its bundled `content/base`
// when launched from any working directory. `program` is argv[0].
std::filesystem::path resolve_beside_program(const std::filesystem::path &relative,
		const std::filesystem::path &program);

} // namespace vb::core
