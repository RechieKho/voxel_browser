#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"

// Asset sync messages (spec §9), lane 3. Sent between S2C_AuthResult and
// C2S_Ready. The protocol layer stays independent of vb::assetsync's own
// Manifest/AssetEntry types (same convention as chat.hpp/world.hpp) so a
// client built without VB_WITH_COMPRESSION can still parse -- and cleanly
// reject/ignore -- a manifest from a server that has one.

namespace vb::protocol {

enum class AssetKind : std::uint8_t {
	kScript = 0,
	kTexture,
	kModel,
	kUi,
	kSound,
	kData,
};
constexpr bool valid(AssetKind k) { return k <= AssetKind::kData; }

// `known_manifest_hash` is {0,0} on a client with nothing cached for this
// server -- the reconnect fast path (spec §9.1): if it matches the server's
// current manifest_hash, S2C_AssetManifest comes back with empty `entries`.
struct C2SAssetManifestRequest {
	static constexpr MessageType kType = MessageType::kC2SAssetManifestRequest;

	core::AssetHash known_manifest_hash{};

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SAssetManifestRequest> decode(std::span<const std::byte> in);
};

struct AssetEntryRecord {
	std::string path;
	core::AssetHash hash{};
	std::uint64_t size = 0;
	AssetKind kind = AssetKind::kData;

	bool operator==(const AssetEntryRecord &) const = default;
};

struct S2CAssetManifest {
	static constexpr MessageType kType = MessageType::kS2CAssetManifest;

	core::AssetHash manifest_hash{};
	std::uint64_t total_bytes = 0;
	std::vector<AssetEntryRecord> entries; // empty when known_manifest_hash matched

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CAssetManifest> decode(std::span<const std::byte> in);
};

// Hashes the client is missing; empty = "send nothing, I have it all".
struct C2SAssetRequest {
	static constexpr MessageType kType = MessageType::kC2SAssetRequest;

	std::vector<core::AssetHash> missing;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SAssetRequest> decode(std::span<const std::byte> in);
};

struct S2CAssetData {
	static constexpr MessageType kType = MessageType::kS2CAssetData;

	core::AssetHash hash{}; // which file this chunk belongs to
	std::uint32_t seq = 0; // 0-based chunk index within this file
	std::uint32_t total_chunks = 0;
	std::vector<std::byte> bytes; // ~48 KiB target; last chunk may be shorter

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CAssetData> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
