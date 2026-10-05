#include <algorithm>
#include <cmath>
#include <tuple>

#include "vb/world/paletted_chunk_store.hpp"
#include "vb/worldgen/det_rng.hpp"
#include "vb/worldgen/generator.hpp"

// Rule-based, cross-chunk structure placement (docs/structure-editor.md §E).
//
// Anchors belong to world columns, not chunks: every anchor of a rule comes
// from a global jittered grid keyed only by (world seed, grid cell, rule), and
// is validated against pre-decoration terrain only (WorldGenerator::
// block_at_pregen and the height field). So any chunk can recompute the
// placements that reach it without looking at a neighbor chunk, workers stay
// independent, and every vertical chunk of a column agrees on where trees
// are. Each chunk "pulls" the placements whose footprint overlaps it and
// stamps just its own cells, in a canonical order, so overlaps resolve the
// same way in every chunk.

namespace vb::worldgen {

using world::kChunkDim;

namespace {

int floor_div(int a, int b) {
	int q = a / b;
	if (a % b != 0 && ((a < 0) != (b < 0))) {
		--q;
	}
	return q;
}

constexpr std::uint64_t kRuleSalt = 0xDEC0000000000000ULL;
// Wavelength of the low-frequency noise `cluster` gates placement with.
constexpr double kClusterScale = 1.0 / 48.0;
// Footprint columns sampled for the slope check (bounded so a 64x64 structure
// doesn't cost 4096 height evaluations per candidate).
constexpr std::size_t kMaxSlopeSamples = 32;
// A rule whose max_slope is this large imposes no slope limit.
constexpr int kNoSlopeLimit = 1 << 19;

} // namespace

std::vector<StructurePlacement> WorldGenerator::structure_placements(
		int x0, int z0, int x1, int z1) const {
	std::vector<StructurePlacement> out;
	if (!pipeline_ || pipeline_->biomes.empty() || x0 > x1 || z0 > z1) {
		return out;
	}
	const PackWorldGenPipeline &pipe = *pipeline_;
	const std::size_t biome_count = std::min(pipe.decoration.size(), pipe.biomes.biome_count());

	std::uint32_t flat = 0;
	for (std::size_t b = 0; b < biome_count; ++b) {
		const auto &rules = pipe.decoration[b];
		for (std::size_t ri = 0; ri < rules.size(); ++ri, ++flat) {
			const PlacementRule &rule = rules[ri];
			if (rule.structure >= pipe.structures.size() || rule.spawn_rate <= 0.0) {
				continue;
			}
			const StructureDef &def = pipe.structures[rule.structure];
			if (def.variants.empty()) {
				continue;
			}
			double total_weight = 0.0;
			for (const StructureVariant &v : def.variants) {
				total_weight += v.weight;
			}

			// Grid cell side 2*spacing with the anchor jittered over
			// [0, spacing] inside it: anchors in neighboring cells are at
			// least `spacing` apart on some axis.
			const int spacing = std::max(1, rule.min_spacing);
			const int cell = 2 * spacing;
			const double keep_base = rule.spawn_rate * static_cast<double>(cell) *
					static_cast<double>(cell) / static_cast<double>(kChunkDim * kChunkDim);
			const std::uint64_t salt = kRuleSalt + flat;

			for (int gz = floor_div(z0 - spacing, cell); gz <= floor_div(z1, cell); ++gz) {
				for (int gx = floor_div(x0 - spacing, cell); gx <= floor_div(x1, cell); ++gx) {
					DetRng rng{ core::noise::hash3(params_.seed, gx, gz, salt) };
					const int ax = gx * cell + static_cast<int>(rng.next_index(spacing + 1));
					const int az = gz * cell + static_cast<int>(rng.next_index(spacing + 1));
					const double keep_roll = rng.next01();
					const double variant_roll = rng.next01();
					const int rotation = rule.rotate ? static_cast<int>(rng.next_index(4)) : 0;
					const bool mirror = rule.mirror ? rng.next_index(2) == 1 : false;
					if (ax < x0 || ax > x1 || az < z0 || az > z1) {
						continue;
					}

					double keep = keep_base;
					if (rule.cluster > 0.0) {
						const double n = core::noise::value2(
								params_.seed ^ salt, static_cast<double>(ax) * kClusterScale,
								static_cast<double>(az) * kClusterScale);
						keep *= (1.0 - rule.cluster) + rule.cluster * 2.0 * n;
					}
					if (keep_roll >= keep) {
						continue;
					}

					const Column col = column_at(ax, az);
					if (col.biome != b) {
						continue;
					}
					if (col.height < pipe.sea_level || col.height < rule.y_min ||
							col.height > rule.y_max) {
						continue;
					}
					const core::BlockId ground = pregen_block(col, ax, col.height, az);
					if (rule.on.empty()) {
						if (ground == air_ || ground == water_) {
							continue;
						}
					} else if (std::find(rule.on.begin(), rule.on.end(), ground) == rule.on.end()) {
						continue;
					}
					if (pregen_block(col, ax, col.height + 1, az) != air_) {
						continue;
					}

					std::uint32_t variant = 0;
					double target = variant_roll * total_weight;
					for (std::size_t vi = 0; vi < def.variants.size(); ++vi) {
						variant = static_cast<std::uint32_t>(vi);
						target -= def.variants[vi].weight;
						if (target < 0.0) {
							break;
						}
					}

					if (rule.max_slope < kNoSlopeLimit) {
						const StructureVariant &sv = def.variants[variant];
						std::vector<std::pair<int, int>> footprint;
						for (int cz = 0; cz < sv.size.z; ++cz) {
							for (int cx = 0; cx < sv.size.x; ++cx) {
								for (int cy = 0; cy < sv.size.y; ++cy) {
									if (sv.at(cx, cy, cz) != kKeepCell) {
										footprint.emplace_back(cx - def.anchor.x, cz - def.anchor.z);
										break;
									}
								}
							}
						}
						const std::size_t stride =
								std::max<std::size_t>(1, (footprint.size() + kMaxSlopeSamples - 1) / kMaxSlopeSamples);
						int lo = col.height;
						int hi = col.height;
						for (std::size_t i = 0; i < footprint.size(); i += stride) {
							int tx = 0;
							int tz = 0;
							transform_offset(footprint[i].first, footprint[i].second, rotation, mirror,
									tx, tz);
							const int h = surface_height(ax + tx, az + tz);
							lo = std::min(lo, h);
							hi = std::max(hi, h);
						}
						if (hi - lo > rule.max_slope) {
							continue;
						}
					}

					StructurePlacement placement;
					placement.x = ax;
					placement.z = az;
					placement.ground_y = col.height;
					placement.biome = static_cast<std::uint32_t>(b);
					placement.rule = static_cast<std::uint32_t>(ri);
					placement.variant = variant;
					placement.rotation = rotation;
					placement.mirror = mirror;
					out.push_back(placement);
				}
			}
		}
	}

	std::sort(out.begin(), out.end(), [](const StructurePlacement &a, const StructurePlacement &b) {
		return std::tie(a.x, a.z, a.biome, a.rule) < std::tie(b.x, b.z, b.biome, b.rule);
	});
	return out;
}

void WorldGenerator::stamp_structures(world::Chunk &chunk) const {
	if (!pipeline_ || pipeline_->biomes.empty()) {
		return;
	}
	const PackWorldGenPipeline &pipe = *pipeline_;
	int max_radius = 0;
	for (const auto &rules : pipe.decoration) {
		for (const PlacementRule &rule : rules) {
			if (rule.structure < pipe.structures.size()) {
				max_radius = std::max(max_radius, pipe.structures[rule.structure].radius_xz);
			}
		}
	}

	const core::IVec3 origin = core::chunk_origin(chunk.coord());
	const std::vector<StructurePlacement> placements = structure_placements(
			origin.x - max_radius, origin.z - max_radius, origin.x + kChunkDim - 1 + max_radius,
			origin.z + kChunkDim - 1 + max_radius);
	world::PalettedChunkStore &blocks = chunk.blocks();

	for (const StructurePlacement &p : placements) {
		const PlacementRule &rule = pipe.decoration[p.biome][p.rule];
		const StructureDef &def = pipe.structures[rule.structure];
		const StructureVariant &variant = def.variants[p.variant];

		// Vertical reach: the anchor cell sits one block above the ground.
		const int base_y = p.ground_y + 1 - def.anchor.y;
		if (base_y + variant.size.y - 1 < origin.y || base_y > origin.y + kChunkDim - 1) {
			continue;
		}
		for (int cy = 0; cy < variant.size.y; ++cy) {
			const int ly = base_y + cy - origin.y;
			if (ly < 0 || ly >= kChunkDim) {
				continue;
			}
			for (int cz = 0; cz < variant.size.z; ++cz) {
				for (int cx = 0; cx < variant.size.x; ++cx) {
					const core::BlockId cell = variant.at(cx, cy, cz);
					if (cell == kKeepCell) {
						continue;
					}
					int tx = 0;
					int tz = 0;
					transform_offset(cx - def.anchor.x, cz - def.anchor.z, p.rotation, p.mirror, tx, tz);
					const int lx = p.x + tx - origin.x;
					const int lz = p.z + tz - origin.z;
					if (lx < 0 || lx >= kChunkDim || lz < 0 || lz >= kChunkDim) {
						continue;
					}
					const auto idx = world::index_of(lx, ly, lz);
					// An explicit air cell carves; anything else is gated
					// by the rule's replace policy.
					if (cell != air_) {
						const core::BlockId current = blocks.get(idx);
						const bool allowed = rule.replace == ReplacePolicy::kAll ||
								current == air_ ||
								(rule.replace == ReplacePolicy::kAirAndPlants &&
										pipe.is_replaceable(current));
						if (!allowed) {
							continue;
						}
					}
					blocks.set(idx, cell);
				}
			}
		}
	}
}

} // namespace vb::worldgen
