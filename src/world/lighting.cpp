#include "vb/world/lighting.hpp"

#include <array>
#include <cstdint>
#include <queue>

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

void LightEngine::relight_chunk(Chunk &chunk) const {
	auto &blocks = chunk.blocks();
	auto &light = chunk.light_volume();

	std::array<std::uint8_t, kChunkVolume> sky{};
	std::array<std::uint8_t, kChunkVolume> blk{};

	// --- sky light: seed the whole top face, then flood ---------------------
	std::queue<Node> q;
	for (int z = 0; z < kDim; ++z) {
		for (int x = 0; x < kDim; ++x) {
			const std::size_t i = index_of(x, kDim - 1, z);
			if (transmittance(blocks.get(i)) > 0) {
				sky[i] = kMaxLight;
				q.push({ x, kDim - 1, z, kMaxLight });
			}
		}
	}
	while (!q.empty()) {
		const Node n = q.front();
		q.pop();
		for (const auto &d : kNeighbours) {
			const int nx = n.x + d[0];
			const int ny = n.y + d[1];
			const int nz = n.z + d[2];
			if (!in_bounds(nx, ny, nz)) {
				continue;
			}
			const std::size_t ni = index_of(nx, ny, nz);
			const std::uint8_t pass = transmittance(blocks.get(ni));
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
				q.push({ nx, ny, nz, nl });
			}
		}
	}

	// --- block light: seed from every emitter -----------------------------
	for (int y = 0; y < kDim; ++y) {
		for (int z = 0; z < kDim; ++z) {
			for (int x = 0; x < kDim; ++x) {
				const std::size_t i = index_of(x, y, z);
				const std::uint8_t e = registry_.light_emission(blocks.get(i));
				if (e > 0) {
					blk[i] = e;
					q.push({ x, y, z, e });
				}
			}
		}
	}
	while (!q.empty()) {
		const Node n = q.front();
		q.pop();
		for (const auto &d : kNeighbours) {
			const int nx = n.x + d[0];
			const int ny = n.y + d[1];
			const int nz = n.z + d[2];
			if (!in_bounds(nx, ny, nz)) {
				continue;
			}
			const std::size_t ni = index_of(nx, ny, nz);
			if (transmittance(blocks.get(ni)) == 0) {
				continue;
			}
			if (n.level <= 1) {
				continue;
			}
			const std::uint8_t nl = static_cast<std::uint8_t>(n.level - 1);
			if (nl > blk[ni]) {
				blk[ni] = nl;
				q.push({ nx, ny, nz, nl });
			}
		}
	}

	for (std::size_t i = 0; i < kChunkVolume; ++i) {
		light[i].set_sky(sky[i]);
		light[i].set_block(blk[i]);
	}
	chunk.dirty().light = false;
	chunk.dirty().mesh = true;
}

} // namespace vb::world
