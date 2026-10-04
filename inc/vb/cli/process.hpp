#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Child-process launching and tracking for `vb launch` / `vb host` /
// `vb server ...` (dev-cli.md §6).

namespace vb::cli {

using Pid = std::uint64_t;

struct RunResult {
	int exit_code = -1; // child's exit status; -1 when it could not be started
	std::string error;  // set when the child could not be started
};

// Runs `exe args...` with `cwd` as working directory (empty = inherit) and
// waits. stdio is inherited. No shell is involved, so arguments need no quoting.
// `on_start` (optional) is called with the child's pid right after it started.
RunResult run_foreground(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd = {},
		const std::function<void(Pid)> &on_start = {});

struct SpawnResult {
	Pid pid = 0;
	std::string error; // set when the child could not be started
};

// Starts `exe args...` fully detached: its own session / process group, no
// stdin, stdout+stderr appended to `log_file` (parent dirs are created). It
// keeps running after this process and its terminal go away, and is never a
// zombie of this process.
SpawnResult spawn_detached(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd,
		const std::filesystem::path &log_file);

// Identifies one incarnation of a pid: a platform-specific process start time.
// nullopt when no such process is running (a zombie counts as not running).
// Recording {pid, token} and comparing later survives pid reuse.
std::optional<std::uint64_t> process_start_token(Pid pid);

// Asks the process to shut down gracefully. POSIX: SIGTERM. Windows: there is
// no signal for a detached process, so this returns false and callers rely on
// the server's --stop-file instead. True when the request was delivered.
bool request_stop(Pid pid);

// Absolute path of the running executable; empty if it cannot be determined.
std::filesystem::path current_executable_path();

// Ends the process immediately (SIGKILL / TerminateProcess).
bool force_kill(Pid pid);

} // namespace vb::cli
