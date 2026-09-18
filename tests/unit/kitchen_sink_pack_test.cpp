// Phase 6.15: loads the real `content/examples/kitchen_sink` files (not
// inline Lua strings) through the same vb::script::load_content_pack path
// src/server/main.cpp uses -- same regression-coverage role
// content_pack_test.cpp plays for content/base. Guards against a future edit
// to this demo pack silently breaking it, and doubles as a smoke check that
// every Phase 6 API it demonstrates actually took effect.

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "vb/net/loopback.hpp"
#include "vb/physics/movement.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/world/block.hpp"
#include "vb/worldgen/generator.hpp"

namespace {

std::filesystem::path kitchen_sink_pack_dir() {
	return std::filesystem::path(VB_PROJECT_SOURCE_DIR) / "content" / "examples" /
			"kitchen_sink";
}

std::filesystem::path temp_storage(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_kitchen_sink_test_") + name + ".json");
	std::filesystem::remove(p);
	return p;
}

} // namespace

#if !VB_WITH_LUA

TEST_CASE("load_content_pack degrades gracefully for kitchen_sink without VB_WITH_LUA") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("disabled"));
	CHECK(vb::script::load_content_pack(rt, kitchen_sink_pack_dir()));
}

#else

TEST_CASE("content/examples/kitchen_sink loads cleanly and registers its new block") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	const std::size_t base_size = registry.size();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("ok"));

	REQUIRE(vb::script::load_content_pack(rt, kitchen_sink_pack_dir()));
	rt.freeze();

	// One genuinely new block (kitchen_sink:unstable_ore) beyond base().
	CHECK(registry.size() == base_size + 1);
	const vb::core::BlockId ore_id = registry.find("kitchen_sink:unstable_ore");
	CHECK(ore_id != vb::world::base_block::air);
	const auto &ore = registry.get(ore_id);
	CHECK(ore.max_damage == 6);
	CHECK(ore.max_stack == 999);
	CHECK(ore.pickup_radius == doctest::Approx(4.0));
	CHECK(ore.drop_lifetime_seconds == doctest::Approx(20.0));
}

TEST_CASE("content/examples/kitchen_sink's Phase 6 overrides all take effect") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("overrides"));
	REQUIRE(vb::script::load_content_pack(rt, kitchen_sink_pack_dir()));
	rt.freeze();

	// Phase 6.7: vb.physics.set_params.
	const vb::physics::MoveParams move =
			rt.effective_move_params(vb::physics::MoveParams{});
	CHECK(move.gravity == doctest::Approx(18.0));
	CHECK(move.jump_speed == doctest::Approx(9.0));
	CHECK(move.sprint_speed == doctest::Approx(9.5));

	// Phase 6.8: vb.daynight.set_curve / set_day_length.
	const auto curve = rt.effective_day_night_curve();
	REQUIRE(curve.has_value());
	CHECK(curve->keyframes.size() == 5);
	CHECK(rt.effective_day_length_seconds(1200.0) == doctest::Approx(180.0));

	// Phase 6.14: vb.worldgen.set_pipeline + vb.register_biome.
	vb::worldgen::WorldGenParams base;
	base.seed = 0xC0FFEEULL;
	const auto pipeline = rt.build_worldgen_pipeline(base);
	REQUIRE(pipeline != nullptr);
	CHECK(pipeline->biomes.biome_count() == 2);
	CHECK(pipeline->carvers.size() == 1);
	CHECK(pipeline->veins.size() == 1);
	CHECK(pipeline->veins[0].block == registry.find("kitchen_sink:unstable_ore"));

	// The pack-driven pipeline actually produces different terrain from the
	// fixed default (same shape as pack_runtime_test.cpp's own worldgen
	// pipeline test).
	const vb::worldgen::WorldGenerator pack_gen(base, registry, pipeline);
	const vb::worldgen::WorldGenerator default_gen(base, registry);
	vb::world::Chunk pack_chunk({ 0, 1, 0 });
	pack_gen.generate(pack_chunk);
	vb::world::Chunk default_chunk({ 0, 1, 0 });
	default_gen.generate(default_chunk);
	bool any_block_differs = false;
	for (std::size_t i = 0; i < vb::world::kChunkVolume; ++i) {
		if (pack_chunk.blocks().get(i) != default_chunk.blocks().get(i)) {
			any_block_differs = true;
			break;
		}
	}
	CHECK(any_block_differs);
}

#endif // VB_WITH_LUA
