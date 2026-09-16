#include "vb/world/raycast.hpp"

#include <cmath>
#include <limits>

namespace vb::world {

VoxelRayHit raycast_voxel(const BlockSolidQuery &world, core::Vec3d origin,
		core::Vec3d dir, double max_dist) {
	const double inf = std::numeric_limits<double>::infinity();
	auto fl = [](double v) { return static_cast<int>(std::floor(v)); };
	auto step_of = [](double v) { return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0); };

	int x = fl(origin.x), y = fl(origin.y), z = fl(origin.z);
	const int sx = step_of(dir.x), sy = step_of(dir.y), sz = step_of(dir.z);

	auto t_first = [&](double o, double d, int s) {
		if (s == 0) {
			return inf;
		}
		const double edge = s > 0 ? std::floor(o) + 1.0 - o : o - std::floor(o);
		return edge / std::fabs(d);
	};
	double t_max_x = t_first(origin.x, dir.x, sx);
	double t_max_y = t_first(origin.y, dir.y, sy);
	double t_max_z = t_first(origin.z, dir.z, sz);
	const double t_dx = sx == 0 ? inf : 1.0 / std::fabs(dir.x);
	const double t_dy = sy == 0 ? inf : 1.0 / std::fabs(dir.y);
	const double t_dz = sz == 0 ? inf : 1.0 / std::fabs(dir.z);

	core::IVec3 normal{};
	double t = 0.0;
	for (int i = 0; i < 512 && t <= max_dist; ++i) {
		if (world.solid_at({ x, y, z })) {
			return { true, { x, y, z }, normal };
		}
		if (t_max_x < t_max_y && t_max_x < t_max_z) {
			x += sx;
			t = t_max_x;
			t_max_x += t_dx;
			normal = { -sx, 0, 0 };
		} else if (t_max_y < t_max_z) {
			y += sy;
			t = t_max_y;
			t_max_y += t_dy;
			normal = { 0, -sy, 0 };
		} else {
			z += sz;
			t = t_max_z;
			t_max_z += t_dz;
			normal = { 0, 0, -sz };
		}
	}
	return {};
}

} // namespace vb::world
