#include "vb/editor/generators.hpp"

#include <algorithm>
#include <cmath>

#include "vb/core/noise.hpp"
#include "vb/worldgen/det_rng.hpp"

namespace vb::editor {

namespace {

using worldgen::DetRng;

int get_int(const ParamValues &v, const char *key) {
	const auto it = v.find(key);
	return it == v.end() ? 0 : static_cast<int>(std::floor(it->second.number + 0.5));
}

double get_num(const ParamValues &v, const char *key) {
	const auto it = v.find(key);
	return it == v.end() ? 0.0 : it->second.number;
}

const std::string &get_text(const ParamValues &v, const char *key) {
	static const std::string kEmpty;
	const auto it = v.find(key);
	return it == v.end() ? kEmpty : it->second.text;
}

ParamDesc int_param(const char *key, const char *label, double min, double max, double def) {
	ParamDesc d;
	d.key = key;
	d.label = label;
	d.kind = ParamKind::kInt;
	d.min = min;
	d.max = max;
	d.def = def;
	return d;
}

ParamDesc float_param(const char *key, const char *label, double min, double max, double def) {
	ParamDesc d = int_param(key, label, min, max, def);
	d.kind = ParamKind::kFloat;
	return d;
}

ParamDesc block_param(const char *key, const char *label, const char *def) {
	ParamDesc d;
	d.key = key;
	d.label = label;
	d.kind = ParamKind::kBlock;
	d.def_text = def;
	return d;
}

ParamDesc choice_param(const char *key, const char *label, std::vector<std::string> choices, int def) {
	ParamDesc d;
	d.key = key;
	d.label = label;
	d.kind = ParamKind::kChoice;
	d.min = 0;
	d.max = static_cast<double>(choices.size() - 1);
	d.def = def;
	d.choices = std::move(choices);
	return d;
}

// min..max inclusive, tolerating min > max.
int rng_range(DetRng &rng, int lo, int hi) {
	if (hi < lo) {
		std::swap(lo, hi);
	}
	return lo + static_cast<int>(rng.next_index(hi - lo + 1));
}

// The writer all generators share: clears the volume and sets cells, ignoring
// anything outside it.
struct Canvas {
	Volume &volume;
	NameTable &names;

	Canvas(Volume &v, NameTable &n) : volume(v), names(n) { volume = Volume(v.size()); }

	void set(int x, int y, int z, const std::string &block) { volume.set(x, y, z, names.intern(block)); }
	// Only writes into a keep cell, so leaves never overwrite a trunk.
	void set_if_empty(int x, int y, int z, const std::string &block) {
		if (volume.in_bounds(x, y, z) && volume.get(x, y, z) == kKeepCell) {
			set(x, y, z, block);
		}
	}
};

// --- Tree ------------------------------------------------------------------

class TreeGenerator final : public Generator {
public:
	TreeGenerator() :
			params_{ int_param("trunk_min", "Trunk height min", 1, 40, 4), int_param("trunk_max", "Trunk height max", 1, 40, 6),
				block_param("trunk_block", "Trunk block", "base:wood"), block_param("leaf_block", "Leaf block", "base:leaves"),
				choice_param("canopy", "Canopy", { "sphere", "cone", "layered" }, 0), int_param("radius", "Canopy radius", 1, 8, 2),
				float_param("density", "Leaf density", 0.0, 1.0, 0.9), int_param("branches", "Branches", 0, 8, 0) } {}

	std::string id() const override { return "tree"; }
	std::string label() const override { return "Tree"; }
	const std::vector<ParamDesc> &params() const override { return params_; }

	void generate(Volume &volume, core::IVec3 anchor, NameTable &names, const ParamValues &v, std::uint64_t seed) const override {
		Canvas c(volume, names);
		DetRng rng{ seed };
		const int height = rng_range(rng, get_int(v, "trunk_min"), get_int(v, "trunk_max"));
		const int radius = get_int(v, "radius");
		const double density = get_num(v, "density");
		const std::string &trunk = get_text(v, "trunk_block");
		const std::string &leaf = get_text(v, "leaf_block");
		const int shape = get_int(v, "canopy");

		// Trunk first, so the leaf passes (set_if_empty) never cover it.
		for (int i = 0; i < height; ++i) {
			c.set(anchor.x, anchor.y + i, anchor.z, trunk);
		}
		const int top = anchor.y + height - 1;

		const auto leaf_at = [&](int x, int y, int z) {
			if (rng.next01() < density) {
				c.set_if_empty(x, y, z, leaf);
			}
		};
		const auto blob = [&](int cx, int cy, int cz, int r, double flatten) {
			const double limit = (static_cast<double>(r) + 0.35) * (static_cast<double>(r) + 0.35);
			for (int dy = -r; dy <= r; ++dy) {
				for (int dz = -r; dz <= r; ++dz) {
					for (int dx = -r; dx <= r; ++dx) {
						const double dyf = static_cast<double>(dy) * flatten;
						if (static_cast<double>(dx * dx + dz * dz) + dyf * dyf <= limit) {
							leaf_at(cx + dx, cy + dy, cz + dz);
						}
					}
				}
			}
		};

		// Branches: short wood arms in one of the eight horizontal directions,
		// each ending in a small leaf blob.
		const int branches = get_int(v, "branches");
		for (int b = 0; b < branches && height >= 3; ++b) {
			static constexpr int kDirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
			const int dir = static_cast<int>(rng.next_index(8));
			const int at = anchor.y + height / 2 + static_cast<int>(rng.next_index(std::max(1, height - height / 2 - 1)));
			const int length = 2 + static_cast<int>(rng.next_index(2));
			int bx = anchor.x;
			int bz = anchor.z;
			for (int i = 0; i < length; ++i) {
				bx += kDirs[dir][0];
				bz += kDirs[dir][1];
				c.set_if_empty(bx, at + i / 2, bz, trunk);
			}
			blob(bx, at + length / 2, bz, 1, 1.0);
		}

		// Canopy.
		if (shape == 0) { // sphere
			blob(anchor.x, top, anchor.z, radius, 1.0);
		} else if (shape == 1) { // cone: wide at the bottom, a point on top
			const int cone_height = radius * 2 + 1;
			for (int i = 0; i < cone_height; ++i) {
				const int r = radius - (i * radius + cone_height / 2) / cone_height;
				const int y = top - radius + i;
				const double limit = (static_cast<double>(r) + 0.35) * (static_cast<double>(r) + 0.35);
				for (int dz = -r; dz <= r; ++dz) {
					for (int dx = -r; dx <= r; ++dx) {
						if (static_cast<double>(dx * dx + dz * dz) <= limit) {
							leaf_at(anchor.x + dx, y, anchor.z + dz);
						}
					}
				}
			}
		} else { // layered: flat disks of alternating radius, like an acacia
			for (int i = 0; i < 3; ++i) {
				const int r = (i == 1) ? std::max(1, radius - 1) : radius;
				const double limit = (static_cast<double>(r) + 0.35) * (static_cast<double>(r) + 0.35);
				for (int dz = -r; dz <= r; ++dz) {
					for (int dx = -r; dx <= r; ++dx) {
						if (static_cast<double>(dx * dx + dz * dz) <= limit) {
							leaf_at(anchor.x + dx, top - 1 + i, anchor.z + dz);
						}
					}
				}
			}
		}
	}

private:
	std::vector<ParamDesc> params_;
};

// --- Bush ------------------------------------------------------------------

class BushGenerator final : public Generator {
public:
	BushGenerator() :
			params_{ int_param("radius", "Radius", 1, 5, 2), float_param("flatten", "Flatness", 0.5, 2.0, 1.4),
				block_param("leaf_block", "Leaf block", "base:leaves"), block_param("core_block", "Core block", "base:wood"),
				int_param("core", "Wood core (0/1)", 0, 1, 1), float_param("density", "Leaf density", 0.0, 1.0, 0.95) } {}

	std::string id() const override { return "bush"; }
	std::string label() const override { return "Bush"; }
	const std::vector<ParamDesc> &params() const override { return params_; }

	void generate(Volume &volume, core::IVec3 anchor, NameTable &names, const ParamValues &v, std::uint64_t seed) const override {
		Canvas c(volume, names);
		DetRng rng{ seed };
		const int radius = get_int(v, "radius");
		const double flatten = get_num(v, "flatten");
		const double density = get_num(v, "density");
		const std::string &leaf = get_text(v, "leaf_block");
		// The blob sits on the ground: its center is `radius - 1` above the anchor row.
		const int cy = anchor.y + radius - 1;
		const double limit = (static_cast<double>(radius) + 0.35) * (static_cast<double>(radius) + 0.35);
		if (get_int(v, "core") != 0) {
			c.set(anchor.x, anchor.y, anchor.z, get_text(v, "core_block"));
		}
		for (int dy = -radius; dy <= radius; ++dy) {
			for (int dz = -radius; dz <= radius; ++dz) {
				for (int dx = -radius; dx <= radius; ++dx) {
					const double dyf = static_cast<double>(dy) * flatten;
					if (static_cast<double>(dx * dx + dz * dz) + dyf * dyf > limit || cy + dy < anchor.y) {
						continue;
					}
					if (rng.next01() < density) {
						c.set_if_empty(anchor.x + dx, cy + dy, anchor.z + dz, leaf);
					}
				}
			}
		}
	}

private:
	std::vector<ParamDesc> params_;
};

// --- Boulder ---------------------------------------------------------------

class BoulderGenerator final : public Generator {
public:
	BoulderGenerator() :
			params_{ float_param("radius_x", "Radius x", 1.0, 12.0, 3.0), float_param("radius_y", "Radius y", 1.0, 12.0, 2.5),
				float_param("radius_z", "Radius z", 1.0, 12.0, 3.0), float_param("roughness", "Roughness", 0.0, 1.5, 0.6),
				block_param("block_a", "Main block", "base:stone"), block_param("block_b", "Mix-in block", "base:dirt"),
				float_param("mix", "Mix-in share", 0.0, 1.0, 0.15) } {}

	std::string id() const override { return "boulder"; }
	std::string label() const override { return "Boulder"; }
	const std::vector<ParamDesc> &params() const override { return params_; }

	void generate(Volume &volume, core::IVec3 anchor, NameTable &names, const ParamValues &v, std::uint64_t seed) const override {
		Canvas c(volume, names);
		DetRng rng{ seed };
		const double rx = get_num(v, "radius_x");
		const double ry = get_num(v, "radius_y");
		const double rz = get_num(v, "radius_z");
		const double roughness = get_num(v, "roughness");
		const double mix = get_num(v, "mix");
		const std::string &a = get_text(v, "block_a");
		const std::string &b = get_text(v, "block_b");
		// The ellipsoid's lower half is buried one row into the ground: its
		// center is `ry - 1` above the anchor row.
		const double cy = static_cast<double>(anchor.y) + ry - 1.0;
		const int span_x = static_cast<int>(std::ceil(rx)) + 1;
		const int span_y = static_cast<int>(std::ceil(ry)) + 1;
		const int span_z = static_cast<int>(std::ceil(rz)) + 1;
		for (int dy = -span_y; dy <= span_y; ++dy) {
			for (int dz = -span_z; dz <= span_z; ++dz) {
				for (int dx = -span_x; dx <= span_x; ++dx) {
					const int x = anchor.x + dx;
					const int y = static_cast<int>(std::floor(cy)) + dy;
					const int z = anchor.z + dz;
					if (y < anchor.y) {
						continue;
					}
					const double nx = static_cast<double>(dx) / rx;
					const double ny = (static_cast<double>(y) - cy) / ry;
					const double nz = static_cast<double>(dz) / rz;
					const double dist = std::sqrt(nx * nx + ny * ny + nz * nz);
					// Relative to the anchor, so the shape doesn't depend on
					// where in the volume it is rooted.
					const double n = core::noise::value3(seed, static_cast<double>(dx) * 0.45,
							static_cast<double>(y - anchor.y) * 0.45, static_cast<double>(dz) * 0.45);
					if (dist <= 1.0 + (n - 0.5) * roughness) {
						c.set(x, y, z, rng.next01() < mix ? b : a);
					}
				}
			}
		}
	}

private:
	std::vector<ParamDesc> params_;
};

// --- Fallen log ------------------------------------------------------------

class FallenLogGenerator final : public Generator {
public:
	FallenLogGenerator() :
			params_{ int_param("length_min", "Length min", 2, 40, 5), int_param("length_max", "Length max", 2, 40, 8),
				int_param("thick", "Thick (0/1)", 0, 1, 0), block_param("log_block", "Log block", "base:wood"),
				block_param("moss_block", "Moss block", "base:leaves"), float_param("moss", "Moss chance", 0.0, 1.0, 0.25) } {}

	std::string id() const override { return "fallen_log"; }
	std::string label() const override { return "Fallen log"; }
	const std::vector<ParamDesc> &params() const override { return params_; }

	void generate(Volume &volume, core::IVec3 anchor, NameTable &names, const ParamValues &v, std::uint64_t seed) const override {
		Canvas c(volume, names);
		DetRng rng{ seed };
		const int length = rng_range(rng, get_int(v, "length_min"), get_int(v, "length_max"));
		const bool along_x = rng.next_index(2) == 0;
		const bool thick = get_int(v, "thick") != 0;
		const double moss = get_num(v, "moss");
		const std::string &log = get_text(v, "log_block");
		const std::string &moss_block = get_text(v, "moss_block");
		const int half = length / 2;
		const int width = thick ? 2 : 1;
		const int height = thick ? 2 : 1;
		for (int i = 0; i < length; ++i) {
			for (int w = 0; w < width; ++w) {
				for (int h = 0; h < height; ++h) {
					const int along = i - half;
					const int x = along_x ? anchor.x + along : anchor.x + w;
					const int z = along_x ? anchor.z + w : anchor.z + along;
					c.set(x, anchor.y + h, z, log);
				}
			}
			if (rng.next01() < moss) {
				const int along = i - half;
				c.set_if_empty(along_x ? anchor.x + along : anchor.x, anchor.y + height, along_x ? anchor.z : anchor.z + along, moss_block);
			}
		}
	}

private:
	std::vector<ParamDesc> params_;
};

} // namespace

const std::vector<std::unique_ptr<Generator>> &all_generators() {
	static const std::vector<std::unique_ptr<Generator>> kAll = [] {
		std::vector<std::unique_ptr<Generator>> list;
		list.push_back(std::make_unique<TreeGenerator>());
		list.push_back(std::make_unique<BushGenerator>());
		list.push_back(std::make_unique<BoulderGenerator>());
		list.push_back(std::make_unique<FallenLogGenerator>());
		return list;
	}();
	return kAll;
}

const Generator *find_generator(const std::string &id) {
	for (const auto &g : all_generators()) {
		if (g->id() == id) {
			return g.get();
		}
	}
	return nullptr;
}

ParamValues default_params(const Generator &generator) {
	ParamValues out;
	for (const ParamDesc &d : generator.params()) {
		ParamValue value;
		value.number = d.def;
		value.text = d.def_text;
		out[d.key] = value;
	}
	return out;
}

ParamValues normalized_params(const Generator &generator, const ParamValues &values) {
	ParamValues out = default_params(generator);
	for (const ParamDesc &d : generator.params()) {
		const auto it = values.find(d.key);
		if (it == values.end()) {
			continue;
		}
		if (d.kind == ParamKind::kBlock) {
			if (!it->second.text.empty()) {
				out[d.key].text = it->second.text;
			}
		} else {
			out[d.key].number = std::clamp(it->second.number, d.min, d.max);
		}
	}
	return out;
}

std::uint64_t variant_seed(std::uint64_t base_seed, int index) {
	return core::noise::mix64(base_seed ^ core::noise::mix64(static_cast<std::uint64_t>(index) + 0x5EEDB0DEULL));
}

} // namespace vb::editor
