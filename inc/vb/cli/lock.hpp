#pragma once

#include <filesystem>
#include <memory>

#include "vb/cli/store.hpp" // Status

namespace vb::cli {

// Advisory exclusive lock on a file (flock / LockFileEx): released when the
// object dies *or the process exits*, so a crash never leaves a stale lock.
class FileLock {
public:
	// wait=false fails immediately if another process holds it.
	static Status acquire(const std::filesystem::path &file, bool wait,
			std::unique_ptr<FileLock> &out);
	~FileLock();
	FileLock(const FileLock &) = delete;
	FileLock &operator=(const FileLock &) = delete;

private:
	FileLock() = default;
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace vb::cli
