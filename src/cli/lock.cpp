#include "vb/cli/lock.hpp"

#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace vb::cli {

struct FileLock::Impl {
#if defined(_WIN32)
	HANDLE handle = INVALID_HANDLE_VALUE;
#else
	int fd = -1;
#endif
};

Status FileLock::acquire(const std::filesystem::path &file, bool wait,
		std::unique_ptr<FileLock> &out) {
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	auto lock = std::unique_ptr<FileLock>(new FileLock());
	lock->impl_ = std::make_unique<Impl>();
#if defined(_WIN32)
	HANDLE h = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return { "cannot open lock file " + file.string() };
	}
	OVERLAPPED ov{};
	const DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY);
	if (!LockFileEx(h, flags, 0, 1, 0, &ov)) {
		CloseHandle(h);
		return { "another vb command is running (lock " + file.string() + ")" };
	}
	lock->impl_->handle = h;
#else
	const int fd = open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0) {
		return { "cannot open lock file " + file.string() };
	}
	if (flock(fd, LOCK_EX | (wait ? 0 : LOCK_NB)) != 0) {
		close(fd);
		return { "another vb command is running (lock " + file.string() + ")" };
	}
	lock->impl_->fd = fd;
#endif
	out = std::move(lock);
	return {};
}

FileLock::~FileLock() {
	if (!impl_) {
		return;
	}
#if defined(_WIN32)
	if (impl_->handle != INVALID_HANDLE_VALUE) {
		CloseHandle(impl_->handle); // releases the lock
	}
#else
	if (impl_->fd >= 0) {
		close(impl_->fd); // releases the flock
	}
#endif
}

} // namespace vb::cli
