#include "vb/cli/zip.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include <miniz.h>

#include "vb/cli/store.hpp"

namespace vb::cli {

namespace fs = std::filesystem;

namespace {

// Returns the safe relative path for an archive entry name, or nullopt.
std::optional<fs::path> safe_relative(const std::string &name) {
	if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos ||
			name.find('\0') != std::string::npos) {
		return std::nullopt;
	}
	fs::path out;
	std::size_t pos = 0;
	while (pos <= name.size()) {
		const std::size_t next = name.find('/', pos);
		const std::string part = name.substr(pos, next == std::string::npos ? next : next - pos);
		if (part == "..") {
			return std::nullopt;
		}
		if (part.size() >= 2 && part[1] == ':') { // C:... drive letters
			return std::nullopt;
		}
		if (!part.empty() && part != ".") {
			out /= part;
		}
		if (next == std::string::npos) {
			break;
		}
		pos = next + 1;
	}
	if (out.empty()) {
		return std::nullopt;
	}
	return out;
}

bool is_known_binary(const fs::path &rel) {
	if (std::distance(rel.begin(), rel.end()) != 1) {
		return false;
	}
	for (const Binary b : { Binary::Client, Binary::Server, Binary::Editor }) {
		if (rel.string() == binary_file_name(b)) {
			return true;
		}
	}
#if defined(_WIN32)
	return rel.string() == "vb.exe";
#else
	return rel.string() == "vb";
#endif
}

struct ZipGuard {
	mz_zip_archive zip{};
	bool open = false;
	~ZipGuard() {
		if (open) {
			mz_zip_reader_end(&zip);
		}
	}
};

} // namespace

Status extract_zip(const fs::path &zip_path, const fs::path &dest, const ExtractLimits &limits) {
	ZipGuard g;
	if (!mz_zip_reader_init_file(&g.zip, zip_path.string().c_str(), 0)) {
		return { "cannot open archive " + zip_path.string() + " (corrupt or truncated?)" };
	}
	g.open = true;
	const mz_uint count = mz_zip_reader_get_num_files(&g.zip);
	if (count > limits.max_entries) {
		return { "archive has too many entries" };
	}

	// Pass 1: validate every entry before writing anything.
	std::uint64_t total = 0;
	for (mz_uint i = 0; i < count; ++i) {
		mz_zip_archive_file_stat st;
		if (!mz_zip_reader_file_stat(&g.zip, i, &st)) {
			return { "corrupt archive directory" };
		}
		if (!safe_relative(st.m_filename)) {
			return { std::string("unsafe path in archive: '") + st.m_filename + "'" };
		}
		// Unix mode lives in the high 16 bits of external attrs when made by Unix.
		const mz_uint32 mode = st.m_external_attr >> 16;
		if ((mode & 0170000u) == 0120000u) {
			return { std::string("symlink in archive rejected: '") + st.m_filename + "'" };
		}
		total += st.m_uncomp_size;
		if (total > limits.max_total_bytes) {
			return { "archive expands beyond the size limit" };
		}
	}

	// Pass 2: extract.
	std::error_code ec;
	for (mz_uint i = 0; i < count; ++i) {
		mz_zip_archive_file_stat st;
		mz_zip_reader_file_stat(&g.zip, i, &st);
		const fs::path rel = *safe_relative(st.m_filename);
		const fs::path target = dest / rel;
		if (st.m_is_directory) {
			fs::create_directories(target, ec);
			continue;
		}
		fs::create_directories(target.parent_path(), ec);
		if (ec) {
			return { "cannot create " + target.parent_path().string() };
		}
		if (!mz_zip_reader_extract_to_file(&g.zip, i, target.string().c_str(), 0)) {
			return { "failed to extract '" + std::string(st.m_filename) + "' (corrupt archive?)" };
		}
		if (is_known_binary(rel)) {
			fs::permissions(target,
					fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
							fs::perms::others_read | fs::perms::others_exec,
					fs::perm_options::replace, ec);
		} else {
			fs::permissions(target,
					fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
							fs::perms::others_read,
					fs::perm_options::replace, ec);
		}
	}
	return {};
}

} // namespace vb::cli
