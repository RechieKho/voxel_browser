#include <doctest/doctest.h>

#include <ostream>

#include "vb/protocol/world.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/chunk_codec.hpp"
#include "vb/world/chunk_mesher.hpp"
#include "vb/world/client_chunk_store.hpp"
#include "vb/world/lighting.hpp"
#include "vb/world/paletted_chunk_store.hpp"

using namespace vb::world;
using vb::core::ChunkCoord;

namespace {

// Push a chunk straight into a ClientChunkStore via the wire codec.
void put(ClientChunkStore &store, const Chunk &chunk) {
	vb::protocol::S2CChunkAdd add;
	add.coord = chunk.coord();
	add.revision = 1;
	add.payload = encode_chunk_payload(chunk);
	REQUIRE(store.apply_add(add));
}

} // namespace

TEST_CASE("an unloaded chunk meshes to nothing") {
	ClientChunkStore store(BlockRegistry::base());
	CHECK(mesh_chunk(store, { 0, 0, 0 }).empty());
}

TEST_CASE("a single isolated block emits 6 quads (36 indices, 24 verts)") {
	Chunk c({ 0, 0, 0 });
	c.blocks().set(16, 16, 16, base_block::stone);
	LightEngine(BlockRegistry::base()).relight_chunk(c);

	ClientChunkStore store(BlockRegistry::base());
	put(store, c);

	const MeshData m = mesh_chunk(store, { 0, 0, 0 });
	CHECK(m.quad_count() == 6);
	CHECK(m.vertices.size() == 24);
	CHECK(m.indices.size() == 36);

	// Every vertex sits on the cube [16,17]^3.
	for (const auto &v : m.vertices) {
		CHECK(v.px >= 16.0f);
		CHECK(v.px <= 17.0f);
		CHECK(v.block_id == static_cast<std::uint32_t>(base_block::stone));
	}
}

TEST_CASE("shared faces between two solid blocks are culled") {
	Chunk c({ 0, 0, 0 });
	c.blocks().set(4, 4, 4, base_block::stone);
	c.blocks().set(5, 4, 4, base_block::stone);
	LightEngine(BlockRegistry::base()).relight_chunk(c);

	ClientChunkStore store(BlockRegistry::base());
	put(store, c);

	// 2 blocks * 6 faces - 2 touching faces = 10 quads.
	CHECK(mesh_chunk(store, { 0, 0, 0 }).quad_count() == 10);
}

TEST_CASE("a full solid chunk only meshes its outer shell") {
	Chunk c({ 0, 0, 0 });
	c.blocks().fill(base_block::stone);
	// no neighbours loaded -> all 6 outer faces exposed
	ClientChunkStore store(BlockRegistry::base());
	put(store, c);

	// 6 faces * 32*32 = 6144 quads.
	CHECK(mesh_chunk(store, { 0, 0, 0 }).quad_count() == 6144);
}

TEST_CASE("a neighbour chunk culls the shared border faces") {
	ClientChunkStore store(BlockRegistry::base());

	Chunk a({ 0, 0, 0 });
	a.blocks().fill(base_block::stone);
	put(store, a);

	Chunk b({ 1, 0, 0 }); // +X neighbour
	b.blocks().fill(base_block::stone);
	put(store, b);

	// a's +X face (32*32 quads) is now culled by b.
	CHECK(mesh_chunk(store, { 0, 0, 0 }).quad_count() == 6144 - 1024);
}

TEST_CASE("transparent leaves do not cull neighbouring faces") {
	Chunk c({ 0, 0, 0 });
	c.blocks().set(8, 8, 8, base_block::stone);
	c.blocks().set(9, 8, 8, base_block::leaves); // non-opaque
	LightEngine(BlockRegistry::base()).relight_chunk(c);

	ClientChunkStore store(BlockRegistry::base());
	put(store, c);

	// stone keeps all 6 faces (leaves are not opaque, so they don't cull);
	// leaves emit 5 (their face buried in the opaque stone is culled).
	CHECK(mesh_chunk(store, { 0, 0, 0 }).quad_count() == 11);
}

TEST_CASE("darker light yields darker vertices") {
	auto reg = BlockRegistry::base();
	Chunk c({ 0, 0, 0 });
	// A stone box with a lit top and a shadowed underside.
	for (int z = 0; z < kChunkDim; ++z) {
		for (int x = 0; x < kChunkDim; ++x) {
			c.blocks().set(x, 10, z, base_block::stone);
		}
	}
	LightEngine(reg).relight_chunk(c);
	ClientChunkStore store(reg);
	put(store, c);

	const MeshData m = mesh_chunk(store, { 0, 0, 0 });
	float top = 0.0f;
	float bottom = 1.0f;
	for (const auto &v : m.vertices) {
		if (v.ny > 0.5f) {
			top = std::max(top, v.light);
		}
		if (v.ny < -0.5f) {
			bottom = std::min(bottom, v.light);
		}
	}
	CHECK(top > bottom);
}
