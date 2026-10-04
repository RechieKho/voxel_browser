#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "vb/cli/layout.hpp"

namespace vb::cli {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1; // operational failure
constexpr int kExitUsage = 2;   // bad command line

// Runs `vb <args...>` (args excludes argv[0]) against `layout`. Output goes to
// the given streams so tests can capture it.
int run_cli(const std::vector<std::string> &args, const Layout &layout, std::ostream &out,
		std::ostream &err);

} // namespace vb::cli
