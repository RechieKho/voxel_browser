#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"

// World replication messages (spec §8.5), lane 1. The chunk payload is an
// opaque blob produced by vb::world::encode_chunk_payload — the protocol layer
// stays independent of the voxel data model.

namespace vb::protocol {

// --- block registry (spec §8.3 / §9) ------------------------------------
// Sent between C2S_Ready and S2C_JoinAccept (Phase 4.3) so the client can
// mirror the server's (possibly Lua-extended) block table. No model/
// collision-shape fields exist here yet.

struct BlockRegistryRecord {
	std::string name;
	bool solid = true;
	bool opaque = true;
	bool liquid = false;
	std::uint8_t light_emission = 0;
	// Real texture/atlas system: mirrors vb::world::BlockType::texture --
	// pack-relative path, empty = no texture. The client resolves it against
	// its own Asset Sync virtual FS (vb::assetsync::ClientAssetCache), same
	// posture as every other BlockType field mirrored here.
	std::string texture;
	// Phase 6.5 (spec §10.7): 0 = instant break, no shared damage pool.
	std::uint16_t max_damage = 0;

	bool operator==(const BlockRegistryRecord &) const = default;
};

struct S2CBlockRegistry {
	static constexpr MessageType kType = MessageType::kS2CBlockRegistry;

	std::vector<BlockRegistryRecord> blocks; // index == BlockId

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CBlockRegistry> decode(std::span<const std::byte> in);
};

// --- physics parameters (spec §7.3, Phase 6.7) --------------------------
// vb::physics::MoveParams's own comment says "Engine defaults; a Lua pack
// overrides per entity kind" -- mirrored here flat (no dependency from
// protocol/ onto physics/, same posture as BlockRegistryRecord mirroring
// BlockType) so the client's prediction can use the exact tunables the
// server's authoritative simulation does, instead of silently drifting from
// vb::physics::MoveParams's own hardcoded defaults. Sent between C2S_Ready
// and S2C_JoinAccept alongside S2C_BlockRegistry/S2C_KeybindRegistry.
struct S2CMoveParams {
	static constexpr MessageType kType = MessageType::kS2CMoveParams;

	double half_width = 0.4;
	double height = 1.8;
	double eye_height = 1.62;
	double walk_speed = 4.5;
	double sprint_speed = 7.0;
	double accel = 45.0;
	double air_accel = 10.0;
	double friction = 12.0;
	double gravity = 28.0;
	double jump_speed = 8.9;
	double terminal_velocity = 60.0;
	double step_height = 1.05;
	double fly_speed = 12.0;
	bool fly = false;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CMoveParams> decode(std::span<const std::byte> in);
};

// --- day/night curve (spec §5.4, Phase 6.8) -----------------------------
// Mirrors vb::world::DayNightKeyframe flat, same posture as
// BlockRegistryRecord/S2CMoveParams (protocol/ never depends on world/).
// Sent between C2S_Ready and S2C_JoinAccept alongside S2C_BlockRegistry/
// S2C_MoveParams only when a pack overrides the default curve via
// vb.daynight.set_curve{...}; no frame at all keeps every client on
// vb::world::default_day_night_curve().
struct DayNightKeyframeRecord {
	std::uint32_t tick = 0;
	double brightness = 1.0;
	std::uint8_t r = 0;
	std::uint8_t g = 0;
	std::uint8_t b = 0;

	bool operator==(const DayNightKeyframeRecord &) const = default;
};

struct S2CDayNightCurve {
	static constexpr MessageType kType = MessageType::kS2CDayNightCurve;

	std::vector<DayNightKeyframeRecord> keyframes;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CDayNightCurve> decode(std::span<const std::byte> in);
};

// --- distance fog (spec §7.2, Phase 7.2) --------------------------------
// A pack's `vb.render.set_fog{start=, end=}` override, sent the same
// opt-in way as S2C_DayNightCurve/S2C_MoveParams above: `nullopt` from
// HandshakeServerHost::fog_params sends no frame at all, and the client
// falls back to its own engine default (fog distance matching its own
// view_distance config) -- there IS no server-side universal default to
// send unprompted, since the server doesn't know each client's
// view_distance. No color field -- fog always reads as "distance to the
// current sky color" (vb::world::sky_color_for_time()), never an
// independently drifting tint (decided 2026-09-19).
struct S2CFogParams {
	static constexpr MessageType kType = MessageType::kS2CFogParams;

	float fog_start = 0.0f;
	float fog_end = 0.0f;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CFogParams> decode(std::span<const std::byte> in);
};

struct S2CChunkAdd {
	static constexpr MessageType kType = MessageType::kS2CChunkAdd;

	core::ChunkCoord coord{};
	std::uint64_t revision = 0;
	std::vector<std::byte> payload; // world::encode_chunk_payload output

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CChunkAdd> decode(std::span<const std::byte> in);
};

struct BlockChange {
	std::uint32_t local_index = 0; // 0 .. CHUNK_VOLUME-1
	core::BlockId block = core::BlockId::kAir;

	bool operator==(const BlockChange &) const = default;
};

struct LightChange {
	std::uint32_t local_index = 0;
	std::uint8_t packed = 0;

	bool operator==(const LightChange &) const = default;
};

struct S2CChunkDelta {
	static constexpr MessageType kType = MessageType::kS2CChunkDelta;

	core::ChunkCoord coord{};
	std::uint64_t base_revision = 0;
	std::uint64_t new_revision = 0;
	std::vector<BlockChange> blocks;
	std::vector<LightChange> light;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CChunkDelta> decode(std::span<const std::byte> in);
};

struct S2CChunkRemove {
	static constexpr MessageType kType = MessageType::kS2CChunkRemove;

	core::ChunkCoord coord{};

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CChunkRemove> decode(std::span<const std::byte> in);
};

// --- block editing (spec §8.5 / §5.2) -----------------------------------

enum class BlockEditAction : std::uint8_t {
	kBreak = 0, // remove the block at `pos`
	kPlace = 1, // fill the (empty) cell at `pos` with `block`
};
constexpr bool valid(BlockEditAction a) {
	return a == BlockEditAction::kBreak || a == BlockEditAction::kPlace;
}

struct C2SBlockEdit {
	static constexpr MessageType kType = MessageType::kC2SBlockEdit;

	std::uint32_t predicted_seq = 0; // client's optimistic-apply id
	BlockEditAction action = BlockEditAction::kBreak;
	core::IVec3 pos{}; // world voxel coordinate
	core::BlockId block = core::BlockId::kAir; // kPlace only

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SBlockEdit> decode(std::span<const std::byte> in);
};

struct S2CBlockEditResult {
	static constexpr MessageType kType = MessageType::kS2CBlockEditResult;

	std::uint32_t predicted_seq = 0;
	bool accepted = false; // false -> client rolls its optimistic apply back
	core::IVec3 pos{};

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CBlockEditResult> decode(std::span<const std::byte> in);
};

// --- shared block-damage breaking (spec §10.7) --------------------------
// Brackets a player holding a target: begin starts (or refreshes) them as a
// contributor to that pos's damage pool, stop drops them. The damage state
// itself, and its completion into an actual break, ride the *existing*
// C2S_BlockEdit/BlockEditSystem pipeline server-side -- these two messages
// only mark who's currently holding, nothing more.

struct C2SBlockBreakBegin {
	static constexpr MessageType kType = MessageType::kC2SBlockBreakBegin;

	core::IVec3 pos{}; // world voxel coordinate of the target
	core::IVec3 face{}; // hit-face normal (from the client's own raycast)

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SBlockBreakBegin> decode(std::span<const std::byte> in);
};

struct C2SBlockBreakStop {
	static constexpr MessageType kType = MessageType::kC2SBlockBreakStop;

	core::IVec3 pos{};

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SBlockBreakStop> decode(std::span<const std::byte> in);
};

// --- day/night (spec §5.4) ------------------------------------------------
// Periodic update of the server's time_of_day clock (whose initial value is
// already carried by S2C_JoinAccept); keeps already-connected clients' sky
// in sync as the server's clock advances. See vb::world::daynight.hpp for
// the tick semantics (0 = sunrise, wraps at kTicksPerDay).
struct S2CTimeOfDay {
	static constexpr MessageType kType = MessageType::kS2CTimeOfDay;

	std::uint32_t time_of_day = 0;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CTimeOfDay> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
