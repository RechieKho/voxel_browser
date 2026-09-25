#include <doctest/doctest.h>

#include "vb/render/entity_visual_layout.hpp"

using namespace vb::render;
using vb::protocol::EntityClipDef;
using vb::protocol::EntityVisualDef;

namespace {

EntityVisualDef make_def() {
	EntityVisualDef def;
	def.texture = "textures/entities/slime.png";
	def.frame_width = 128;
	def.frame_height = 128;
	def.facings = 8;
	def.origin_x = 0.5f;
	def.origin_y = 1.0f;
	def.clips.push_back({ "idle", 4, 6.0f });
	def.clips.push_back({ "walk", 6, 10.0f });
	return def;
}

} // namespace

TEST_CASE("build_entity_visual_layout resolves running column offsets and row count") {
	const EntityVisualDef def = make_def();
	// facings=8 -> rows = 8/2+1 = 5; total frames = 4+6=10 columns.
	const auto layout = build_entity_visual_layout(def, 128 * 10, 128 * 5);
	REQUIRE(layout.has_value());
	CHECK(layout->frame_width == 128);
	CHECK(layout->frame_height == 128);
	CHECK(layout->rows == 5);
	REQUIRE(layout->clips.size() == 2);
	CHECK(layout->clips[0].name == "idle");
	CHECK(layout->clips[0].start_frame == 0);
	CHECK(layout->clips[1].name == "walk");
	CHECK(layout->clips[1].start_frame == 4); // running sum past "idle"'s 4 frames
}

TEST_CASE("build_entity_visual_layout resolves facings=4 to 3 rows") {
	EntityVisualDef def = make_def();
	def.facings = 4;
	const auto layout = build_entity_visual_layout(def, 128 * 10, 128 * 3);
	REQUIRE(layout.has_value());
	CHECK(layout->rows == 3);
}

TEST_CASE("build_entity_visual_layout rejects a width mismatch") {
	const EntityVisualDef def = make_def();
	CHECK_FALSE(build_entity_visual_layout(def, 128 * 9, 128 * 5).has_value());
}

TEST_CASE("build_entity_visual_layout rejects a height mismatch") {
	const EntityVisualDef def = make_def();
	CHECK_FALSE(build_entity_visual_layout(def, 128 * 10, 128 * 4).has_value());
}

TEST_CASE("build_entity_visual_layout rejects an empty clip list") {
	EntityVisualDef def = make_def();
	def.clips.clear();
	CHECK_FALSE(build_entity_visual_layout(def, 128 * 10, 128 * 5).has_value());
}

TEST_CASE("resolve_clip finds an exact name match") {
	const EntityVisualDef def = make_def();
	const auto layout = build_entity_visual_layout(def, 128 * 10, 128 * 5);
	REQUIRE(layout.has_value());
	CHECK(resolve_clip(*layout, "walk").start_frame == 4);
}

TEST_CASE("resolve_clip falls back to the first declared clip when the name is absent") {
	const EntityVisualDef def = make_def();
	const auto layout = build_entity_visual_layout(def, 128 * 10, 128 * 5);
	REQUIRE(layout.has_value());
	// def has no "dead" clip -- falls back to "idle" (first declared).
	CHECK(resolve_clip(*layout, "dead").name == "idle");
}
