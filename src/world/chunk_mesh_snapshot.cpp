#include "vb/world/chunk_mesh_snapshot.hpp"

#include <array>
#include <utility>

#include "vb/core/math.hpp"

namespace vb::world {

namespace {

using core::IVec3;

// Face order: +X -X +Y -Y +Z -Z.
constexpr std::array<IVec3, 6> kFaceNormal{ { { 1, 0, 0 }, { -1, 0, 0 },
		{ 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } } };

// Quad corners per face, CCW when viewed from outside. Each corner is a unit
// offset in chunk-local space added to the voxel's min corner.
constexpr std::array<std::array<IVec3, 4>, 6> kFaceCorners{ {
		{ { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } } }, // +X
		{ { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } } }, // -X
		{ { { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 0 } } }, // +Y
		{ { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } } }, // -Y
		{ { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } } }, // +Z
		{ { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } } }, // -Z
} };

// The two in-plane tangent axes for each face (u, v), used for AO neighbour
// sampling.
constexpr std::array<std::array<IVec3, 2>, 6> kFaceTangents{ {
		{ { { 0, 0, 1 }, { 0, 1, 0 } } }, // +X
		{ { { 0, 0, 1 }, { 0, 1, 0 } } }, // -X
		{ { { 1, 0, 0 }, { 0, 0, 1 } } }, // +Y
		{ { { 1, 0, 0 }, { 0, 0, 1 } } }, // -Y
		{ { { 1, 0, 0 }, { 0, 1, 0 } } }, // +Z
		{ { { 1, 0, 0 }, { 0, 1, 0 } } }, // -Z
} };

// A corner's sign along a tangent axis, derived from its own coordinates
// rather than a hand-matched lookup table -- see chunk_mesher.cpp's original
// comment; unchanged here, just moved.
int corner_sign(const IVec3 &corner, const IVec3 &tangent) {
	const int dot = corner.x * tangent.x + corner.y * tangent.y + corner.z * tangent.z;
	return dot != 0 ? 1 : -1;
}

// raylib's Mesh.indices is `unsigned short*` -- a hard 16-bit vertex cap per
// chunk mesh. See chunk_mesher.cpp's original comment for why this is needed.
inline constexpr std::size_t kMaxMeshVertices = 65532; // multiple of 4, < 65536

float ao_level(bool side1, bool side2, bool corner) {
	if (side1 && side2) {
		return 0.0f;
	}
	return static_cast<float>(3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner)));
}

// Maps one padded axis coordinate (in [-1, kChunkDim]) to which of the 3
// chunks along that axis it falls in (-1/0/+1) and its local coordinate
// within that chunk.
std::pair<int, int> split_padded(int p) {
	if (p < 0) {
		return { -1, kChunkDim - 1 };
	}
	if (p >= kChunkDim) {
		return { 1, 0 };
	}
	return { 0, p };
}

} // namespace

ChunkMeshSnapshot build_chunk_mesh_snapshot(const ClientChunkStore &store, core::ChunkCoord coord) {
	ChunkMeshSnapshot snapshot;
	snapshot.coord = coord;

	const Chunk *chunk = store.find(coord);
	if (chunk == nullptr) {
		return snapshot; // loaded == false, nothing else to fill in
	}
	snapshot.loaded = true;
	snapshot.revision = chunk->revision();
	snapshot.blocks.resize(kMeshSnapshotVolume);
	snapshot.lights.resize(kMeshSnapshotVolume);

	// This runs on the main thread (the only thread allowed to touch
	// ClientChunkStore, see the header) -- ClientChunkStore::block_at/
	// light_at() each do a coord hashmap lookup, and calling them per padded
	// voxel meant ~2 * kMeshSnapshotVolume (~78k) lookups per chunk, which
	// showed up as a real per-frame FPS hit while chunks stream in. Instead,
	// resolve each of the (up to) 27 neighbour chunks once up front and index
	// straight into them -- O(1) array reads for the other ~39k voxels.
	std::array<const Chunk *, 27> neighbours{};
	for (int dz = -1; dz <= 1; ++dz) {
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				const std::size_t ni = static_cast<std::size_t>(dx + 1) +
						3 * (static_cast<std::size_t>(dy + 1) + 3 * static_cast<std::size_t>(dz + 1));
				neighbours[ni] = (dx == 0 && dy == 0 && dz == 0)
						? chunk
						: store.find({ coord.x + dx, coord.y + dy, coord.z + dz });
			}
		}
	}

	for (int py = -1; py <= kChunkDim; ++py) {
		const auto [dy, ly] = split_padded(py);
		for (int pz = -1; pz <= kChunkDim; ++pz) {
			const auto [dz, lz] = split_padded(pz);
			for (int px = -1; px <= kChunkDim; ++px) {
				const auto [dx, lx] = split_padded(px);
				const std::size_t ni = static_cast<std::size_t>(dx + 1) +
						3 * (static_cast<std::size_t>(dy + 1) + 3 * static_cast<std::size_t>(dz + 1));
				const Chunk *c = neighbours[ni];
				const std::size_t idx = padded_index(px, py, pz);
				if (c == nullptr) {
					snapshot.blocks[idx] = core::BlockId::kAir;
					Light full;
					full.set_sky(15); // matches ClientChunkStore::light_at()'s unloaded default
					snapshot.lights[idx] = full;
				} else {
					snapshot.blocks[idx] = c->get(lx, ly, lz);
					snapshot.lights[idx] = c->light(lx, ly, lz);
				}
			}
		}
	}
	return snapshot;
}

MeshData mesh_chunk_from_snapshot(const ChunkMeshSnapshot &snapshot, const BlockRegistry &reg) {
	MeshData mesh;
	if (!snapshot.loaded) {
		return mesh;
	}

	const auto block_local = [&](IVec3 p) {
		return snapshot.blocks[padded_index(p.x, p.y, p.z)];
	};
	const auto blocks_face = [&](IVec3 p) {
		const core::BlockId id = block_local(p);
		return reg.is_opaque(id) || reg.is_liquid(id);
	};
	// Phase 7.3 fix: whether a face is *culled* depends on the current
	// voxel's own type, not just the neighbour's -- blocks_face() above
	// (kept as-is for AO sampling, where "is there occluding stuff here"
	// is the right generic question) can't answer that alone. A liquid
	// neighbour should cull a liquid voxel's own face (merges into one
	// water body, matches "liquid blocks cull faces against each other"
	// below) but must NOT cull an opaque voxel's face -- an opaque block
	// sitting in/under water still needs its water-facing side drawn, or
	// every submerged block goes invisible (the "can't see underwater
	// terrain" bug). An opaque neighbour still culls both kinds, same as
	// always.
	const auto face_culled = [&](core::BlockId current, IVec3 outside) {
		const core::BlockId neighbor = block_local(outside);
		if (reg.is_opaque(neighbor)) {
			return true;
		}
		return reg.is_liquid(current) && reg.is_liquid(neighbor);
	};
	const auto light_local = [&](IVec3 p) {
		return snapshot.lights[padded_index(p.x, p.y, p.z)];
	};

	for (int ly = 0; ly < kChunkDim; ++ly) {
		for (int lz = 0; lz < kChunkDim; ++lz) {
			for (int lx = 0; lx < kChunkDim; ++lx) {
				const IVec3 lv{ lx, ly, lz };
				const core::BlockId block = block_local(lv);
				if (block == core::BlockId::kAir) {
					continue;
				}

				for (int f = 0; f < 6; ++f) {
					if (mesh.vertices.size() + 4 > kMaxMeshVertices) {
						return mesh; // hit the 16-bit index cap; see the comment above
					}
					const IVec3 n = kFaceNormal[static_cast<std::size_t>(f)];
					const IVec3 outside{ lv.x + n.x, lv.y + n.y, lv.z + n.z };
					if (face_culled(block, outside)) {
						continue; // culled
					}

					const auto fu = static_cast<std::size_t>(f);
					const IVec3 tu = kFaceTangents[fu][0];
					const IVec3 tv = kFaceTangents[fu][1];
					const Light lgt = light_local(outside);
					const float base_light =
							static_cast<float>(lgt.max()) / 15.0f * 0.85f + 0.15f;

					const std::uint32_t first =
							static_cast<std::uint32_t>(mesh.vertices.size());
					for (int c = 0; c < 4; ++c) {
						const IVec3 corner =
								kFaceCorners[fu][static_cast<std::size_t>(c)];
						const int su = corner_sign(corner, tu);
						const int sv = corner_sign(corner, tv);

						// AO from the 3 voxels around this corner, in the face's
						// outward plane.
						const IVec3 a{ outside.x + tu.x * su, outside.y + tu.y * su,
							outside.z + tu.z * su };
						const IVec3 bpt{ outside.x + tv.x * sv,
							outside.y + tv.y * sv, outside.z + tv.z * sv };
						const IVec3 d{ outside.x + tu.x * su + tv.x * sv,
							outside.y + tu.y * su + tv.y * sv,
							outside.z + tu.z * su + tv.z * sv };
						const float ao = ao_level(blocks_face(a), blocks_face(bpt),
								blocks_face(d));
						const float vlight = base_light * (0.55f + 0.15f * ao);

						MeshVertex vert;
						vert.px = static_cast<float>(lx + corner.x);
						vert.py = static_cast<float>(ly + corner.y);
						vert.pz = static_cast<float>(lz + corner.z);
						vert.nx = static_cast<float>(n.x);
						vert.ny = static_cast<float>(n.y);
						vert.nz = static_cast<float>(n.z);
						vert.u = (c == 1 || c == 2) ? 1.0f : 0.0f;
						vert.v = (c >= 2) ? 1.0f : 0.0f;
						vert.light = vlight;
						vert.block_id = static_cast<std::uint32_t>(block);
						mesh.vertices.push_back(vert);
					}

					mesh.indices.push_back(first + 0);
					mesh.indices.push_back(first + 1);
					mesh.indices.push_back(first + 2);
					mesh.indices.push_back(first + 0);
					mesh.indices.push_back(first + 2);
					mesh.indices.push_back(first + 3);
				}
			}
		}
	}

	return mesh;
}

} // namespace vb::world
