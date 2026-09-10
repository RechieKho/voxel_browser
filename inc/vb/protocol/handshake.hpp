#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/core/result.hpp"
#include "vb/protocol/byte_buffer.hpp"
#include "vb/protocol/message.hpp"

// Connection handshake payloads (spec §8.3). Every struct has:
//   void encode(std::vector<std::byte>&) const;   // appends the payload
//   static Result<T, ProtocolError> decode(std::span<const std::byte>);
// and a round-trip test in tests/unit/protocol_test.cpp.

namespace vb::protocol {

template <typename T>
using Decoded = core::Result<T, core::ProtocolError>;

enum class AuthMode : std::uint8_t {
	kNone = 0,
	kToken = 1,
};
constexpr bool valid(AuthMode m) {
	return m == AuthMode::kNone || m == AuthMode::kToken;
}

enum class DisconnectReason : std::uint8_t {
	kUnknown = 0,
	kServerFull,
	kProtocolMismatch,
	kAuthFailed,
	kServerShutdown,
	kKicked,
	kTimeout,
	kProtocolError,
	kBadHandshake,
};
constexpr bool valid(DisconnectReason r) {
	return static_cast<std::uint8_t>(r) <= static_cast<std::uint8_t>(DisconnectReason::kBadHandshake);
}

struct C2SHello {
	static constexpr MessageType kType = MessageType::kC2SHello;
	std::uint16_t engine_protocol_version = 0;
	std::uint64_t client_nonce = 0;
	std::string client_version;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SHello> decode(std::span<const std::byte> in);
};

struct S2CServerInfo {
	static constexpr MessageType kType = MessageType::kS2CServerInfo;
	std::string pack_name;
	std::string pack_version;
	std::uint16_t engine_protocol_version = 0;
	std::uint16_t tick_rate = 20;
	std::string motd;
	AuthMode auth_mode = AuthMode::kNone;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CServerInfo> decode(std::span<const std::byte> in);
};

struct C2SAuth {
	static constexpr MessageType kType = MessageType::kC2SAuth;
	std::string player_name;
	std::string token; // empty when auth_mode == kNone

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SAuth> decode(std::span<const std::byte> in);
};

struct S2CAuthResult {
	static constexpr MessageType kType = MessageType::kS2CAuthResult;
	bool ok = false;
	std::string reason;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CAuthResult> decode(std::span<const std::byte> in);
};

struct C2SReady {
	static constexpr MessageType kType = MessageType::kC2SReady;
	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SReady> decode(std::span<const std::byte> in);
};

struct S2CJoinAccept {
	static constexpr MessageType kType = MessageType::kS2CJoinAccept;
	core::NetId your_net_id = core::NetId::kInvalid;
	core::Vec3d spawn_pos{};
	std::uint64_t world_seed = 0;
	std::uint32_t time_of_day = 0; // ticks into the day cycle

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CJoinAccept> decode(std::span<const std::byte> in);
};

struct S2CDisconnect {
	static constexpr MessageType kType = MessageType::kS2CDisconnect;
	DisconnectReason reason = DisconnectReason::kUnknown;
	std::string message;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CDisconnect> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
