#pragma once

#include <cstdint>
#include <string_view>

// Engine-wide error enums. Each subsystem owns its own enum; `Result<T, XError>`
// keeps failures explicit without exceptions. Keep `message()` switches total so
// -Werror=switch catches an unhandled case when a value is added.

namespace vb::core {

enum class CoreError : std::uint8_t {
	kNone = 0,
	kInvalidArgument,
	kNotFound,
	kIoError,
	kParseError,
	kOutOfRange,
	kUnsupported,
};

constexpr std::string_view message(CoreError e) {
	switch (e) {
		case CoreError::kNone:
			return "no error";
		case CoreError::kInvalidArgument:
			return "invalid argument";
		case CoreError::kNotFound:
			return "not found";
		case CoreError::kIoError:
			return "I/O error";
		case CoreError::kParseError:
			return "parse error";
		case CoreError::kOutOfRange:
			return "value out of range";
		case CoreError::kUnsupported:
			return "unsupported";
	}
	return "unknown core error";
}

// Wire (de)serialization failures — see inc/vb/protocol/.
enum class ProtocolError : std::uint8_t {
	kNone = 0,
	kShortBuffer, // ran off the end of the input
	kOverlongVarint, // varint exceeded its type width
	kTrailingBytes, // decoded fine but bytes were left over
	kBadEnum, // enum value not in range
	kLengthExceeded, // a declared length exceeds a sane cap
	kVersionMismatch, // ENGINE_PROTOCOL_VERSION disagreement
	kMalformed, // structurally invalid payload
};

constexpr std::string_view message(ProtocolError e) {
	switch (e) {
		case ProtocolError::kNone:
			return "no error";
		case ProtocolError::kShortBuffer:
			return "buffer underrun";
		case ProtocolError::kOverlongVarint:
			return "overlong varint";
		case ProtocolError::kTrailingBytes:
			return "trailing bytes after payload";
		case ProtocolError::kBadEnum:
			return "enum value out of range";
		case ProtocolError::kLengthExceeded:
			return "declared length exceeds cap";
		case ProtocolError::kVersionMismatch:
			return "protocol version mismatch";
		case ProtocolError::kMalformed:
			return "malformed payload";
	}
	return "unknown protocol error";
}

} // namespace vb::core
