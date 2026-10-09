#include "vb/world/lighting.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "vb/world/paletted_chunk_store.hpp"

namespace vb::world {

namespace {

constexpr int kDim = kChunkDim;

struct Node {
	int x, y, z;
	std::uint8_t level;
};

bool in_bounds(int x, int y, int z) {
	return x >= 0 && x < kDim && y >= 0 && y < kDim && z >= 0 && z < kDim;
}

constexpr std::array<std::array<int, 3>, 6> kNeighbours{ { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
		{ 0, 0, -1 } } };

} // namespace

LightEngine::LightEngine(BlockRegistry registry) : registry_(std::move(registry)) {
	pass_by_id_.resize(registry_.size());
	emit_by_id_.resize(registry_.size());
	for (std::size_t i = 0; i < registry_.size(); ++i) {
		const auto id = static_cast<core::BlockId>(i);
		pass_by_id_[i] = transmittance(id);
		emit_by_id_[i] = registry_.light_emission(id);
	}
}

std::uint8_t LightEngine::emission(core::BlockId block) const {
	return registry_.light_emission(block);
}

std::uint8_t LightEngine::transmittance(core::BlockId block) const {
	const BlockType &t = registry_.get(block);
	if (!t.opaque && !t.liquid) {
		return kMaxLight; // air / glass / leaves-as-cutout: full pass-through
	}
	if (t.liquid) {
		return kMaxLight - 2; // water dims light slightly
	}
	return 0; // opaque
}

void LightEngine::relight_chunk(Chunk &chunk, const Neighbours &neighbours) const {
	auto &blocks = chunk.blocks();
	auto &light = chunk.light_volume();

	auto write_back = [&](auto sky_at, auto blk_at) {
		for (std::size_t i = 0; i < kChunkVolume; ++i) {
			light[i].set_sky(sky_at(i));
			light[i].set_block(blk_at(i));
		}
		chunk.dirty().light = false;
		chunk.dirty().mesh = true;
	};

	// Each voxel's transmittance/emission, read once: the floods below visit
	// every voxel up to six times, and a paletted read plus a registry lookup
	// per visit was most of a relight's cost.
	const std::size_t id_count = pass_by_id_.size();
	auto pass_of = [&](core::BlockId id) {
		const auto i = static_cast<std::size_t>(id);
		return i < id_count ? pass_by_id_[i] : transmittance(id);
	};
	auto emit_of = [&](core::BlockId id) {
		const auto i = static_cast<std::size_t>(id);
		return i < id_count ? emit_by_id_[i] : emission(id);
	};

	// A chunk of one block (solid rock underground, open sky above the
	// terrain) has a closed-form result; the flood below would compute the same.
	core::BlockId only = core::BlockId::kAir;
	if (blocks.uniform_value(&only)) {
		const std::uint8_t pass = pass_of(only);
		const std::uint8_t emit = emit_of(only);
		if (pass == 0) {
			// Nothing enters or spreads; each voxel keeps only its own emission.
			write_back([](std::size_t) { return std::uint8_t{ 0 }; },
					[emit](std::size_t) { return emit; });
			return;
		}
		bool full_sky_from_above = true;
		if (neighbours.above != nullptr) {
			for (int z = 0; z < kDim && full_sky_from_above; ++z) {
				for (int x = 0; x < kDim; ++x) {
					if (neighbours.above->light(x, 0, z).sky() != kMaxLight) {
						full_sky_from_above = false;
						break;
					}
				}
			}
		}
		if (pass == kMaxLight && emit == 0 && full_sky_from_above) {
			// Full sky falls straight down through every voxel; nothing from
			// the sides can exceed it.
			write_back([](std::size_t) { return kMaxLight; },
					[](std::size_t) { return std::uint8_t{ 0 }; });
			return;
		}
	}

	thread_local std::array<std::uint8_t, kChunkVolume> pass_at;
	thread_local std::array<std::uint8_t, kChunkVolume> emit_at;
	for (std::size_t i = 0; i < kChunkVolume; ++i) {
		const core::BlockId id = blocks.get(i);
		pass_at[i] = pass_of(id);
		emit_at[i] = emit_of(id);
	}

	thread_local std::array<std::uint8_t, kChunkVolume> sky;
	thread_local std::array<std::uint8_t, kChunkVolume> blk;
	sky.fill(0);
	blk.fill(0);

	// --- sky light: seed the whole top face, then flood ---------------------
	// No `above` loaded means open sky: every original single-chunk caller's
	// exact behaviour (unattenuated kMaxLight at any transparent top voxel),
	// unchanged -- this is only really correct for the topmost chunk in a
	// column, though, so relight_column() passes the real neighbour whenever
	// one is loaded: `above`'s bottom row (local y = 0) is what's actually
	// arriving into this chunk, attenuated the same way a normal interior
	// step is.
	// FIFO over a reused buffer (same visit order as a std::queue, no
	// per-node allocation).
	thread_local std::vector<Node> q;
	q.clear();
	std::size_t head = 0;
	for (int z = 0; z < kDim; ++z) {
		for (int x = 0; x < kDim; ++x) {
			const std::size_t i = index_of(x, kDim - 1, z);
			const std::uint8_t pass = pass_at[i];
			if (pass == 0) {
				continue;
			}
			std::uint8_t seeded = kMaxLight;
			if (neighbours.above != nullptr) {
				const std::uint8_t incoming = neighbours.above->light(x, 0, z).sky();
				if (incoming == 0) {
					continue;
				}
				const bool straight_down =
						incoming == kMaxLight && pass == kMaxLight;
				const std::uint8_t step = straight_down ? 0 : 1;
				const std::uint8_t drop =
						static_cast<std::uint8_t>(kMaxLight - pass + step);
				if (incoming <= drop) {
					continue;
				}
				seeded = static_cast<std::uint8_t>(incoming - drop);
			}
			sky[i] = seeded;
			q.push_back({ x, kDim - 1, z, seeded });
		}
	}

	// --- sky light: seed the 4 vertical faces from horizontal neighbours ---
	// (cross-chunk horizontal propagation -- e.g. light spilling sideways
	// under an overhang whose own opening is in the neighbouring chunk).
	// Unlike the top face above, a border cell here may already carry a
	// value from the top-face seed (the shared edge/corner column) or an
	// earlier face in this same pass, so this only ever raises a cell's
	// light -- same relax-if-greater rule the interior BFS below uses --
	// never overwrites a higher value with a lower one. No "straight
	// through, no falloff" special case either (unlike straight-down sky
	// light): every horizontal step decays by 1, matching how a purely
	// interior sideways step already behaves within one chunk.
	auto seed_horizontal = [&](const Chunk *neighbour, int fixed_a, bool a_is_x,
								   int neighbour_a) {
		if (neighbour == nullptr) {
			return;
		}
		for (int y = 0; y < kDim; ++y) {
			for (int b = 0; b < kDim; ++b) {
				const int x = a_is_x ? fixed_a : b;
				const int z = a_is_x ? b : fixed_a;
				const std::size_t i = index_of(x, y, z);
				const std::uint8_t pass = pass_at[i];
				if (pass == 0) {
					continue;
				}
				const int nx = a_is_x ? neighbour_a : b;
				const int nz = a_is_x ? b : neighbour_a;
				const std::uint8_t incoming = neighbour->light(nx, y, nz).sky();
				if (incoming == 0) {
					continue;
				}
				const std::uint8_t drop = static_cast<std::uint8_t>(kMaxLight - pass + 1);
				if (incoming <= drop) {
					continue;
				}
				const std::uint8_t seeded = static_cast<std::uint8_t>(incoming - drop);
				if (seeded > sky[i]) {
					sky[i] = seeded;
					q.push_back({ x, y, z, seeded });
				}
			}
		}
	};
	seed_horizontal(neighbours.east, kDim - 1, /*a_is_x=*/true, 0);
	seed_horizontal(neighbours.west, 0, /*a_is_x=*/true, kDim - 1);
	seed_horizontal(neighbours.north, kDim - 1, /*a_is_x=*/false, 0);
	seed_horizontal(neighbours.south, 0, /*a_is_x=*/false, kDim - 1);

	while (head < q.size()) {
		const Node n = q[head++];
		for (const auto &d : kNeighbours) {
			const int nx = n.x + d[0];
			const int ny = n.y + d[1];
			const int nz = n.z + d[2];
			if (!in_bounds(nx, ny, nz)) {
				continue;
			}
			const std::size_t ni = index_of(nx, ny, nz);
			const std::uint8_t pass = pass_at[ni];
			if (pass == 0) {
				continue;
			}
			// Straight down through open air keeps full strength.
			const bool straight_down = d[1] == -1 && n.level == kMaxLight &&
					pass == kMaxLight;
			const std::uint8_t step = straight_down ? 0 : 1;
			const std::uint8_t drop =
					static_cast<std::uint8_t>(kMaxLight - pass + step);
			if (n.level <= drop) {
				continue;
			}
			const std::uint8_t nl = static_cast<std::uint8_t>(n.level - drop);
			if (nl > sky[ni]) {
				sky[ni] = nl;
				q.push_back({ nx, ny, nz, nl });
			}
		}
	}

	// --- block light: seed from every emitter -----------------------------
	q.clear();
	head = 0;
	for (int y = 0; y < kDim; ++y) {
		for (int z = 0; z < kDim; ++z) {
			for (int x = 0; x < kDim; ++x) {
				const std::size_t i = index_of(x, y, z);
				const std::uint8_t e = emit_at[i];
				if (e > 0) {
					blk[i] = e;
					q.push_back({ x, y, z, e });
				}
			}
		}
	}
	while (head < q.size()) {
		const Node n = q[head++];
		for (const auto &d : kNeighbours) {
			const int nx = n.x + d[0];
			const int ny = n.y + d[1];
			const int nz = n.z + d[2];
			if (!in_bounds(nx, ny, nz)) {
				continue;
			}
			const std::size_t ni = index_of(nx, ny, nz);
			if (pass_at[ni] == 0) {
				continue;
			}
			if (n.level <= 1) {
				continue;
			}
			const std::uint8_t nl = static_cast<std::uint8_t>(n.level - 1);
			if (nl > blk[ni]) {
				blk[ni] = nl;
				q.push_back({ nx, ny, nz, nl });
			}
		}
	}

	write_back([](std::size_t i) { return sky[i]; }, [](std::size_t i) { return blk[i]; });
}

} // namespace vb::world
