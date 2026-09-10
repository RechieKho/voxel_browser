#pragma once

#include <cmath>
#include <cstdint>

// Small POD math types. vb_core must not depend on raylib/raymath, so these are
// standalone; vb_render converts to/from raylib's Vector3 at its boundary.

namespace vb::core {

template <typename T>
struct Vec2 {
	T x{};
	T y{};

	constexpr Vec2 operator+(const Vec2 &o) const { return { x + o.x, y + o.y }; }
	constexpr Vec2 operator-(const Vec2 &o) const { return { x - o.x, y - o.y }; }
	constexpr Vec2 operator*(T s) const { return { x * s, y * s }; }
	constexpr bool operator==(const Vec2 &) const = default;
};

template <typename T>
struct Vec3 {
	T x{};
	T y{};
	T z{};

	constexpr Vec3 operator+(const Vec3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
	constexpr Vec3 operator-(const Vec3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
	constexpr Vec3 operator*(T s) const { return { x * s, y * s, z * s }; }
	constexpr Vec3 operator-() const { return { -x, -y, -z }; }
	constexpr bool operator==(const Vec3 &) const = default;

	constexpr T dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
	T length() const { return std::sqrt(static_cast<double>(dot(*this))); }
};

using Vec2f = Vec2<float>;
using Vec2d = Vec2<double>;
using Vec3f = Vec3<float>;
using Vec3d = Vec3<double>;
using IVec3 = Vec3<std::int32_t>;

// Axis-aligned bounding box, half-open [min, max).
struct AABB {
	Vec3d min{};
	Vec3d max{};

	constexpr Vec3d size() const { return max - min; }
	constexpr Vec3d center() const {
		return { (min.x + max.x) * 0.5, (min.y + max.y) * 0.5, (min.z + max.z) * 0.5 };
	}
	constexpr bool intersects(const AABB &o) const {
		return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y &&
				max.y > o.min.y && min.z < o.max.z && max.z > o.min.z;
	}
	constexpr AABB translated(const Vec3d &d) const { return { min + d, max + d }; }
};

template <typename T>
constexpr T clamp(T v, T lo, T hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

// Euclidean floor-division / modulo — used for world<->chunk<->local coords so
// negative coordinates map correctly.
constexpr std::int32_t floor_div(std::int32_t a, std::int32_t b) {
	const std::int32_t q = a / b;
	const std::int32_t r = a % b;
	return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}
constexpr std::int32_t floor_mod(std::int32_t a, std::int32_t b) {
	const std::int32_t r = a % b;
	return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
}

} // namespace vb::core
