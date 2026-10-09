#include <doctest/doctest.h>

#include <ostream>

#include "vb/net/block_damage_tracker.hpp"

using vb::core::IVec3;
using vb::net::BlockDamageTracker;

namespace {

constexpr IVec3 kPos{ 3, 10, -4 };
constexpr IVec3 kAbove{ 3, 11, -4 };

int punches_at(const BlockDamageTracker &t, IVec3 pos) {
	const auto it = t.punches().find(pos);
	return it == t.punches().end() ? 0 : it->second;
}

} // namespace

// S2C_BlockDamage (Lane::kFeedback) and block changes (S2C_ChunkDelta on
// Lane::kWorld) are not ordered against each other; revisions decide.
TEST_CASE("block damage tracker: in-order updates") {
	BlockDamageTracker t;
	t.on_damage(kPos, 1, 5);
	t.on_damage(kPos, 2, 5); // later punch, same chunk revision
	CHECK(punches_at(t, kPos) == 2);
	t.on_damage(kPos, 0, 5); // healed
	CHECK(t.punches().empty());
}

TEST_CASE("block damage tracker: the block breaks, delta first") {
	BlockDamageTracker t;
	t.on_damage(kPos, 2, 5);
	t.on_block_changed(kPos, 6); // broken at revision 6
	CHECK(t.punches().empty());
	t.on_damage(kPos, 0, 6); // the server's "cleared" arrives after
	CHECK(t.punches().empty());
}

TEST_CASE("block damage tracker: damage for a replaced block arriving late is dropped") {
	BlockDamageTracker t;
	// Broken (6) and a new block placed (7) before the old block's last
	// damage update (sent at 5) gets here.
	t.on_block_changed(kPos, 6);
	t.on_block_changed(kPos, 7);
	t.on_damage(kPos, 3, 5);
	CHECK(t.punches().empty());
	// Damage on the new block is accepted.
	t.on_damage(kPos, 1, 7);
	CHECK(punches_at(t, kPos) == 1);
}

TEST_CASE("block damage tracker: damage on a new block arriving before its delta is kept") {
	BlockDamageTracker t;
	t.on_damage(kPos, 1, 7); // punch on the block placed at revision 7
	t.on_block_changed(kPos, 6); // the break, late
	t.on_block_changed(kPos, 7); // the place, late
	CHECK(punches_at(t, kPos) == 1);
}

TEST_CASE("block damage tracker: changes elsewhere in the chunk don't affect a position") {
	BlockDamageTracker t;
	t.on_block_changed({ kPos.x + 1, kPos.y, kPos.z }, 9);
	t.on_damage(kPos, 2, 5);
	CHECK(punches_at(t, kPos) == 2);
}

TEST_CASE("block damage tracker: a removed chunk forgets its damage and changes") {
	BlockDamageTracker t;
	t.on_damage(kPos, 2, 5);
	t.on_block_changed({ 100, 0, 0 }, 3); // another chunk
	t.on_damage({ 100, 0, 1 }, 1, 3);
	t.on_block_changed(kAbove, 8);
	t.on_chunk_removed(vb::core::chunk_of(kPos));
	CHECK(punches_at(t, kPos) == 0);
	CHECK(punches_at(t, { 100, 0, 1 }) == 1);
	// The chunk comes back with its revision reset: old change records are gone.
	t.on_damage(kAbove, 1, 2);
	CHECK(punches_at(t, kAbove) == 1);
}

TEST_CASE("block damage tracker: old change records are forgotten") {
	BlockDamageTracker t;
	t.on_block_changed(kPos, 6);
	t.advance(BlockDamageTracker::kForgetAfterSeconds + 1.0);
	t.advance(BlockDamageTracker::kForgetAfterSeconds + 1.0);
	t.on_damage(kPos, 1, 5); // no record left to compare against
	CHECK(punches_at(t, kPos) == 1);
}
