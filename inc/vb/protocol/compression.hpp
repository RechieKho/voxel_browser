#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "vb/core/error.hpp"
#include "vb/core/result.hpp"

// Optional wire compression for a message payload (ARCHITECTURE_SPEC.md §18
// Q4: "Chunk compression: LZ4 vs. zstd vs. palette-only" -- resolved LZ4,
// since VB_WITH_COMPRESSION already links it for asset-sync manifest hashing
// and this is the same dependency, not a new one). `frame_message()`
// (vb/net/handshake.hpp) is the one real caller: it compresses an encoded
// payload above a size threshold when doing so actually shrinks it, and sets
// MessageFlag::kCompressed so the receiver knows to reverse it before
// decoding the message struct.
//
// Only declared when VB_WITH_COMPRESSION is on -- compression is a build-time
// feature, not a runtime fallback, matching every other optional subsystem's
// own `#if VB_WITH_*` gating in this codebase (assetsync, worldgen, ...).
//
// Wire format: a u32 LE "original size" prefix followed by one LZ4 block
// (LZ4_compress_default / LZ4_decompress_safe) -- LZ4's block API carries no
// length of its own, so the decompressor has to be told the output size up
// front rather than discovering it.

#if VB_WITH_COMPRESSION

namespace vb::protocol {

std::vector<std::byte> compress_lz4(std::span<const std::byte> in);

core::Result<std::vector<std::byte>, core::ProtocolError> decompress_lz4(
		std::span<const std::byte> in);

} // namespace vb::protocol

#endif // VB_WITH_COMPRESSION
