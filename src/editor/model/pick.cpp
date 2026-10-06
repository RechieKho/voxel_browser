#include "vb/editor/pick.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vb::editor {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double axis_component(core::Vec3d v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

} // namespace

PickResult pick_cell(const Volume &volume, core::Vec3d origin, core::Vec3d dir, const PickOptions &options) {
	PickResult result;
	const core::IVec3 size = volume.size();
	if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
		return result;
	}
	const double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (len < 1e-12) {
		return result;
	}
	dir = { dir.x / len, dir.y / len, dir.z / len };

	// Entry into the volume's bounding box (slab test).
	double t_enter = 0.0;
	double t_exit = kInf;
	const core::Vec3d lo{ 0.0, 0.0, 0.0 };
	const core::Vec3d hi{ static_cast<double>(size.x), static_cast<double>(size.y), static_cast<double>(size.z) };
	int enter_axis = -1;
	bool inside_box = true;
	for (int axis = 0; axis < 3; ++axis) {
		const double o = axis_component(origin, axis);
		const double d = axis_component(dir, axis);
		const double a = axis_component(lo, axis);
		const double b = axis_component(hi, axis);
		if (std::abs(d) < 1e-12) {
			if (o < a || o > b) {
				t_enter = kInf;
				t_exit = -kInf;
			}
			continue;
		}
		double t0 = (a - o) / d;
		double t1 = (b - o) / d;
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		if (t0 > t_enter) {
			t_enter = t0;
			enter_axis = axis;
		}
		t_exit = std::min(t_exit, t1);
	}
	inside_box = t_enter <= 0.0;

	double t_hit_cell = kInf;
	if (t_enter <= t_exit && t_exit >= 0.0) {
		// Start just inside the box and walk the cells (Amanatides-Woo).
		const double t0 = std::max(t_enter, 0.0) + 1e-9;
		const core::Vec3d p{ origin.x + dir.x * t0, origin.y + dir.y * t0, origin.z + dir.z * t0 };
		int cx = std::clamp(static_cast<int>(std::floor(p.x)), 0, size.x - 1);
		int cy = std::clamp(static_cast<int>(std::floor(p.y)), 0, size.y - 1);
		int cz = std::clamp(static_cast<int>(std::floor(p.z)), 0, size.z - 1);
		const int step_x = dir.x > 0 ? 1 : -1;
		const int step_y = dir.y > 0 ? 1 : -1;
		const int step_z = dir.z > 0 ? 1 : -1;
		const auto next_boundary = [&](int c, int step, double d, double o) {
			if (std::abs(d) < 1e-12) {
				return kInf;
			}
			return ((step > 0 ? c + 1 : c) - o) / d;
		};
		double tmax_x = next_boundary(cx, step_x, dir.x, origin.x);
		double tmax_y = next_boundary(cy, step_y, dir.y, origin.y);
		double tmax_z = next_boundary(cz, step_z, dir.z, origin.z);
		const double tdelta_x = std::abs(dir.x) < 1e-12 ? kInf : std::abs(1.0 / dir.x);
		const double tdelta_y = std::abs(dir.y) < 1e-12 ? kInf : std::abs(1.0 / dir.y);
		const double tdelta_z = std::abs(dir.z) < 1e-12 ? kInf : std::abs(1.0 / dir.z);

		core::IVec3 normal{ 0, 0, 0 };
		if (!inside_box) {
			normal = enter_axis == 0 ? core::IVec3{ -step_x, 0, 0 } : (enter_axis == 1 ? core::IVec3{ 0, -step_y, 0 } : core::IVec3{ 0, 0, -step_z });
		}
		double t_cur = t0;
		for (int guard = 0; guard < size.x + size.y + size.z + 4; ++guard) {
			if (cy <= options.max_y && volume.get(cx, cy, cz) != kKeepCell) {
				result.hit = true;
				result.on_cell = true;
				result.cell = { cx, cy, cz };
				result.normal = normal;
				result.place = { cx + normal.x, cy + normal.y, cz + normal.z };
				t_hit_cell = t_cur;
				break;
			}
			if (tmax_x < tmax_y && tmax_x < tmax_z) {
				cx += step_x;
				t_cur = tmax_x;
				tmax_x += tdelta_x;
				normal = { -step_x, 0, 0 };
			} else if (tmax_y < tmax_z) {
				cy += step_y;
				t_cur = tmax_y;
				tmax_y += tdelta_y;
				normal = { 0, -step_y, 0 };
			} else {
				cz += step_z;
				t_cur = tmax_z;
				tmax_z += tdelta_z;
				normal = { 0, 0, -step_z };
			}
			if (cx < 0 || cy < 0 || cz < 0 || cx >= size.x || cy >= size.y || cz >= size.z) {
				break;
			}
		}
	}

	// Ground plane at y = ground_y, within the padded footprint.
	if (std::abs(dir.y) > 1e-12) {
		const double t_ground = (static_cast<double>(options.ground_y) - origin.y) / dir.y;
		if (t_ground > 0.0 && t_ground < t_hit_cell) {
			const double gx = origin.x + dir.x * t_ground;
			const double gz = origin.z + dir.z * t_ground;
			const int pad = options.ground_pad;
			if (gx >= -pad && gx <= size.x + pad && gz >= -pad && gz <= size.z + pad) {
				result.hit = true;
				result.on_cell = false;
				const int gcx = static_cast<int>(std::floor(gx));
				const int gcz = static_cast<int>(std::floor(gz));
				result.cell = { gcx, options.ground_y, gcz };
				result.normal = { 0, dir.y < 0 ? 1 : -1, 0 };
				result.place = { gcx, options.ground_y, gcz };
				return result;
			}
		}
	}
	return result;
}

} // namespace vb::editor
