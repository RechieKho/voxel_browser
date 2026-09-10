#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vb/core/error.hpp"
#include "vb/core/result.hpp"
#include "vb/world/chunk.hpp"

// Serialize a chunk's block store + light volume to a compact byte blob and
// back (spec §8.5). The blob is RLE'd (terrain and light are very repetitive),
// then optionally LZ4-framed by the caller via MessageFlag::kCompressed.
//
// Layout:
//   u8   bits_per_index
//   var  palette_len ; palette_len * u16 BlockId
//   rle  packed index words  (empty when homogeneous)
//   rle  light bytes (CHUNK_VOLUME entries)

namespace vb::world {

// Byte-wise run-length: repeated (varint run_length, u8 value).
std::vector<std::byte> rle_encode(std::span<const std::byte> in);
core::Result<std::vector<std::byte>, core::ProtocolError> rle_decode(
		std::span<const std::byte> in, std::size_t expected_size);

std::vector<std::byte> encode_chunk_payload(const Chunk &chunk);

core::Result<void, core::ProtocolError> decode_chunk_payload(
		std::span<const std::byte> payload, Chunk &out);

} // namespace vb::world
