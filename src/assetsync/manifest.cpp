#include "vb/assetsync/manifest.hpp"

#if !VB_WITH_COMPRESSION

// Stub build: no xxHash, no manifest support. Every entry point reports
// kDisabled -- the whole asset-sync feature degrades gracefully (a server
// never advertises a manifest, a client never enters the sync states).

namespace vb::assetsync {

const AssetEntry *Manifest::find(core::AssetHash h) const {
	for (const auto &e : entries) {
		if (e.hash == h) {
			return &e;
		}
	}
	return nullptr;
}

core::Result<Manifest, core::AssetSyncError> build_manifest(
		const std::filesystem::path &, AssetSizeCaps) {
	return core::Err{ core::AssetSyncError::kDisabled };
}

core::AssetHash hash_bytes(std::span<const std::byte>) { return {}; }

} // namespace vb::assetsync

#else

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#include <xxhash.h>

#include "vb/protocol/byte_buffer.hpp"

namespace vb::assetsync {

using core::Err;

const AssetEntry *Manifest::find(core::AssetHash h) const {
	for (const auto &e : entries) {
		if (e.hash == h) {
			return &e;
		}
	}
	return nullptr;
}

core::AssetHash hash_bytes(std::span<const std::byte> data) {
	const XXH128_hash_t h = XXH3_128bits(data.data(), data.size());
	return { h.low64, h.high64 };
}

namespace {

// Path-component-wise prefix check -- a naive string prefix compare would
// wrongly accept "/pack2/x" as "under" "/pack" since it's a string prefix
// without a separator boundary.
bool is_under_root(const std::filesystem::path &root,
		const std::filesystem::path &target) {
	auto root_it = root.begin();
	auto target_it = target.begin();
	for (; root_it != root.end(); ++root_it, ++target_it) {
		if (target_it == target.end() || *target_it != *root_it) {
			return false;
		}
	}
	return true;
}

std::string lower_ext(const std::filesystem::path &p) {
	std::string ext = p.extension().string();
	for (char &c : ext) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return ext;
}

AssetKind kind_for(const std::filesystem::path &rel_path) {
	const std::string ext = lower_ext(rel_path);
	if (ext == ".lua") {
		return AssetKind::kScript;
	}
	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds") {
		return AssetKind::kTexture;
	}
	if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
		return AssetKind::kModel;
	}
	if (ext == ".ogg" || ext == ".wav") {
		return AssetKind::kSound;
	}
	if (rel_path.generic_string().rfind("ui/", 0) == 0) {
		return AssetKind::kUi;
	}
	return AssetKind::kData;
}

} // namespace

core::Result<Manifest, core::AssetSyncError> build_manifest(
		const std::filesystem::path &pack_root, AssetSizeCaps caps) {
	std::error_code ec;
	if (!std::filesystem::is_directory(pack_root, ec) || ec) {
		return Err{ core::AssetSyncError::kIoError };
	}
	const std::filesystem::path root_canonical =
			std::filesystem::canonical(pack_root, ec);
	if (ec) {
		return Err{ core::AssetSyncError::kIoError };
	}

	Manifest manifest;
	std::uint64_t running_total = 0;

	std::filesystem::recursive_directory_iterator it(pack_root,
			std::filesystem::directory_options::skip_permission_denied, ec);
	if (ec) {
		return Err{ core::AssetSyncError::kIoError };
	}
	const std::filesystem::recursive_directory_iterator end;

	for (; it != end; it.increment(ec)) {
		if (ec) {
			return Err{ core::AssetSyncError::kIoError };
		}
		const std::filesystem::directory_entry &entry = *it;

		const bool is_link = entry.is_symlink(ec);
		if (ec) {
			return Err{ core::AssetSyncError::kIoError };
		}
		if (is_link) {
			const std::filesystem::path resolved =
					std::filesystem::canonical(entry.path(), ec);
			if (ec) {
				return Err{ core::AssetSyncError::kIoError }; // dangling symlink
			}
			if (!is_under_root(root_canonical, resolved)) {
				return Err{ core::AssetSyncError::kPathEscape };
			}
		}

		const bool regular = entry.is_regular_file(ec);
		if (ec) {
			return Err{ core::AssetSyncError::kIoError };
		}
		if (!regular) {
			continue;
		}

		const std::uintmax_t file_size = entry.file_size(ec);
		if (ec) {
			return Err{ core::AssetSyncError::kIoError };
		}
		if (file_size > caps.max_file_bytes) {
			return Err{ core::AssetSyncError::kFileTooLarge };
		}
		running_total += file_size;
		if (running_total > caps.max_total_bytes) {
			return Err{ core::AssetSyncError::kPackTooLarge };
		}

		std::ifstream f(entry.path(), std::ios::binary);
		if (!f) {
			return Err{ core::AssetSyncError::kIoError };
		}
		std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
		if (file_size > 0) {
			f.read(reinterpret_cast<char *>(bytes.data()),
					static_cast<std::streamsize>(file_size));
			if (!f) {
				return Err{ core::AssetSyncError::kIoError };
			}
		}

		const std::filesystem::path rel =
				std::filesystem::relative(entry.path(), pack_root, ec);
		if (ec) {
			return Err{ core::AssetSyncError::kIoError };
		}

		AssetEntry ae;
		ae.path = rel.generic_string();
		ae.hash = hash_bytes(bytes);
		ae.size = file_size;
		ae.kind = kind_for(rel);
		manifest.entries.push_back(std::move(ae));
	}

	std::sort(manifest.entries.begin(), manifest.entries.end(),
			[](const AssetEntry &a, const AssetEntry &b) { return a.path < b.path; });
	manifest.total_bytes = running_total;

	std::vector<std::byte> scratch;
	protocol::ByteWriter w(scratch);
	for (const auto &e : manifest.entries) {
		w.string(e.path);
		w.u64(e.hash.lo);
		w.u64(e.hash.hi);
		w.u64(e.size);
		w.u8(static_cast<std::uint8_t>(e.kind));
	}
	manifest.manifest_hash = hash_bytes(scratch);

	return manifest;
}

} // namespace vb::assetsync

#endif // VB_WITH_COMPRESSION
