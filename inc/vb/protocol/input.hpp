#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "vb/core/math.hpp"
#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"

// C2S_InputBatch (spec §8.4) — lane 4 (kInput), unreliable, sent every client
// frame. Carries a short run of the most recent unacked InputCmds so a dropped
// packet is covered by the next one. The server acks the highest seq it has
// simulated via S2CEntitySnapshot::last_acked_input_seq.

namespace vb::protocol {

enum InputButton : std::uint8_t {
	kInputJump = 1u << 0,
	kInputSprint = 1u << 1,
	kInputPrimary = 1u << 2, // break / attack
	kInputSecondary = 1u << 3, // place / use
	kInputFlyUp = 1u << 4,
	kInputFlyDown = 1u << 5,
};

struct InputCmd {
	std::uint32_t seq = 0;
	float dt = 0.0f; // seconds this command covers
	core::Vec3f move{}; // x = strafe right, y = up (fly), z = forward; [-1,1]
	float yaw = 0.0f; // degrees
	float pitch = 0.0f; // degrees
	std::uint8_t buttons = 0;
	// Phase 6.3: closed-schema pack-defined keybinds. Bit i = the keybind at
	// index i in the most recent S2C_KeybindRegistry is held this cmd; an
	// unregistered key literally cannot be represented here.
	std::uint32_t keybinds = 0;

	bool operator==(const InputCmd &) const = default;
};

struct C2SInputBatch {
	static constexpr MessageType kType = MessageType::kC2SInputBatch;

	// Ascending by seq. The server ignores any seq it has already simulated.
	std::vector<InputCmd> cmds;

	// Wire cap: a misbehaving client can't make us allocate unboundedly.
	static constexpr std::size_t kMaxCmds = 64;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SInputBatch> decode(std::span<const std::byte> in);
};

// S2C_KeybindRegistry (spec §10.6, Phase 6.3) — sent between C2S_Ready and
// S2C_JoinAccept alongside S2C_BlockRegistry (same handshake step). The
// registered set is a closed, ordered schema: `names[i]` is the pack-chosen
// name for bit i of every InputCmd::keybinds going forward. Capped at
// kMaxKeybinds so the bitset always fits one uint32_t -- register_keybind
// enforces the same cap server-side before this is ever sent.
struct S2CKeybindRegistry {
	static constexpr MessageType kType = MessageType::kS2CKeybindRegistry;

	std::vector<std::string> names; // index == bit position

	static constexpr std::size_t kMaxKeybinds = 32;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CKeybindRegistry> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
