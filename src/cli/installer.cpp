#include "vb/cli/installer.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#include "vb/cli/lock.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/store.hpp"
#include "vb/cli/version.hpp"
#include "vb/cli/zip.hpp"
#include "vb/core/sha256.hpp"

namespace vb::cli {

namespace fs = std::filesystem;

namespace {

bool read_all(const fs::path &p, std::string &out) {
	std::ifstream in(p, std::ios::binary);
	if (!in) {
		return false;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

} // namespace

Status verify_archive(const fs::path &file, const ArtifactInfo &a) {
	std::error_code ec;
	const auto size = fs::file_size(file, ec);
	if (ec || size != a.size) {
		return { "size mismatch for " + a.file + " (expected " + std::to_string(a.size) +
				" bytes, got " + (ec ? std::string("?") : std::to_string(size)) + ")" };
	}
	std::string bytes;
	if (!read_all(file, bytes)) {
		return { "cannot read " + file.string() };
	}
	if (vb::core::sha256_hex(bytes) != a.sha256) {
		return { "SHA-256 mismatch for " + a.file + "; refusing to install" };
	}
	return {};
}

namespace {

std::string random_suffix() {
	std::random_device rd;
	std::ostringstream os;
	os << std::hex << rd();
	return os.str();
}

// Removes a directory tree on scope exit unless released.
struct DirCleanup {
	fs::path path;
	bool armed = true;
	~DirCleanup() {
		if (armed && !path.empty()) {
			std::error_code ec;
			fs::remove_all(path, ec);
		}
	}
};

std::string iso_now() {
	const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm tm{};
#if defined(_WIN32)
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
	return buf;
}

} // namespace

InstallResult install_release(const Layout &layout, Source &source,
		std::string_view version_or_latest, const InstallOptions &opts) {
	InstallResult res;
	const auto fail = [&](std::string msg) {
		res.status = { std::move(msg) };
		return res;
	};
	if (opts.build != "release" && opts.build != "debug") {
		return fail("--build must be 'release' or 'debug'");
	}
	const std::string platform = opts.platform.empty() ? current_platform() : opts.platform;
	if (platform.empty()) {
		return fail("this platform has no published Voxel Browser builds");
	}
	std::string wanted(version_or_latest);
	if (wanted != "latest") {
		const auto v = parse_version(wanted);
		if (!v) {
			return fail("'" + wanted + "' is not a version (use vX.Y.Z or latest)");
		}
		wanted = to_tag(*v);
	}

	std::unique_ptr<FileLock> lock;
	if (const Status s = FileLock::acquire(layout.lock_file(), opts.wait_for_lock, lock); !s) {
		return fail(s.error);
	}

	std::string manifest_text;
	if (const Status s = source.fetch_manifest(wanted, manifest_text); !s) {
		return fail(s.error);
	}
	ReleaseManifest manifest;
	if (const Status s = parse_manifest(manifest_text, manifest); !s) {
		return fail(s.error);
	}
	if (wanted != "latest" && manifest.version != wanted) {
		return fail("source returned " + manifest.version + " when " + wanted + " was requested");
	}
	res.version = manifest.version;

	const fs::path final_dir = layout.version_dir(manifest.version);
	std::error_code ec;
	if (fs::exists(final_dir, ec)) {
		if (!opts.force) {
			res.already_installed = true;
			return res;
		}
	}
	const ArtifactInfo *art = select_artifact(manifest, "game", platform, opts.build);
	if (art == nullptr) {
		return fail(manifest.version + " has no " + opts.build + " build for " + platform);
	}

	// Download (resumable) -> verify.
	fs::create_directories(layout.downloads_dir(), ec);
	const fs::path archive = layout.downloads_dir() / art->file;
	fs::path part = archive;
	part += ".part";
	Status verified;
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (const Status s = source.fetch_file(manifest.version, art->file, part, opts.progress); !s) {
			return fail(s.error); // keep .part for resume
		}
		verified = verify_archive(part, *art);
		if (verified) {
			break;
		}
		fs::remove(part, ec); // a bad (possibly stale-resumed) file is never reused
	}
	if (!verified) {
		return fail(verified.error);
	}
	fs::rename(part, archive, ec);
	if (ec) {
		return fail("cannot finalize download: " + ec.message());
	}

	// Extract into staging, validate, then commit with one rename.
	fs::create_directories(layout.versions_dir(), ec);
	const fs::path staging =
			layout.versions_dir() / (".staging-" + manifest.version + "-" + random_suffix());
	DirCleanup cleanup{ staging };
	fs::create_directories(staging, ec);
	if (const Status s = extract_zip(archive, staging); !s) {
		fs::remove(archive, ec); // a corrupt archive that hashes right is still unusable
		return fail(s.error);
	}
	Entry probe;
	probe.root = staging;
	const auto server = find_binary(probe, Binary::Server);
	const auto client = find_binary(probe, Binary::Client);
	if (!server || !client) {
		return fail("archive is missing " + binary_file_name(Binary::Client) + " or " +
				binary_file_name(Binary::Server));
	}
	if (opts.run_version_check) {
		const RunResult r = run_foreground(*server, { "--version" }, staging);
		if (!r.error.empty() || r.exit_code != 0) {
			return fail("the installed server does not run (" +
					(r.error.empty() ? "exit " + std::to_string(r.exit_code) : r.error) + ")");
		}
	}
	{
		std::ofstream receipt(staging / ".install.toml", std::ios::binary);
		receipt << "version = \"" << manifest.version << "\"\n"
				<< "commit = \"" << manifest.commit << "\"\n"
				<< "build = \"" << opts.build << "\"\n"
				<< "platform = \"" << platform << "\"\n"
				<< "source = \"" << source.describe() << "\"\n"
				<< "archive = \"" << art->file << "\"\n"
				<< "sha256 = \"" << art->sha256 << "\"\n"
				<< "installed_at = \"" << iso_now() << "\"\n";
		if (!receipt) {
			return fail("cannot write install receipt");
		}
	}

	if (opts.force && fs::exists(final_dir, ec)) {
		fs::remove_all(final_dir, ec);
		if (ec) {
			return fail("cannot replace " + final_dir.string() + ": " + ec.message());
		}
	}
	fs::rename(staging, final_dir, ec);
	if (ec) {
		return fail("cannot commit install: " + ec.message());
	}
	cleanup.armed = false;
	if (!opts.keep_download) {
		fs::remove(archive, ec);
	}
	return res;
}

Status prune_releases(const Layout &layout, int keep, std::vector<std::string> &removed,
		const std::set<std::string> &protected_versions, std::vector<std::string> *skipped) {
	if (keep < 0) {
		return { "--keep must be >= 0" };
	}
	std::unique_ptr<FileLock> lock;
	if (const Status s = FileLock::acquire(layout.lock_file(), true, lock); !s) {
		return s;
	}
	int seen = 0;
	for (const Entry &e : list_entries(layout)) { // releases first, newest first
		if (e.kind != EntryKind::Release) {
			continue;
		}
		if (++seen <= keep || e.is_default) {
			continue;
		}
		if (protected_versions.count(e.name) != 0) {
			if (skipped != nullptr) {
				skipped->push_back(e.name);
			}
			continue;
		}
		std::error_code ec;
		fs::remove_all(e.root, ec);
		if (ec) {
			return { "cannot remove " + e.root.string() + ": " + ec.message() };
		}
		removed.push_back(e.name);
	}
	// Leftovers of crashed installs.
	std::error_code ec;
	for (fs::directory_iterator it(layout.versions_dir(), ec), end; !ec && it != end;
			it.increment(ec)) {
		if (it->path().filename().string().rfind(".staging-", 0) == 0) {
			std::error_code rec;
			fs::remove_all(it->path(), rec);
		}
	}
	return {};
}

} // namespace vb::cli
