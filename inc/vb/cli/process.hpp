#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Minimal child-process launching for `vb launch`/`vb host`.

namespace vb::cli {

struct RunResult {
	int exit_code = -1; // child's exit status; -1 when it could not be started
	std::string error;  // set when the child could not be started
};

// Runs `exe args...` with `cwd` as working directory (empty = inherit) and
// waits. stdio is inherited. No shell is involved, so arguments need no quoting.
RunResult run_foreground(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd = {});

} // namespace vb::cli
