#include "vb/net/block_damage_tracker.hpp"

#include <iterator>

namespace vb::net {

void BlockDamageTracker::on_damage(core::IVec3 pos, std::uint16_t punches,
		std::uint64_t revision) {
	if (const auto it = changes_.find(pos); it != changes_.end() && revision < it->second.revision) {
		return; // sent before the block at pos was replaced
	}
	// Same lane, so in order; equal revisions are later punches on the same block.
	if (const auto it = damage_revision_.find(pos);
			it != damage_revision_.end() && revision < it->second) {
		return;
	}
	if (punches == 0) {
		punches_.erase(pos);
		damage_revision_.erase(pos);
		return;
	}
	punches_[pos] = punches;
	damage_revision_[pos] = revision;
}

void BlockDamageTracker::on_block_changed(core::IVec3 pos, std::uint64_t revision) {
	Change &change = changes_[pos];
	if (revision >= change.revision) {
		change = { revision, now_ };
	}
	if (const auto it = damage_revision_.find(pos);
			it != damage_revision_.end() && it->second < revision) {
		punches_.erase(pos);
		damage_revision_.erase(it);
	}
}

void BlockDamageTracker::on_chunk_removed(core::ChunkCoord coord) {
	for (auto it = punches_.begin(); it != punches_.end();) {
		if (core::chunk_of(it->first) == coord) {
			damage_revision_.erase(it->first);
			it = punches_.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = changes_.begin(); it != changes_.end();) {
		it = core::chunk_of(it->first) == coord ? changes_.erase(it) : std::next(it);
	}
}

void BlockDamageTracker::advance(double dt_seconds) {
	now_ += dt_seconds;
	if (now_ < next_prune_) {
		return;
	}
	next_prune_ = now_ + kForgetAfterSeconds;
	for (auto it = changes_.begin(); it != changes_.end();) {
		it = now_ - it->second.at > kForgetAfterSeconds ? changes_.erase(it) : std::next(it);
	}
}

} // namespace vb::net
