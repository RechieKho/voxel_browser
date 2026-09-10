#pragma once

#include <cmath>

#include "vb/core/math.hpp"

// First-person camera controller (spec §11.4). Pure math, no raylib — the client
// fills LookMoveInput from raylib each frame and converts the result to a
// raylib Camera3D at the render boundary. Header-only so it is unit-tested
// without linking the render library. Double precision throughout to match the
// world's dvec3 positions and keep the strict warning set (-Wdouble-promotion)
// quiet.

namespace vb::render {

struct LookMoveInput {
	// Mouse motion in pixels since the last frame (x = yaw, y = pitch).
	core::Vec2d look_delta{};
	// Desired motion, each component in [-1, 1]:
	//   x = strafe (right positive), y = vertical (up positive),
	//   z = forward (into the screen positive).
	core::Vec3d move_axis{};
	bool sprint = false;
};

class FirstPersonController {
public:
	void set_position(core::Vec3d p) { position_ = p; }
	void set_look(double yaw_deg, double pitch_deg) {
		yaw_ = yaw_deg;
		pitch_ = clamp_pitch(pitch_deg);
	}
	void set_sensitivity(double deg_per_pixel) { sensitivity_ = deg_per_pixel; }
	void set_speed(double units_per_second) { speed_ = units_per_second; }
	void set_sprint_multiplier(double m) { sprint_multiplier_ = m; }

	core::Vec3d position() const { return position_; }
	double yaw() const { return yaw_; }
	double pitch() const { return pitch_; }

	// Unit forward vector (where the camera looks), y-up to match raylib.
	core::Vec3d forward() const {
		const double cy = std::cos(radians(yaw_));
		const double sy = std::sin(radians(yaw_));
		const double cp = std::cos(radians(pitch_));
		const double sp = std::sin(radians(pitch_));
		return { sy * cp, sp, -cy * cp };
	}

	// Point the camera targets, one unit ahead of the eye.
	core::Vec3d target() const {
		const core::Vec3d f = forward();
		return { position_.x + f.x, position_.y + f.y, position_.z + f.z };
	}

	void update(const LookMoveInput &in, double dt_seconds) {
		yaw_ = std::fmod(yaw_ + in.look_delta.x * sensitivity_, 360.0);
		pitch_ = clamp_pitch(pitch_ - in.look_delta.y * sensitivity_);

		// Horizontal basis (ignore pitch so looking up doesn't slow you down).
		const double cy = std::cos(radians(yaw_));
		const double sy = std::sin(radians(yaw_));
		const core::Vec3d fwd{ sy, 0.0, -cy };
		const core::Vec3d right{ cy, 0.0, sy };

		core::Vec3d delta{
			right.x * in.move_axis.x + fwd.x * in.move_axis.z,
			in.move_axis.y,
			right.z * in.move_axis.x + fwd.z * in.move_axis.z,
		};
		const double horiz = std::sqrt(delta.x * delta.x + delta.z * delta.z);
		if (horiz > 1.0) {
			delta.x /= horiz;
			delta.z /= horiz;
		}

		const double speed =
				speed_ * (in.sprint ? sprint_multiplier_ : 1.0) * dt_seconds;
		position_.x += delta.x * speed;
		position_.y += delta.y * speed;
		position_.z += delta.z * speed;
	}

private:
	static constexpr double radians(double deg) {
		return deg * 3.14159265358979323846 / 180.0;
	}
	static constexpr double clamp_pitch(double p) {
		return core::clamp(p, -89.0, 89.0);
	}

	core::Vec3d position_{ 0.0, 0.0, 0.0 };
	double yaw_ = 0.0;
	double pitch_ = 0.0;
	double sensitivity_ = 0.12;
	double speed_ = 8.0;
	double sprint_multiplier_ = 2.0;
};

} // namespace vb::render
