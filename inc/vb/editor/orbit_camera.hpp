#pragma once

#include <algorithm>
#include <cmath>

#include "vb/core/math.hpp"

// Orbit/pan/zoom camera around a target point (docs/structure-editor.md §I).
// Pure math, so it is unit-tested without a window; the editor app turns
// eye()/target() into a raylib Camera3D.

namespace vb::editor {

class OrbitCamera {
public:
	void set_target(core::Vec3d t) { target_ = t; }
	core::Vec3d target() const { return target_; }
	double distance() const { return distance_; }
	double yaw() const { return yaw_; }
	double pitch() const { return pitch_; }

	// Frames a structure of `size` blocks centered on `center`.
	void fit(core::Vec3d center, core::Vec3d size) {
		target_ = center;
		const double extent = std::max({ size.x, size.y, size.z, 1.0 });
		distance_ = std::clamp(extent * 2.2, kMinDistance, kMaxDistance);
	}

	// Mouse deltas in pixels.
	void orbit(double dx, double dy) {
		yaw_ = std::fmod(yaw_ - dx * kOrbitSpeed, 360.0);
		pitch_ = std::clamp(pitch_ + dy * kOrbitSpeed, -89.0, 89.0);
	}
	// Moves the target in the camera's screen plane.
	void pan(double dx, double dy) {
		const double scale = distance_ * kPanSpeed;
		const double cy = std::cos(radians(yaw_));
		const double sy = std::sin(radians(yaw_));
		const core::Vec3d right{ cy, 0.0, -sy };
		target_.x -= right.x * dx * scale;
		target_.z -= right.z * dx * scale;
		target_.y += dy * scale;
	}
	// Wheel notches; positive zooms in.
	void zoom(double notches) {
		distance_ = std::clamp(distance_ * std::pow(kZoomFactor, -notches), kMinDistance, kMaxDistance);
	}

	core::Vec3d eye() const {
		const double cp = std::cos(radians(pitch_));
		return { target_.x + distance_ * cp * std::sin(radians(yaw_)),
			target_.y + distance_ * std::sin(radians(pitch_)),
			target_.z + distance_ * cp * std::cos(radians(yaw_)) };
	}

	static constexpr double kMinDistance = 2.0;
	static constexpr double kMaxDistance = 400.0;

private:
	static constexpr double radians(double deg) { return deg * 3.14159265358979323846 / 180.0; }
	static constexpr double kOrbitSpeed = 0.4;
	static constexpr double kPanSpeed = 0.0016;
	static constexpr double kZoomFactor = 1.12;

	core::Vec3d target_{ 0.0, 0.0, 0.0 };
	double distance_ = 20.0;
	double yaw_ = 35.0;
	double pitch_ = 30.0;
};

} // namespace vb::editor
