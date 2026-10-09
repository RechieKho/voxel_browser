#pragma once

#include <cstdint>
#include <unordered_map>

#include "vb/core/ids.hpp"

// Client-side mirror of the server's block damage (S2C_BlockDamage), kept
// consistent with block changes that arrive on another lane.
//
// S2C_BlockDamage travels on Lane::kFeedback so it never waits behind chunk
// data, which means it is not ordered against S2C_ChunkDelta/ChunkAdd. Each
// message carries the server's revision of the chunk holding the block when
// it was sent; each block change carries the revision it produced. The
// server's chunk revision only grows, so for one position:
//
//   damage.revision <  change revision  -> the damage was about the block
//                                          that change replaced: drop it
//   damage.revision >= change revision  -> it is about the current block
//
// That is what stops a late damage update from cracking a block placed after
// the damaged one broke, and a crack outliving its block.

namespace vb::net {

class BlockDamageTracker {
public:
	using DamageMap = std::unordered_map<core::IVec3, std::uint16_t>;

	// An S2C_BlockDamage. `punches == 0` clears the position.
	void on_damage(core::IVec3 pos, std::uint16_t punches, std::uint64_t revision);
	// The block at `pos` changed, at server chunk revision `revision`.
	void on_block_changed(core::IVec3 pos, std::uint64_t revision);
	// The chunk left this client's view: the server stops sending its damage.
	void on_chunk_removed(core::ChunkCoord coord);
	// Forgets change records older than kForgetAfterSeconds: by then no damage
	// message sent before that change can still be in flight.
	void advance(double dt_seconds);

	const DamageMap &punches() const { return punches_; }

	static constexpr double kForgetAfterSeconds = 30.0;

private:
	struct Change {
		std::uint64_t revision = 0;
		double at = 0.0;
	};

	DamageMap punches_;
	std::unordered_map<core::IVec3, std::uint64_t> damage_revision_;
	std::unordered_map<core::IVec3, Change> changes_;
	double now_ = 0.0;
	double next_prune_ = kForgetAfterSeconds;
};

} // namespace vb::net
