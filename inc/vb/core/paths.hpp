#pragma once

#include <filesystem>

// OS-appropriate user directories. Small and dependency-free -- lives in
// vb_core proper rather than any single subsystem.

namespace vb::core {

// Per-user cache directory root (Phase 4.4 asset cache default):
//   Windows: %LOCALAPPDATA%\voxel_browser\cache (falls back to %TEMP%)
//   macOS:   ~/Library/Caches/voxel_browser
//   other:   $XDG_CACHE_HOME/voxel_browser or ~/.cache/voxel_browser
std::filesystem::path user_cache_dir();

} // namespace vb::core
