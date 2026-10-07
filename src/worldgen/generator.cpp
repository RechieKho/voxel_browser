#include "vb/worldgen/generator.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "vb/core/math.hpp"
#include "vb/world/paletted_chunk_store.hpp"
#include "vb/worldgen/det_rng.hpp"

namespace vb::worldgen {

using world::kChunkDim;

WorldGenerator::WorldGenerator(WorldGenParams params,
		const world::BlockRegistry &registry,
		std::shared_ptr<const PackWorldGenPipeline> pipeline) : params_(params),
																air_(core::BlockId::kAir),
																stone_(registry.find("base:stone")),
																dirt_(registry.find("base:dirt")),
																grass_(registry.find("base:grass")),
																sand_(registry.find("base:sand")),
																water_(registry.find("base:water")),
																pipeline_(std::move(pipeline)) {}

int WorldGenerator::surface_height(int world_x, int world_z) const {
	if (pipeline_) {
		return static_cast<int>(std::floor(
				pipeline_->height_field(static_cast<double>(world_x),
						static_cast<double>(world_z))));
	}
	const double n = core::noise::fbm2(params_.seed,
			static_cast<double>(world_x), static_cast<double>(world_z),
			params_.height_noise);
	// n in [0,1) -> centred [-1,1) -> scaled around base_height.
	const double h = params_.base_height + (n * 2.0 - 1.0) * params_.amplitude;
	return static_cast<int>(std::floor(h));
}

WorldGenerator::Column WorldGenerator::column_at(int world_x, int world_z) const {
	const PackWorldGenPipeline &pipe = *pipeline_;
	Column col;
	col.height = static_cast<int>(std::floor(pipe.height_field(world_x, world_z)));
	col.surface = grass_;
	col.filler = dirt_;
	col.stone = stone_;
	if (!pipe.biomes.empty()) {
		col.biome = pipe.biomes.resolve(world_x, world_z);
		const auto &biome = pipe.biomes.biome(col.biome);
		col.surface = biome.surface != core::BlockId::kAir ? biome.surface : col.surface;
		col.filler = biome.filler != core::BlockId::kAir ? biome.filler : col.filler;
		col.stone = biome.stone != core::BlockId::kAir ? biome.stone : col.stone;
	}
	if (pipe.beach != core::BlockId::kAir && col.height <= pipe.sea_level + 1) {
		col.surface = pipe.beach;
		col.filler = pipe.beach;
	}
	return col;
}

core::BlockId WorldGenerator::pregen_block(
		const Column &col, int world_x, int world_y, int world_z) const {
	const PackWorldGenPipeline &pipe = *pipeline_;
	core::BlockId block = air_;
	if (world_y > col.height) {
		block = (world_y <= pipe.sea_level) ? water_ : air_;
	} else if (world_y == col.height) {
		block = col.surface;
	} else if (world_y >= col.height - pipe.soil_depth) {
		block = col.filler;
	} else {
		block = col.stone;
	}

	// Carvers: any carver's density at/above its threshold in this voxel's
	// y-range carves solid, non-water rock/soil back to air (caves never carve
	// through the water table or the biome's own surface crust look).
	if (block != air_ && block != water_) {
		for (const CarverDef &carver : pipe.carvers) {
			if (world_y < carver.y_min || world_y > carver.y_max) {
				continue;
			}
			if (carver.density(static_cast<double>(world_x), static_cast<double>(world_y),
						static_cast<double>(world_z)) >= carver.threshold) {
				block = air_;
				break;
			}
		}
	}
	return block;
}

core::BlockId WorldGenerator::block_at_pregen(int world_x, int world_y, int world_z) const {
	if (pipeline_) {
		return pregen_block(column_at(world_x, world_z), world_x, world_y, world_z);
	}
	const int height = surface_height(world_x, world_z);
	const bool beach = height <= params_.sea_level + 1;
	if (world_y > height) {
		return (world_y <= params_.sea_level) ? water_ : air_;
	}
	if (world_y == height) {
		return beach ? sand_ : grass_;
	}
	if (world_y >= height - params_.soil_depth) {
		return beach ? sand_ : dirt_;
	}
	return stone_;
}

void WorldGenerator::generate(world::Chunk &chunk) const {
	const core::IVec3 origin = core::chunk_origin(chunk.coord());
	world::PalettedChunkStore &blocks = chunk.blocks();

	if (!pipeline_) {
		// Fixed default path -- byte-identical to every pre-6.14 build, so
		// the worldgen_test.cpp golden hash stays valid unless a pack
		// actually opts into vb.worldgen.set_pipeline.
		for (int lz = 0; lz < kChunkDim; ++lz) {
			for (int lx = 0; lx < kChunkDim; ++lx) {
				const int wx = origin.x + lx;
				const int wz = origin.z + lz;
				const int height = surface_height(wx, wz);
				const bool beach = height <= params_.sea_level + 1;

				for (int ly = 0; ly < kChunkDim; ++ly) {
					const int wy = origin.y + ly;
					core::BlockId block = air_;

					if (wy > height) {
						block = (wy <= params_.sea_level) ? water_ : air_;
					} else if (wy == height) {
						block = beach ? sand_ : grass_;
					} else if (wy >= height - params_.soil_depth) {
						block = beach ? sand_ : dirt_;
					} else {
						block = stone_;
					}

					if (block != air_) {
						blocks.set(world::index_of(lx, ly, lz), block);
					}
				}
			}
		}
	} else {
		const PackWorldGenPipeline &pipe = *pipeline_;

		for (int lz = 0; lz < kChunkDim; ++lz) {
			for (int lx = 0; lx < kChunkDim; ++lx) {
				const int wx = origin.x + lx;
				const int wz = origin.z + lz;
				const Column col = column_at(wx, wz);

				for (int ly = 0; ly < kChunkDim; ++ly) {
					const core::BlockId block = pregen_block(col, wx, origin.y + ly, wz);
					if (block != air_) {
						blocks.set(world::index_of(lx, ly, lz), block);
					}
				}
			}
		}

		// Vein/scatter pass (spec §6 stage 5) -- deterministic per-chunk RNG,
		// a small random-walk "blob" per vein instance that only replaces
		// `target_rock`.
		const std::uint64_t chunk_seed = core::noise::hash3(
				params_.seed, chunk.coord().x, chunk.coord().y, chunk.coord().z);
		for (std::size_t vi = 0; vi < pipe.veins.size(); ++vi) {
			const VeinDef &vein = pipe.veins[vi];
			DetRng rng{ chunk_seed ^ (0xF00D000000000000ULL + vi) };
			int count = static_cast<int>(vein.spawn_rate);
			if (rng.next01() < vein.spawn_rate - static_cast<double>(count)) {
				++count;
			}
			for (int n = 0; n < count; ++n) {
				const int ax = static_cast<int>(rng.next_index(kChunkDim));
				const int az = static_cast<int>(rng.next_index(kChunkDim));
				const int height_span =
						std::max(1, vein.height_max - vein.height_min + 1);
				int ay = vein.height_min + static_cast<int>(rng.next_index(height_span)) -
						origin.y;
				int cx = ax;
				int cy = ay;
				int cz = az;
				for (int step = 0; step < vein.vein_size; ++step) {
					if (cx >= 0 && cx < kChunkDim && cy >= 0 && cy < kChunkDim &&
							cz >= 0 && cz < kChunkDim) {
						const auto idx = world::index_of(cx, cy, cz);
						if (blocks.get(idx) == vein.target_rock) {
							blocks.set(idx, vein.block);
						}
					}
					cx += static_cast<int>(rng.next_index(3)) - 1;
					cy += static_cast<int>(rng.next_index(3)) - 1;
					cz += static_cast<int>(rng.next_index(3)) - 1;
				}
			}
		}

		// Decoration pass (spec §6 stage 6): pull every structure placement
		// that reaches this chunk (structure_placement.cpp).
		stamp_structures(chunk);
	}

	chunk.dirty().terrain = true;
	chunk.dirty().light = true;
	chunk.dirty().mesh = true;
	chunk.set_gen_state(world::GenState::kGenerated);
}

int WorldGenerator::sea_level() const {
	return pipeline_ ? pipeline_->sea_level : params_.sea_level;
}

namespace {

// Generates (and caches) whole chunks so a spawn candidate can be checked
// against the real world -- structures and carvers included -- instead of the
// bare heightmap.
class GeneratedVoxels {
public:
	explicit GeneratedVoxels(const WorldGenerator &gen) : gen_(gen) {}

	core::BlockId at(int x, int y, int z) {
		const core::IVec3 voxel{ x, y, z };
		const core::ChunkCoord coord = core::chunk_of(voxel);
		auto it = chunks_.find(coord);
		if (it == chunks_.end()) {
			world::Chunk chunk(coord);
			gen_.generate(chunk);
			it = chunks_.emplace(coord, std::move(chunk)).first;
		}
		const core::IVec3 local = core::local_of(voxel);
		return it->second.get(local.x, local.y, local.z);
	}

private:
	const WorldGenerator &gen_;
	std::map<core::ChunkCoord, world::Chunk> chunks_;
};

} // namespace

core::Vec3d default_spawn_position(const WorldGenerator &gen, int spawn_x,
		int spawn_z) {
	const int sea_level = gen.sea_level();
	const core::BlockId air = core::BlockId::kAir;
	GeneratedVoxels voxels(gen);

	// A column is a spawn spot when its surface is dry land and, in the
	// generated world, the surface voxel is still solid with two voxels of
	// air above it (a standing player is under two voxels tall). The
	// heightmap alone misses whatever the later passes put there: a tree
	// trunk or boulder standing on that exact column, or a cave carved
	// through the surface.
	const auto standable = [&](int x, int z, int surface) {
		const core::BlockId floor = voxels.at(x, surface, z);
		return floor != air && voxels.at(x, surface + 1, z) == air &&
				voxels.at(x, surface + 2, z) == air;
	};

	// Spiral outward in growing square rings from (spawn_x, spawn_z) until a
	// standable column turns up. Ring 0 is just the starting column itself;
	// ring r visits the perimeter of the (2r+1)x(2r+1) square around it.
	// Capped well beyond any plausible island/ocean size so a pathological
	// seed can't spin forever -- falls back to the first dry-land column seen
	// (or the starting column) if nothing standable turns up.
	constexpr int kMaxRing = 256;
	std::optional<core::IVec3> dry_land;
	const auto try_column = [&](int x, int z) -> std::optional<core::IVec3> {
		const int surface = gen.surface_height(x, z);
		if (surface <= sea_level) {
			return std::nullopt;
		}
		if (!dry_land) {
			dry_land = core::IVec3{ x, surface, z };
		}
		if (!standable(x, z, surface)) {
			return std::nullopt;
		}
		return core::IVec3{ x, surface, z };
	};

	std::optional<core::IVec3> found = try_column(spawn_x, spawn_z);
	for (int r = 1; !found && r <= kMaxRing; ++r) {
		const int x0 = spawn_x - r;
		const int x1 = spawn_x + r;
		const int z0 = spawn_z - r;
		const int z1 = spawn_z + r;
		for (int x = x0; !found && x <= x1; ++x) {
			for (int z = z0; !found && z <= z1; ++z) {
				// Perimeter only -- interior columns were already visited by
				// smaller rings.
				if (x != x0 && x != x1 && z != z0 && z != z1) {
					continue;
				}
				found = try_column(x, z);
			}
		}
	}

	core::IVec3 spot{ spawn_x, gen.surface_height(spawn_x, spawn_z), spawn_z };
	if (found) {
		spot = *found;
	} else if (dry_land) {
		spot = *dry_land;
	}
	// Feet one voxel above the topmost solid block (occupies [surface,
	// surface+1)), centred in the column.
	return { static_cast<double>(spot.x) + 0.5,
		static_cast<double>(spot.y) + 1.0, static_cast<double>(spot.z) + 0.5 };
}

} // namespace vb::worldgen
