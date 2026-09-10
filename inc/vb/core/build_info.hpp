#pragma once

#include <string>

#include "vb/core/version.hpp" // generated: cmake/version.hpp.in

// Human-readable build identification for --version output and log banners.

namespace vb::core {

// e.g. "voxel_browser v0.1.0-3-gabc1234 (protocol 1, built 2026-09-10T12:00:00Z)"
std::string describe_build();

} // namespace vb::core
