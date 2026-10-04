#include "vb/cli/self_update.hpp"

#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <system_error>

#include "vb/cli/installer.hpp"
#include "vb/cli/lock.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/release_manifest.hpp"
#include "vb/cli/store.hpp"
#include "vb/cli/version.hpp"
#include "vb/cli/zip.hpp"

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace vb::cli {

namespace fs = std::filesystem;

namespace {

struct DirCleanup {
	fs::path path;
	~DirCleanup() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

// Puts `fresh` in place of `target`. POSIX: rename over it (atomic, fine for a
// running executable). Windows: a running .exe cannot be overwritten or
// deleted but can be renamed, so move it aside as <target>.old (removed by the
// next vb start) and rename the new one in, restoring on failure.
Status swap_executable(const fs::path &target, const fs::path &fresh) {
	std::error_code ec;
#if defined(_WIN32)
	fs::path old = target;
	old += ".old";
	fs::remove(old, ec);
	fs::rename(target, old, ec);
	if (ec) {
		return { "cannot move " + target.string() + " aside: " + ec.message() };
	}
	fs::rename(fresh, target, ec);
	if (ec) {
		std::error_code undo;
		fs::rename(old, target, undo);
		return { "cannot put the new vb in place: " + ec.message() };
	}
	return {};
#else
	fs::rename(fresh, target, ec);
	if (ec) {
		return { "cannot replace " + target.string() + ": " + ec.message() };
	}
	return {};
#endif
}

} // namespace

SelfUpdateResult self_update(const Layout &layout, Source &source, const SelfUpdateOptions &opts) {
	SelfUpdateResult res;
	const auto fail = [&](std::string msg) {
		res.status = { std::move(msg) };
		return res;
	};
	const std::string platform = opts.platform.empty() ? current_platform() : opts.platform;
	if (platform.empty()) {
		return fail("this platform has no published vb builds");
	}
	fs::path target = opts.target.empty() ? current_executable_path() : opts.target;
	if (target.empty()) {
		return fail("cannot determine where the running vb lives");
	}

	std::string text;
	if (const Status s = source.fetch_manifest("latest", text); !s) {
		return fail(s.error);
	}
	ReleaseManifest manifest;
	if (const Status s = parse_manifest(text, manifest); !s) {
		return fail(s.error);
	}
	res.latest = manifest.version;

	const auto current = parse_version(opts.current_version);
	const auto latest = parse_version(manifest.version);
	if (!current && !opts.force) {
		return fail("this vb is a development build (" + opts.current_version +
				"); pass --force to replace it with " + manifest.version);
	}
	if (current && latest && !(*current < *latest)) {
		res.up_to_date = true;
		if (!opts.force) {
			return res;
		}
	}
	if (opts.check_only) {
		return res;
	}

	const ArtifactInfo *art = select_artifact(manifest, "cli", platform, "release");
	if (art == nullptr) {
		return fail(manifest.version + " has no vb download for " + platform);
	}

	std::unique_ptr<FileLock> lock;
	if (const Status s = FileLock::acquire(layout.lock_file(), true, lock); !s) {
		return fail(s.error);
	}
	std::error_code ec;
	fs::create_directories(layout.downloads_dir(), ec);
	const fs::path archive = layout.downloads_dir() / art->file;
	fs::path part = archive;
	part += ".part";
	Status verified;
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (const Status s = source.fetch_file(manifest.version, art->file, part, opts.progress); !s) {
			return fail(s.error);
		}
		verified = verify_archive(part, *art);
		if (verified) {
			break;
		}
		fs::remove(part, ec);
	}
	if (!verified) {
		return fail(verified.error);
	}
	fs::rename(part, archive, ec);
	if (ec) {
		return fail("cannot finalize download: " + ec.message());
	}

	std::random_device rd;
	std::ostringstream suffix;
	suffix << std::hex << rd();
	const fs::path staging = layout.downloads_dir() / (".self-" + suffix.str());
	DirCleanup cleanup{ staging };
	fs::create_directories(staging, ec);
	if (const Status s = extract_zip(archive, staging); !s) {
		fs::remove(archive, ec);
		return fail(s.error);
	}
	fs::remove(archive, ec);
	const fs::path extracted = staging / target.filename();
	if (!fs::is_regular_file(extracted, ec)) {
		return fail("the vb archive does not contain " + target.filename().string());
	}
	if (opts.run_version_check) {
		const RunResult r = run_foreground(extracted, { "--version" }, staging);
		if (!r.error.empty() || r.exit_code != 0) {
			return fail("the downloaded vb does not run (" +
					(r.error.empty() ? "exit " + std::to_string(r.exit_code) : r.error) +
					"); keeping the current one");
		}
	}

	// Stage next to the target (same filesystem => rename is atomic).
	fs::path beside = target;
	beside += ".new";
	fs::remove(beside, ec);
	fs::copy_file(extracted, beside, fs::copy_options::overwrite_existing, ec);
	if (ec) {
		return fail("cannot write " + beside.string() + ": " + ec.message());
	}
#if !defined(_WIN32)
	chmod(beside.c_str(), 0755);
#endif
	if (const Status s = swap_executable(target, beside); !s) {
		fs::remove(beside, ec);
		return fail(s.error);
	}
	res.updated = true;
	return res;
}

} // namespace vb::cli
