#include "vb/physics/movement.hpp"

#include <algorithm>
#include <cmath>

#include "vb/core/math.hpp"

namespace vb::physics {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSkin = 1e-4; // shrink the box off voxel seams
constexpr double kMaxStepDist = 0.4; // substep so one move stays sub-block
constexpr double kMaxDt = 0.1;

int ifloor(double v) { return static_cast<int>(std::floor(v)); }

bool box_hits_solid(const core::AABB &box, const world::BlockSolidQuery &world) {
	const int x0 = ifloor(box.min.x + kSkin);
	const int x1 = ifloor(box.max.x - kSkin);
	const int y0 = ifloor(box.min.y + kSkin);
	const int y1 = ifloor(box.max.y - kSkin);
	const int z0 = ifloor(box.min.z + kSkin);
	const int z1 = ifloor(box.max.z - kSkin);
	for (int x = x0; x <= x1; ++x) {
		for (int y = y0; y <= y1; ++y) {
			for (int z = z0; z <= z1; ++z) {
				if (world.solid_at(core::IVec3{ x, y, z })) {
					return true;
				}
			}
		}
	}
	return false;
}

bool hits(core::Vec3d feet, const MoveParams &p,
		const world::BlockSolidQuery &world) {
	return box_hits_solid(player_box(feet, p), world);
}

struct SweepResult {
	core::Vec3d pos;
	bool blocked = false;
};

// Move `pos` by `delta` (expected axis-aligned). On contact, bisect to sit flush
// against the surface.
SweepResult sweep(core::Vec3d pos, core::Vec3d delta, const MoveParams &p,
		const world::BlockSolidQuery &world) {
	core::Vec3d np{ pos.x + delta.x, pos.y + delta.y, pos.z + delta.z };
	if (!hits(np, p, world)) {
		return { np, false };
	}
	core::Vec3d lo = pos;
	core::Vec3d hi = np;
	for (int k = 0; k < 16; ++k) {
		const core::Vec3d mid{ (lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5,
			(lo.z + hi.z) * 0.5 };
		if (hits(mid, p, world)) {
			hi = mid;
		} else {
			lo = mid;
		}
	}
	return { lo, true };
}

// Horizontal move along one axis; when blocked and `can_step`, try climbing a
// ledge up to step_height and settling back down onto it.
void resolve_horizontal(MoveState &s, core::Vec3d delta, bool can_step,
		const MoveParams &p, const world::BlockSolidQuery &world) {
	const SweepResult flat = sweep(s.position, delta, p, world);
	if (!flat.blocked) {
		s.position = flat.pos;
		return;
	}

	if (can_step) {
		const core::Vec3d lifted{ s.position.x, s.position.y + p.step_height,
			s.position.z };
		if (!hits(lifted, p, world)) {
			const SweepResult over = sweep(lifted, delta, p, world);
			if (!over.blocked) {
				const SweepResult down = sweep(over.pos,
						core::Vec3d{ 0.0, -(p.step_height + 0.05), 0.0 }, p, world);
				s.position = down.pos;
				s.on_ground = true;
				return;
			}
		}
	}

	s.position = flat.pos;
	if (delta.x != 0.0) {
		s.velocity.x = 0.0;
	}
	if (delta.z != 0.0) {
		s.velocity.z = 0.0;
	}
}

} // namespace

core::AABB player_box(core::Vec3d feet, const MoveParams &params) {
	return core::AABB{
		core::Vec3d{ feet.x - params.half_width, feet.y,
				feet.z - params.half_width },
		core::Vec3d{ feet.x + params.half_width, feet.y + params.height,
				feet.z + params.half_width },
	};
}

core::Vec3d wish_dir_from_local(core::Vec3f local_move, float yaw_degrees) {
	const double yaw = static_cast<double>(yaw_degrees) * kPi / 180.0;
	const double sy = std::sin(yaw);
	const double cy = std::cos(yaw);
	// forward (-Z at yaw 0): (sy, 0, -cy); right: (cy, 0, sy)
	const double strafe = static_cast<double>(local_move.x);
	const double fwd = static_cast<double>(local_move.z);
	core::Vec3d d{ cy * strafe + sy * fwd, 0.0, sy * strafe - cy * fwd };
	const double len = std::sqrt(d.x * d.x + d.z * d.z);
	if (len > 1.0) {
		d.x /= len;
		d.z /= len;
	}
	return d;
}

MoveState step_movement(const MoveState &state, const MoveInput &input,
		const MoveParams &params, const world::BlockSolidQuery &world) {
	MoveState out = state;
	const double dt = core::clamp(input.dt, 0.0, kMaxDt);
	if (dt <= 0.0) {
		return out;
	}

	core::Vec3d wish = input.wish_dir;
	const double wlen = std::sqrt(wish.x * wish.x + wish.z * wish.z);
	const bool has_wish = wlen > 1e-6;
	if (wlen > 1.0) {
		wish.x /= wlen;
		wish.z /= wlen;
	}

	if (params.fly) {
		out.velocity.x = wish.x * params.fly_speed;
		out.velocity.z = wish.z * params.fly_speed;
		const double vy = (input.fly_up ? 1.0 : 0.0) - (input.fly_down ? 1.0 : 0.0);
		out.velocity.y = vy * params.fly_speed;
		out.on_ground = false;
	} else {
		const double target_speed =
				input.sprint ? params.sprint_speed : params.walk_speed;
		const core::Vec3d wish_vel{ has_wish ? wish.x * target_speed : 0.0, 0.0,
			has_wish ? wish.z * target_speed : 0.0 };

		// Ground friction — only decelerate when there is no active wish
		// direction (skid to a stop on release). Applying it unconditionally
		// fought the acceleration step below every tick and capped the
		// reachable top speed at accel/friction, well under walk_speed and
		// identical regardless of sprint — sprint had no effect.
		if (out.on_ground && !has_wish) {
			const double sp = std::sqrt(out.velocity.x * out.velocity.x +
					out.velocity.z * out.velocity.z);
			if (sp > 0.0) {
				double ns = sp - sp * params.friction * dt;
				if (ns < 0.0) {
					ns = 0.0;
				}
				out.velocity.x *= ns / sp;
				out.velocity.z *= ns / sp;
			}
		}

		// Accelerate toward the wish velocity.
		const double a = out.on_ground ? params.accel : params.air_accel;
		core::Vec3d dv{ wish_vel.x - out.velocity.x, 0.0,
			wish_vel.z - out.velocity.z };
		const double dvlen = std::sqrt(dv.x * dv.x + dv.z * dv.z);
		const double maxd = a * dt;
		if (dvlen > maxd && dvlen > 0.0) {
			dv.x *= maxd / dvlen;
			dv.z *= maxd / dvlen;
		}
		out.velocity.x += dv.x;
		out.velocity.z += dv.z;

		// Gravity.
		out.velocity.y -= params.gravity * dt;
		if (out.velocity.y < -params.terminal_velocity) {
			out.velocity.y = -params.terminal_velocity;
		}

		// Jump.
		if (input.jump && out.on_ground) {
			out.velocity.y = params.jump_speed;
			out.on_ground = false;
		}
	}

	// Integrate with per-axis voxel collision, substepped to stay sub-block.
	core::Vec3d disp{ out.velocity.x * dt, out.velocity.y * dt,
		out.velocity.z * dt };
	const double biggest = std::max({ std::fabs(disp.x), std::fabs(disp.y),
			std::fabs(disp.z) });
	const int steps =
			std::max(1, static_cast<int>(std::ceil(biggest / kMaxStepDist)));
	core::Vec3d step_disp{ disp.x / steps, disp.y / steps, disp.z / steps };

	bool grounded = false;
	for (int i = 0; i < steps; ++i) {
		if (step_disp.y != 0.0) {
			const SweepResult r = sweep(out.position,
					core::Vec3d{ 0.0, step_disp.y, 0.0 }, params, world);
			out.position = r.pos;
			if (r.blocked) {
				if (out.velocity.y <= 0.0) {
					grounded = true;
				}
				out.velocity.y = 0.0;
				step_disp.y = 0.0;
			}
		}
		const bool can_step = out.on_ground || grounded;
		resolve_horizontal(out, core::Vec3d{ step_disp.x, 0.0, 0.0 }, can_step,
				params, world);
		resolve_horizontal(out, core::Vec3d{ 0.0, 0.0, step_disp.z }, can_step,
				params, world);
	}

	// Ground probe just below the feet.
	if (!grounded && out.velocity.y <= 0.0) {
		core::Vec3d probe = out.position;
		probe.y -= 0.02;
		if (hits(probe, params, world)) {
			grounded = true;
		}
	}
	out.on_ground = params.fly ? false : grounded;
	return out;
}

} // namespace vb::physics
