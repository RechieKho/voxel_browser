#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vb/core/error.hpp"
#include "vb/core/ids.hpp"
#include "vb/core/result.hpp"

// Server-side asset manifest (spec §9.1, Phase 4.4): walks a content pack
// directory, hashes every file, and produces a manifest the handshake sends
// to a connecting client (inc/vb/protocol/assetsync.hpp carries the wire
// form). Gated behind VB_WITH_COMPRESSION (the xxHash dependency); a build
// without it always fails with AssetSyncError::kDisabled, degrading the
// whole feature gracefully (no client is ever offered a manifest).

namespace vb::assetsync {

enum class AssetKind : std::uint8_t {
	kScript = 0,
	kTexture,
	kModel,
	kUi,
	kSound,
	kData,
};

struct AssetEntry {
	std::string path; // pack-relative, forward-slash, no leading '/'
	core::AssetHash hash{};
	std::uint64_t size = 0;
	AssetKind kind = AssetKind::kData;

	bool operator==(const AssetEntry &) const = default;
};

struct Manifest {
	std::vector<AssetEntry> entries; // sorted by path (deterministic order + hash)
	core::AssetHash manifest_hash{}; // xxHash3-128 over the sorted, serialized entries
	std::uint64_t total_bytes = 0;

	const AssetEntry *find(core::AssetHash h) const;
};

struct AssetSizeCaps {
	std::uint64_t max_file_bytes = 32u * 1024u * 1024u;
	std::uint64_t max_total_bytes = 512u * 1024u * 1024u;
};

// Walks `pack_root` recursively, hashing every regular file. Rejects any
// entry that (once resolved) falls outside `pack_root` (spec §9.4: '..',
// absolute paths, symlinks escaping the root) and any file/total size
// exceeding `caps`. Deterministic: identical directory contents always
// produce the same entry order and manifest_hash, regardless of the OS's
// directory-iteration order.
core::Result<Manifest, core::AssetSyncError> build_manifest(
		const std::filesystem::path &pack_root, AssetSizeCaps caps = {});

// xxHash3-128 of a byte span. Also used by the client cache to verify a
// downloaded file. Returns {} when built without VB_WITH_COMPRESSION --
// callers should gate on build_manifest's own kDisabled instead of relying
// on that.
core::AssetHash hash_bytes(std::span<const std::byte> data);

} // namespace vb::assetsync
