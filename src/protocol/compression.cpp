#include "vb/protocol/compression.hpp"

#if VB_WITH_COMPRESSION

#include <lz4.h>

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

std::vector<std::byte> compress_lz4(std::span<const std::byte> in) {
	std::vector<std::byte> out;
	ByteWriter w(out);
	w.u32(static_cast<std::uint32_t>(in.size()));
	if (in.empty()) {
		return out; // a bare 0-length prefix, nothing to compress
	}

	const int src_size = static_cast<int>(in.size());
	const int bound = LZ4_compressBound(src_size);
	std::vector<char> compressed(static_cast<std::size_t>(bound));
	const int written = LZ4_compress_default(
			reinterpret_cast<const char *>(in.data()), compressed.data(),
			src_size, bound);
	// LZ4_compressBound() guarantees `compressed` is large enough for any
	// input, so a real LZ4 build never returns 0 here; if it somehow did, `out`
	// already carries a valid prefix with zero compressed bytes after it, and
	// decompress_lz4() below reports that as too-short-to-be-valid rather than
	// silently zero-filling the decoded output.
	out.insert(out.end(), reinterpret_cast<const std::byte *>(compressed.data()),
			reinterpret_cast<const std::byte *>(compressed.data()) + written);
	return out;
}

core::Result<std::vector<std::byte>, core::ProtocolError> decompress_lz4(
		std::span<const std::byte> in) {
	ByteReader r(in);
	const std::uint32_t original_size = r.u32();
	if (r.failed()) {
		return core::Err{ core::ProtocolError::kShortBuffer };
	}
	if (original_size > kMaxDecodedLength) {
		return core::Err{ core::ProtocolError::kLengthExceeded };
	}
	std::vector<std::byte> out(original_size);
	if (original_size == 0) {
		return out;
	}
	const auto compressed = r.bytes(r.remaining());
	if (r.failed() || compressed.empty()) {
		return core::Err{ core::ProtocolError::kShortBuffer };
	}
	const int decoded = LZ4_decompress_safe(
			reinterpret_cast<const char *>(compressed.data()),
			reinterpret_cast<char *>(out.data()),
			static_cast<int>(compressed.size()),
			static_cast<int>(original_size));
	if (decoded < 0 || static_cast<std::uint32_t>(decoded) != original_size) {
		return core::Err{ core::ProtocolError::kMalformed };
	}
	return out;
}

} // namespace vb::protocol

#endif // VB_WITH_COMPRESSION
