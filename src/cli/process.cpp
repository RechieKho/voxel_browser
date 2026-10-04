#include "vb/cli/process.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
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

} // namespace

RunResult run_foreground(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd) {
	std::wstring cmd = quote_arg(exe.wstring());
	for (const std::string &a : args) {
		cmd += L' ';
		cmd += quote_arg(widen(a));
	}
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const std::wstring wcwd = cwd.wstring();
	if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
				wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi)) {
		return { -1, "cannot start " + exe.string() + " (error " +
				std::to_string(GetLastError()) + ")" };
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

#else

RunResult run_foreground(const std::filesystem::path &exe,
		const std::vector<std::string> &args, const std::filesystem::path &cwd) {
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

	// Ctrl+C goes to the whole foreground group; let the child handle it (the
	// server saves the world on SIGINT) and just keep waiting.
	struct sigaction ignore{}, old_int{}, old_quit{};
	ignore.sa_handler = SIG_IGN;
	sigaction(SIGINT, &ignore, &old_int);
	sigaction(SIGQUIT, &ignore, &old_quit);
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
	sigaction(SIGINT, &old_int, nullptr);
	sigaction(SIGQUIT, &old_quit, nullptr);

	if (got == static_cast<ssize_t>(sizeof(child_errno))) {
		return { -1, "cannot start " + exe.string() + ": " + std::strerror(child_errno) };
	}
	if (WIFEXITED(status)) {
		return { WEXITSTATUS(status), {} };
	}
	return { 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0), {} };
}

#endif

} // namespace vb::cli
