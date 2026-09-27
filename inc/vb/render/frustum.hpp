#pragma once

#include <array>
#include <cmath>

#include "vb/core/math.hpp"

// Pure view-frustum construction + AABB test (Phase 2 remaining item: chunk
// frustum culling). No raylib dependency -- header-only, unit-tested without
// a GL context, same posture as entity_visual_layout.hpp. The GL-facing side
// (raylib Camera3D -> position/forward/up + aspect, and the actual
// chunk-list culling) lives in chunk_renderer.cpp, the only caller.

namespace vb::render {

// A half-space: points p with dot(normal, p) >= distance are on the inside.
struct FrustumPlane {
	core::Vec3d normal{};
	double distance = 0.0;
};

// near/far/right/left/top/bottom, in no particular order -- aabb_in_frustum
// tests all 6 uniformly.
struct Frustum {
	std::array<FrustumPlane, 6> planes{};
};

namespace detail {

constexpr core::Vec3d frustum_cross(const core::Vec3d &a, const core::Vec3d &b) {
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline core::Vec3d frustum_normalized(const core::Vec3d &v) {
	const double len = v.length();
	return len > 1e-9 ? core::Vec3d{ v.x / len, v.y / len, v.z / len } : core::Vec3d{};
}

} // namespace detail

// Builds a frustum straight from camera basis vectors, via the standard
// "cross product of the far-plane corner vectors" construction -- avoids
// extracting planes from a combined view-projection matrix entirely, which
// would tie this pure header to raylib/rlgl's internal row/column matrix
// convention. `forward`/`up` need not be normalized or orthogonal to each
// other (an internal right/real-up basis is re-derived); `fovy_deg` is the
// full vertical field of view in degrees (matches raylib's Camera3D::fovy
// for CAMERA_PERSPECTIVE); `aspect` is viewport width/height.
inline Frustum build_frustum(core::Vec3d position, core::Vec3d forward, core::Vec3d up,
		double fovy_deg, double aspect, double near_dist, double far_dist) {
	constexpr double kPi = 3.14159265358979323846;
	const core::Vec3d f = detail::frustum_normalized(forward);
	const core::Vec3d right = detail::frustum_normalized(detail::frustum_cross(f, up));
	const core::Vec3d real_up = detail::frustum_cross(right, f);

	const double half_v = far_dist * std::tan(fovy_deg * 0.5 * (kPi / 180.0));
	const double half_h = half_v * aspect;
	const core::Vec3d front_far = f * far_dist;

	Frustum out;
	out.planes[0] = { f, f.dot(position + f * near_dist) }; // near
	out.planes[1] = { -f, (-f).dot(position + front_far) }; // far
	{
		const core::Vec3d n = detail::frustum_cross(front_far - right * half_h, real_up);
		out.planes[2] = { n, n.dot(position) }; // right
	}
	{
		const core::Vec3d n = detail::frustum_cross(real_up, front_far + right * half_h);
		out.planes[3] = { n, n.dot(position) }; // left
	}
	{
		const core::Vec3d n = detail::frustum_cross(right, front_far - real_up * half_v);
		out.planes[4] = { n, n.dot(position) }; // top
	}
	{
		const core::Vec3d n = detail::frustum_cross(front_far + real_up * half_v, right);
		out.planes[5] = { n, n.dot(position) }; // bottom
	}
	return out;
}

// True unless [min,max] is provably entirely on the outside of at least one
// plane -- the standard "positive vertex" AABB-vs-plane test. Conservative:
// some boxes just outside a frustum corner still pass, but nothing actually
// inside the frustum is ever wrongly culled.
inline bool aabb_in_frustum(const Frustum &frustum, core::Vec3d min, core::Vec3d max) {
	for (const FrustumPlane &plane : frustum.planes) {
		const core::Vec3d p{
			plane.normal.x >= 0.0 ? max.x : min.x,
			plane.normal.y >= 0.0 ? max.y : min.y,
			plane.normal.z >= 0.0 ? max.z : min.z,
		};
		if (plane.normal.dot(p) < plane.distance) {
			return false;
		}
	}
	return true;
}

} // namespace vb::render
