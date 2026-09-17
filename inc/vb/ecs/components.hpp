#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/physics/movement.hpp"

// Base ECS components (spec §7.1). The server holds an EnTT registry of these
// (one entity per playing connection, `vb::net::ServerSession`); the client
// keeps a lightweight subset for rendering (Position prev/current, Rotation,
// EntityKind, RenderHandle). Systems live in vb/ecs and vb/physics.
//
// Phase 3.1 (2026-09-17): ServerSession's own methods read/write player state
// through the registry directly (no indirection through Conn's old inline
// fields anymore); a `SystemRunner` that iterates the registry generically is
// still follow-up work, deferred until a Lua entity kind (Phase 4) actually
// needs to.

namespace vb::ecs {

struct Position {
	core::Vec3d value{};
};

struct Velocity {
	core::Vec3d value{};
};

struct Rotation {
	float yaw = 0.0f; // degrees
	float pitch = 0.0f;
};

struct PlayerTag {
	std::string name;
};

struct NetReplicated {
	core::NetId net_id = core::NetId::kInvalid;
};

struct EntityKind {
	core::EntityKindId id = core::EntityKindId::kInvalid;
};

struct Collider {
	physics::MoveParams params{};
	bool on_ground = false;
};

struct PlayerInput {
	core::Vec3f move{};
	float yaw = 0.0f;
	float pitch = 0.0f;
	std::uint8_t buttons = 0;
	std::uint32_t last_seq = 0; // highest input seq applied
};

struct Health {
	float current = 20.0f;
	float max = 20.0f;
};

struct ItemStack {
	core::BlockId item = core::BlockId::kAir;
	std::uint16_t count = 0;
};

struct Inventory {
	std::vector<ItemStack> slots;
};

// Client-side render interpolation buffer (spec §8.4): two authoritative samples
// and the render lerps between them at server_time_est - interp_delay.
struct InterpBuffer {
	core::Vec3d prev_pos{};
	core::Vec3d cur_pos{};
	float prev_yaw = 0.0f;
	float cur_yaw = 0.0f;
	std::uint32_t prev_tick = 0;
	std::uint32_t cur_tick = 0;
};

} // namespace vb::ecs
