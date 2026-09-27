#include <doctest/doctest.h>

#include "vb/render/frustum.hpp"

using vb::core::Vec3d;
using vb::render::aabb_in_frustum;
using vb::render::build_frustum;

namespace {

// Camera at the origin looking down +X, world-up +Y, a 90-degree vertical
// FOV, 1:1 aspect, near=0.1/far=100 -- simple numbers to reason about by hand.
vb::render::Frustum test_frustum() {
	return build_frustum({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, 90.0, 1.0, 0.1, 100.0);
}

} // namespace

TEST_CASE("aabb_in_frustum: a box straight ahead is inside") {
	const auto f = test_frustum();
	CHECK(aabb_in_frustum(f, { 10.0, -1.0, -1.0 }, { 12.0, 1.0, 1.0 }));
}

TEST_CASE("aabb_in_frustum: a box directly behind the camera is culled") {
	const auto f = test_frustum();
	CHECK_FALSE(aabb_in_frustum(f, { -12.0, -1.0, -1.0 }, { -10.0, 1.0, 1.0 }));
}

TEST_CASE("aabb_in_frustum: a box far off to one side is culled") {
	const auto f = test_frustum();
	// Well outside the +/-45 degree horizontal half-angle at this distance.
	CHECK_FALSE(aabb_in_frustum(f, { 10.0, -1.0, 500.0 }, { 12.0, 1.0, 502.0 }));
}

TEST_CASE("aabb_in_frustum: a box beyond the far plane is culled") {
	const auto f = test_frustum();
	CHECK_FALSE(aabb_in_frustum(f, { 500.0, -1.0, -1.0 }, { 502.0, 1.0, 1.0 }));
}

TEST_CASE("aabb_in_frustum: a box nearer than the near plane is culled") {
	const auto f = test_frustum();
	CHECK_FALSE(aabb_in_frustum(f, { -0.05, -0.01, -0.01 }, { 0.05, 0.01, 0.01 }));
}

TEST_CASE("aabb_in_frustum: a box straddling the frustum boundary still counts as visible") {
	const auto f = test_frustum();
	// Spans from well inside to well outside on one axis -- the conservative
	// n-vertex test must not cull a box that's only partly outside.
	CHECK(aabb_in_frustum(f, { 10.0, -1.0, -1.0 }, { 12.0, 1.0, 1000.0 }));
}

TEST_CASE("build_frustum tolerates a non-normalized, non-orthogonal up vector") {
	// forward/up need not be unit-length or perpendicular -- both get
	// re-derived internally (see build_frustum's own comment).
	const auto f = build_frustum({ 0.0, 0.0, 0.0 }, { 5.0, 0.0, 0.0 }, { 1.0, 3.0, 0.0 }, 90.0, 1.0, 0.1, 100.0);
	CHECK(aabb_in_frustum(f, { 10.0, -1.0, -1.0 }, { 12.0, 1.0, 1.0 }));
	CHECK_FALSE(aabb_in_frustum(f, { -12.0, -1.0, -1.0 }, { -10.0, 1.0, 1.0 }));
}
