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

// Lua scripting failures — see inc/vb/script/.
enum class ScriptError : std::uint8_t {
	kNone = 0,
	kSyntax, // chunk failed to compile
	kRuntime, // error raised during execution
	kBudgetExceeded, // instruction-count hook fired (runaway callback)
	kOutOfMemory, // allocator ceiling hit
	kSandboxViolation, // attempted to reach a stripped global
	kNotFound, // module / callback not found
	kDisabled, // built without VB_WITH_LUA
};

constexpr std::string_view message(ScriptError e) {
	switch (e) {
		case ScriptError::kNone:
			return "no error";
		case ScriptError::kSyntax:
			return "script syntax error";
		case ScriptError::kRuntime:
			return "script runtime error";
		case ScriptError::kBudgetExceeded:
			return "script instruction budget exceeded";
		case ScriptError::kOutOfMemory:
			return "script memory ceiling exceeded";
		case ScriptError::kSandboxViolation:
			return "script sandbox violation";
		case ScriptError::kNotFound:
			return "script module or callback not found";
		case ScriptError::kDisabled:
			return "scripting disabled (built without VB_WITH_LUA)";
	}
	return "unknown script error";
}

// Transport / connection failures — see inc/vb/net/.
enum class NetError : std::uint8_t {
	kNone = 0,
	kAlreadyListening,
	kNotListening,
	kBindFailed,
	kConnectFailed,
	kUnknownConnection,
	kBackendUnavailable, // built without VB_WITH_NET
	kHandshakeTimeout,
	kHandshakeProtocol, // peer violated the handshake sequence
	kServerFull,
	kAuthRejected,
	kDisconnected,
};

constexpr std::string_view message(NetError e) {
	switch (e) {
		case NetError::kNone:
			return "no error";
		case NetError::kAlreadyListening:
			return "already listening";
		case NetError::kNotListening:
			return "not listening";
		case NetError::kBindFailed:
			return "bind failed";
		case NetError::kConnectFailed:
			return "connect failed";
		case NetError::kUnknownConnection:
			return "unknown connection";
		case NetError::kBackendUnavailable:
			return "network backend unavailable (built without VB_WITH_NET)";
		case NetError::kHandshakeTimeout:
			return "handshake timed out";
		case NetError::kHandshakeProtocol:
			return "handshake protocol violation";
		case NetError::kServerFull:
			return "server full";
		case NetError::kAuthRejected:
			return "authentication rejected";
		case NetError::kDisconnected:
			return "disconnected";
	}
	return "unknown network error";
}

} // namespace vb::core
