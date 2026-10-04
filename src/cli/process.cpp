#include "vb/cli/process.hpp"

#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <sstream>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#endif

namespace vb::cli {

#if defined(_WIN32)

namespace {

// Quotes one argument per the MSVCRT/CommandLineToArgvW rules.
std::wstring quote_arg(const std::wstring &a) {
	if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		return a;
	}
	std::wstring out = L"\"";
	for (std::size_t i = 0;; ++i) {
		std::size_t slashes = 0;
		while (i < a.size() && a[i] == L'\\') {
			++slashes;
			++i;
		}
		if (i == a.size()) {
			out.append(slashes * 2, L'\\');
			break;
		}
		if (a[i] == L'"') {
			out.append(slashes * 2 + 1, L'\\');
		} else {
			out.append(slashes, L'\\');
		}
		out += a[i];
	}
	return out + L"\"";
}

std::wstring widen(const std::string &s) {
	if (s.empty()) {
		return {};
	}
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(static_cast<std::size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
	return w;
}

std::wstring build_command_line(const std::filesystem::path &exe,
		const std::vector<std::string> &args) {
	std::wstring cmd = quote_arg(exe.wstring());
	for (const std::string &a : args) {
		cmd += L' ';
		cmd += quote_arg(widen(a));
	}
	return cmd;
}

} // namespace

RunResult run_foreground(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd,
		const std::function<void(Pid)> &on_start) {
	std::wstring cmd = build_command_line(exe, args);
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const std::wstring wcwd = cwd.wstring();
	if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
				wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi)) {
		return { -1, "cannot start " + exe.string() + " (error " +
				std::to_string(GetLastError()) + ")" };
	}
	if (on_start) {
		on_start(pi.dwProcessId);
	}
	// Ctrl+C reaches the child too (shared console); don't die before it saves.
	SetConsoleCtrlHandler(nullptr, TRUE);
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	SetConsoleCtrlHandler(nullptr, FALSE);
	return { static_cast<int>(code), {} };
}

SpawnResult spawn_detached(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd,
		const std::filesystem::path &log_file) {
	std::error_code ec;
	std::filesystem::create_directories(log_file.parent_path(), ec);

	SECURITY_ATTRIBUTES inheritable{};
	inheritable.nLength = sizeof(inheritable);
	inheritable.bInheritHandle = TRUE;
	HANDLE log = CreateFileW(log_file.c_str(), FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
	if (log == INVALID_HANDLE_VALUE) {
		return { 0, "cannot open log file " + log_file.string() };
	}
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nul;
	si.hStdOutput = log;
	si.hStdError = log;
	PROCESS_INFORMATION pi{};
	std::wstring cmd = build_command_line(exe, args);
	const std::wstring wcwd = cwd.wstring();
	// DETACHED_PROCESS: no console of its own, so closing vb's terminal can't
	// take it down; NEW_PROCESS_GROUP: a Ctrl+C in that terminal isn't delivered.
	const BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
			DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
			wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
	const DWORD err = GetLastError();
	CloseHandle(log);
	if (nul != INVALID_HANDLE_VALUE) {
		CloseHandle(nul);
	}
	if (!ok) {
		return { 0, "cannot start " + exe.string() + " (error " + std::to_string(err) + ")" };
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return { pi.dwProcessId, {} };
}

std::optional<std::uint64_t> process_start_token(Pid pid) {
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
	if (h == nullptr) {
		return std::nullopt;
	}
	DWORD code = 0;
	FILETIME created{}, exited{}, kernel{}, user{};
	const bool ok = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE &&
			GetProcessTimes(h, &created, &exited, &kernel, &user);
	CloseHandle(h);
	if (!ok) {
		return std::nullopt;
	}
	return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

bool request_stop(Pid) {
	return false; // see header: the server's --stop-file is the Windows path
}

bool force_kill(Pid pid) {
	HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
	if (h == nullptr) {
		return false;
	}
	const bool ok = TerminateProcess(h, 1) != 0;
	CloseHandle(h);
	return ok;
}

#else

namespace {

volatile sig_atomic_t g_forward_to = 0; // child pid while run_foreground waits

extern "C" void forward_signal(int sig) {
	const pid_t child = static_cast<pid_t>(g_forward_to);
	if (child > 0) {
		kill(child, sig);
	}
}

} // namespace

RunResult run_foreground(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd,
		const std::function<void(Pid)> &on_start) {
	std::vector<std::string> storage;
	storage.push_back(exe.string());
	storage.insert(storage.end(), args.begin(), args.end());
	std::vector<char *> argv;
	for (std::string &s : storage) {
		argv.push_back(s.data());
	}
	argv.push_back(nullptr);

	// Reports exec/chdir failure through a close-on-exec pipe.
	int err_pipe[2];
	if (pipe(err_pipe) != 0) {
		return { -1, std::string("pipe: ") + std::strerror(errno) };
	}
	for (const int fd : err_pipe) {
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	}
	const pid_t pid = fork();
	if (pid < 0) {
		close(err_pipe[0]);
		close(err_pipe[1]);
		return { -1, std::string("fork: ") + std::strerror(errno) };
	}
	if (pid == 0) {
		if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
			const int e = errno;
			(void)!write(err_pipe[1], &e, sizeof(e));
			_exit(127);
		}
		execv(argv[0], argv.data());
		const int e = errno;
		(void)!write(err_pipe[1], &e, sizeof(e));
		_exit(127);
	}
	close(err_pipe[1]);
	int child_errno = 0;
	const ssize_t got = read(err_pipe[0], &child_errno, sizeof(child_errno));
	close(err_pipe[0]);
	if (on_start && got != static_cast<ssize_t>(sizeof(child_errno))) {
		on_start(static_cast<Pid>(pid));
	}

	// A terminal Ctrl+C goes to the whole foreground group, so the child already
	// has it; but a `kill <vb pid>` or a closing terminal (SIGHUP) reaches only
	// us, so forward those to the child (the server saves the world on them) and
	// keep waiting rather than dying and orphaning it. A doubled SIGINT is harmless.
	g_forward_to = pid;
	struct sigaction forward {
	}, old_int{}, old_term{}, old_hup{}, old_quit{}, ignore{};
	forward.sa_handler = forward_signal;
	sigemptyset(&forward.sa_mask);
	ignore.sa_handler = SIG_IGN;
	sigaction(SIGINT, &forward, &old_int);
	sigaction(SIGTERM, &forward, &old_term);
	sigaction(SIGHUP, &forward, &old_hup);
	sigaction(SIGQUIT, &ignore, &old_quit);
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
	g_forward_to = 0;
	sigaction(SIGINT, &old_int, nullptr);
	sigaction(SIGTERM, &old_term, nullptr);
	sigaction(SIGHUP, &old_hup, nullptr);
	sigaction(SIGQUIT, &old_quit, nullptr);

	if (got == static_cast<ssize_t>(sizeof(child_errno))) {
		return { -1, "cannot start " + exe.string() + ": " + std::strerror(child_errno) };
	}
	if (WIFEXITED(status)) {
		return { WEXITSTATUS(status), {} };
	}
	return { 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0), {} };
}

SpawnResult spawn_detached(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd,
		const std::filesystem::path &log_file) {
	std::error_code ec;
	std::filesystem::create_directories(log_file.parent_path(), ec);
	// Opened here (not in the child) so a bad log path is reported to the caller.
	const int log_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (log_fd < 0) {
		return { 0, "cannot open log file " + log_file.string() + ": " + std::strerror(errno) };
	}
	const int null_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);

	std::vector<std::string> storage;
	storage.push_back(exe.string());
	storage.insert(storage.end(), args.begin(), args.end());
	std::vector<char *> argv;
	for (std::string &a : storage) {
		argv.push_back(a.data());
	}
	argv.push_back(nullptr);

	// pid_pipe: the intermediate child reports the grandchild's pid.
	// err_pipe: close-on-exec, so it reads empty iff exec succeeded.
	int pid_pipe[2];
	int err_pipe[2];
	if (pipe(pid_pipe) != 0 || pipe(err_pipe) != 0) {
		close(log_fd);
		if (null_fd >= 0) {
			close(null_fd);
		}
		return { 0, std::string("pipe: ") + std::strerror(errno) };
	}
	fcntl(err_pipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

	const pid_t middle = fork();
	if (middle < 0) {
		return { 0, std::string("fork: ") + std::strerror(errno) };
	}
	if (middle == 0) {
		// Double fork + setsid: the daemon is reparented to init, never becomes
		// our zombie, and has no controlling terminal to receive SIGHUP from.
		setsid();
		const pid_t daemon = fork();
		if (daemon < 0) {
			_exit(1);
		}
		if (daemon > 0) {
			const auto reported = static_cast<Pid>(daemon);
			(void)!write(pid_pipe[1], &reported, sizeof(reported));
			_exit(0);
		}
		if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
			const int e = errno;
			(void)!write(err_pipe[1], &e, sizeof(e));
			_exit(127);
		}
		if (null_fd >= 0) {
			dup2(null_fd, STDIN_FILENO);
		}
		dup2(log_fd, STDOUT_FILENO);
		dup2(log_fd, STDERR_FILENO);
		// Nothing else inherited from vb (notably flock-held lock files, which
		// a leaked descriptor would keep locked for the server's lifetime).
		for (int fd = 3; fd < 1024; ++fd) {
			if (fd != err_pipe[1]) {
				close(fd);
			}
		}
		signal(SIGINT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		execv(argv[0], argv.data());
		const int e = errno;
		(void)!write(err_pipe[1], &e, sizeof(e));
		_exit(127);
	}

	close(log_fd);
	if (null_fd >= 0) {
		close(null_fd);
	}
	close(pid_pipe[1]);
	close(err_pipe[1]);
	int status = 0;
	while (waitpid(middle, &status, 0) < 0 && errno == EINTR) {
	}
	Pid pid = 0;
	const ssize_t got_pid = read(pid_pipe[0], &pid, sizeof(pid));
	close(pid_pipe[0]);
	int child_errno = 0;
	ssize_t got_err = 0;
	do {
		got_err = read(err_pipe[0], &child_errno, sizeof(child_errno));
	} while (got_err < 0 && errno == EINTR);
	close(err_pipe[0]);

	if (got_pid != static_cast<ssize_t>(sizeof(pid))) {
		return { 0, "cannot start " + exe.string() + ": fork failed" };
	}
	if (got_err == static_cast<ssize_t>(sizeof(child_errno))) {
		return { 0, "cannot start " + exe.string() + ": " + std::strerror(child_errno) };
	}
	return { pid, {} };
}

std::optional<std::uint64_t> process_start_token(Pid pid) {
#if defined(__linux__)
	// /proc/<pid>/stat: "pid (comm) S ppid ... starttime(22nd field) ...". comm
	// may contain spaces/parens, so parse from the last ')'.
	std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
	std::string line;
	if (!f || !std::getline(f, line)) {
		return std::nullopt;
	}
	const std::size_t close_paren = line.rfind(')');
	if (close_paren == std::string::npos) {
		return std::nullopt;
	}
	std::istringstream rest(line.substr(close_paren + 1));
	std::string field;
	for (int i = 3; i <= 22 && (rest >> field); ++i) {
		if (i == 3 && (field == "Z" || field == "X")) {
			return std::nullopt; // exited, just not reaped yet
		}
		if (i == 22) {
			try {
				return static_cast<std::uint64_t>(std::stoull(field));
			} catch (...) {
				return std::nullopt;
			}
		}
	}
	return std::nullopt;
#elif defined(__APPLE__)
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(pid) };
	struct kinfo_proc info {};
	std::size_t size = sizeof(info);
	if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size == 0 ||
			info.kp_proc.p_pid != static_cast<pid_t>(pid) || info.kp_proc.p_stat == SZOMB) {
		return std::nullopt;
	}
	const auto &t = info.kp_proc.p_starttime;
	return static_cast<std::uint64_t>(t.tv_sec) * 1000000ull + static_cast<std::uint64_t>(t.tv_usec);
#else
	// No portable start time: liveness only (pid-reuse guard is weaker here).
	if (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM) {
		return 1;
	}
	return std::nullopt;
#endif
}

bool request_stop(Pid pid) {
	return kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

bool force_kill(Pid pid) {
	return kill(static_cast<pid_t>(pid), SIGKILL) == 0;
}

#endif

} // namespace vb::cli
