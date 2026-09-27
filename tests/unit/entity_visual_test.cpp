#include <doctest/doctest.h>

#include <ostream>

#include "vb/render/entity_visual.hpp"

using namespace vb::render;
using vb::core::Vec3d;
using vb::core::Vec3f;

TEST_CASE("resolve_anim_clip: idle/walk/run from horizontal speed on ground") {
	AnimThresholds t;
	CHECK(resolve_anim_clip({ 0.0f, 0.0f, 0.0f }, kAnimOnGround, t) == AnimClip::kIdle);
	CHECK(resolve_anim_clip({ 1.0f, 0.0f, 0.0f }, kAnimOnGround, t) == AnimClip::kWalk);
	CHECK(resolve_anim_clip({ 7.0f, 0.0f, 0.0f }, kAnimOnGround, t) == AnimClip::kRun);
}

TEST_CASE("resolve_anim_clip: airborne is jump or fall by vertical velocity") {
	AnimThresholds t;
	CHECK(resolve_anim_clip({ 0.0f, 5.0f, 0.0f }, 0, t) == AnimClip::kJump);
	CHECK(resolve_anim_clip({ 0.0f, -5.0f, 0.0f }, 0, t) == AnimClip::kFall);
	// Barely negative (still rising/at apex) counts as jump, not fall.
	CHECK(resolve_anim_clip({ 0.0f, -0.1f, 0.0f }, 0, t) == AnimClip::kJump);
}

TEST_CASE("resolve_anim_clip: priority order dead > hurt > acting > movement") {
	const std::uint8_t all = kAnimOnGround | kAnimDead | kAnimHurtPulse | kAnimActing;
	CHECK(resolve_anim_clip({ 7.0f, 0.0f, 0.0f }, all) == AnimClip::kDead);
	CHECK(resolve_anim_clip({ 7.0f, 0.0f, 0.0f },
				  kAnimOnGround | kAnimHurtPulse | kAnimActing) == AnimClip::kHurt);
	CHECK(resolve_anim_clip({ 7.0f, 0.0f, 0.0f }, kAnimOnGround | kAnimActing) ==
			AnimClip::kActing);
	CHECK(resolve_anim_clip({ 7.0f, 0.0f, 0.0f }, kAnimOnGround) == AnimClip::kRun);
}

TEST_CASE("bearing_degrees matches the engine's yaw convention (0 = -Z, 90 = +X)") {
	const Vec3d from{ 0, 0, 0 };
	CHECK(bearing_degrees(from, { 0, 0, -5 }) == doctest::Approx(0.0));
	CHECK(bearing_degrees(from, { 5, 0, 0 }) == doctest::Approx(90.0));
	CHECK(bearing_degrees(from, { 0, 0, 5 }) == doctest::Approx(180.0));
	CHECK(bearing_degrees(from, { -5, 0, 0 }) == doctest::Approx(270.0));
}

TEST_CASE("direction_bucket: camera facing the entity is bucket 0 (front)") {
	// Entity at origin facing yaw 0 (-Z); camera directly in front of it, i.e.
	// also along -Z from the entity -> the entity looks straight at the viewer.
	const double bearing = bearing_degrees({ 0, 0, 0 }, { 0, 0, -5 });
	CHECK(direction_bucket(bearing, 0.0, 8) == 0);
}

TEST_CASE("direction_bucket: camera behind the entity is the back bucket") {
	const double bearing = bearing_degrees({ 0, 0, 0 }, { 0, 0, 5 }); // +Z, behind
	CHECK(direction_bucket(bearing, 0.0, 8) == 4); // facings/2 = back
}

TEST_CASE("direction_bucket is invariant to the entity's own yaw offset") {
	const double bearing = bearing_degrees({ 0, 0, 0 }, { 5, 0, 0 });
	const int b0 = direction_bucket(bearing, 0.0, 8);
	const int b1 = direction_bucket(bearing, 90.0, 8); // entity turned 90 deg
	CHECK(b0 != b1); // same viewer angle reads differently once entity turns
}

TEST_CASE("select_pose: facings=8 mirrors the far half of the sectors") {
	CHECK(select_pose(0, 8) == PoseSelection{ 0, false }); // front
	CHECK(select_pose(1, 8) == PoseSelection{ 1, false });
	CHECK(select_pose(4, 8) == PoseSelection{ 4, false }); // back, no mirror needed
	CHECK(select_pose(5, 8) == PoseSelection{ 3, true });
	CHECK(select_pose(6, 8) == PoseSelection{ 2, true });
	CHECK(select_pose(7, 8) == PoseSelection{ 1, true });
}

TEST_CASE("select_pose: facings=4 needs only 3 unique poses") {
	CHECK(select_pose(0, 4) == PoseSelection{ 0, false });
	CHECK(select_pose(1, 4) == PoseSelection{ 1, false });
	CHECK(select_pose(2, 4) == PoseSelection{ 2, false });
	CHECK(select_pose(3, 4) == PoseSelection{ 1, true });
}

TEST_CASE("select_pose: facings=1 is trivial (the Phase 3 placeholder)") {
	CHECK(select_pose(0, 1) == PoseSelection{ 0, false });
}

TEST_CASE("select_pose: mirror=false never mirrors, one row per facing") {
	CHECK(select_pose(0, 4, false) == PoseSelection{ 0, false });
	CHECK(select_pose(1, 4, false) == PoseSelection{ 1, false });
	CHECK(select_pose(2, 4, false) == PoseSelection{ 2, false });
	// bucket 3 would mirror pose 1 under mirror=true (see the facings=4 case
	// above) -- with mirror=false it's its own distinct, unmirrored row.
	CHECK(select_pose(3, 4, false) == PoseSelection{ 3, false });
}

TEST_CASE("DirectionBucketTracker ignores a single-frame flicker") {
	DirectionBucketTracker tracker(3);
	CHECK(tracker.update(0) == 0);
	CHECK(tracker.update(1) == 0); // one-off flicker, not yet accepted
	CHECK(tracker.update(0) == 0); // back to 0 cancels the pending change
}

TEST_CASE("DirectionBucketTracker switches once a bucket is stable") {
	DirectionBucketTracker tracker(3);
	CHECK(tracker.update(0) == 0);
	CHECK(tracker.update(2) == 0);
	CHECK(tracker.update(2) == 0);
	CHECK(tracker.update(2) == 2); // 3rd consecutive observation -> switches
	CHECK(tracker.current() == 2);
}

TEST_CASE("EntityPresentationState: clip_time resets on clip change") {
	EntityPresentationState state(8);
	state.update({ 0, 0, 0 }, 0.0, { 0, 0, 0 }, kAnimOnGround, { 0, 0, -5 }, 0.5);
	CHECK(state.frame().clip == AnimClip::kIdle);
	state.update({ 0, 0, 0 }, 0.0, { 0, 0, 0 }, kAnimOnGround, { 0, 0, -5 }, 0.5);
	CHECK(state.frame().clip_time == doctest::Approx(1.0)); // accumulated

	state.update({ 0, 0, 0 }, 0.0, { 7.0f, 0, 0 }, kAnimOnGround, { 0, 0, -5 }, 0.5);
	CHECK(state.frame().clip == AnimClip::kRun);
	CHECK(state.frame().clip_time == doctest::Approx(0.0)); // reset on clip change
}

TEST_CASE("EntityPresentationState tracks the entity's latest position") {
	EntityPresentationState state(8);
	state.update({ 1, 2, 3 }, 0.0, {}, kAnimOnGround, { 0, 0, -5 }, 0.1);
	CHECK(state.position().x == doctest::Approx(1.0));
	CHECK(state.position().y == doctest::Approx(2.0));
	CHECK(state.position().z == doctest::Approx(3.0));
}

TEST_CASE("EntityPresentationState with mirror=false never reports a mirrored pose") {
	// facings=4, mirror=false, stable_frames=1 so a single update() commits.
	EntityPresentationState state(4, false, 1);
	// Camera due +X of the entity, entity facing its own -Z (yaw 0) -> the
	// same viewing angle that select_pose(3, 4) (mirrored) vs.
	// select_pose(3, 4, false) (not) distinguishes in entity_visual_test.cpp
	// above.
	state.update({ 0, 0, 0 }, 0.0, {}, kAnimOnGround, { -5, 0, 0 }, 0.0);
	CHECK_FALSE(state.frame().pose.mirrored);
}

TEST_CASE("anim_clip_name covers every AnimClip with rendering.md's base clip names") {
	CHECK(anim_clip_name(AnimClip::kIdle) == "idle");
	CHECK(anim_clip_name(AnimClip::kWalk) == "walk");
	CHECK(anim_clip_name(AnimClip::kRun) == "run");
	CHECK(anim_clip_name(AnimClip::kJump) == "jump");
	CHECK(anim_clip_name(AnimClip::kFall) == "fall");
	CHECK(anim_clip_name(AnimClip::kActing) == "acting");
	CHECK(anim_clip_name(AnimClip::kHurt) == "hurt");
	CHECK(anim_clip_name(AnimClip::kDead) == "dead");
}
