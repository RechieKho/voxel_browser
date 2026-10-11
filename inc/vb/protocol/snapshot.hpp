#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"
#include "vb/protocol/world.hpp" // EntityVisualOverride

// S2C_EntitySnapshot (spec §8.4) — lane 2, unreliable, one per server tick.
// Carries newly-visible entities in full (`entered`), position/rotation deltas
// for still-visible ones (`updated`), and ids that left the interest set
// (`removed`). See docs/replication.md.

namespace vb::protocol {

struct EntityRecord {
	core::NetId net_id = core::NetId::kInvalid;
	core::EntityKindId kind = core::EntityKindId::kInvalid;
	core::Vec3d pos{};
	core::Vec2f rot{}; // yaw, pitch (degrees)
	core::Vec3f vel{};
	std::uint8_t flags = 0;
	// Entity-management follow-up (spec architecture_spec/rendering.md
	// §11.3's "Per-instance override"): a script entity's `vb.world.spawn`
	// `visual_override` option, learned once and cached client-side
	// (ClientSession's own entity_visual_overrides_ map) rather than resent
	// every tick. Only ever populated on an `entered` record -- ServerSession
	// only attaches it there (see net::ServerSession::to_record); `updated`/
	// `local` records always leave this nullopt, which means "unchanged", not
	// "cleared" (there is no clear path yet -- the override is fixed for the
	// entity's whole replicated lifetime, same as `kind`). Absent entirely
	// (the common case) costs one bool on the wire.
	std::optional<EntityVisualOverride> visual_override;
	// The block id a dropped-item entity represents, so the client can draw
	// it as that block. Same "entered records only, nullopt = unchanged"
	// contract as `visual_override` above.
	std::optional<std::uint16_t> item;

	bool operator==(const EntityRecord &) const = default;
};

struct S2CEntitySnapshot {
	static constexpr MessageType kType = MessageType::kS2CEntitySnapshot;

	std::uint32_t server_tick = 0;
	std::uint32_t last_acked_input_seq = 0; // highest InputCmd seq simulated
	std::vector<EntityRecord> entered;
	std::vector<EntityRecord> updated;
	std::vector<core::NetId> removed;

	// The recipient's own authoritative state (interest culling excludes self,
	// so it is carried separately for client-side reconciliation, spec §8.4).
	bool has_local = false;
	EntityRecord local{};

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CEntitySnapshot> decode(std::span<const std::byte> in);
};

// A world-space text label attached to a script entity (protocol v32;
// `vb.world.spawn(kind, pos, {text = ...})` / `entity:set_text(...)`). Always
// fully resolved server-side (kind default + per-call fields merged by
// PackRuntime), so the client never has to know a kind's text defaults.
inline constexpr std::size_t kMaxEntityTextBytes = 64;

struct EntityText {
	std::string value; // UTF-8, <= kMaxEntityTextBytes, '\n' starts a new line
	std::array<std::uint8_t, 4> color{ 255, 255, 255, 255 }; // RGBA
	// Rounded panel drawn behind the text; nullopt draws a dark outline
	// around the glyphs instead, so the label stays legible either way.
	std::optional<std::array<std::uint8_t, 4>> background;
	float size = 0.3f; // height of one text line, metres
	float offset_y = 2.05f; // metres above the entity's position (its anchor)
	float max_distance = 0.0f; // hide beyond this many metres; 0 = no limit
	bool through_walls = false; // skip the depth test (name tags)

	bool operator==(const EntityText &) const = default;
};

struct EntityTextUpdate {
	core::NetId net_id = core::NetId::kInvalid;
	std::optional<EntityText> text; // nullopt = the label was removed

	bool operator==(const EntityTextUpdate &) const = default;
};

// S2C_EntityText (protocol v32) -- lane kFeedback (reliable ordered), at most
// one per player per server tick. Carries the current label of every script
// entity that entered the recipient's interest set this tick and has one,
// plus every label that changed (set or removed) on an entity already in it.
// The text's lifetime on the client is the entity's: a snapshot `removed`
// entry drops it. Because this is reliable and snapshots are not, the two can
// arrive in either order; `server_tick` lets the client keep a label sent at
// or after the tick of a (late) removal, see ClientSession::apply_snapshot.
struct S2CEntityText {
	static constexpr MessageType kType = MessageType::kS2CEntityText;

	std::uint32_t server_tick = 0;
	std::vector<EntityTextUpdate> updates;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CEntityText> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
