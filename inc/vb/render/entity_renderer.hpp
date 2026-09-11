#pragma once

#include <cstddef>
#include <memory>

#include "vb/core/math.hpp"

// Draws a Y-axis-billboarded sprite for each replicated remote entity (spec
// §11.3). Phase 3 ships a single hardcoded flat-tinted placeholder frame (no
// pack/Lua dependency); the direction-bucket + animation-clip machinery
// (vb/render/entity_visual.hpp) is already wired, so swapping in real
// per-entity-kind atlases later (Phase 4.2/4.4/5.1) only changes how a frame
// is looked up, not this class's structure.
//
// Like ChunkRenderer, only ever construct this when the window isn't
// headless -- the constructor uploads a GPU texture.

namespace vb::net {
class ClientSession;
}

namespace vb::render {

struct CameraView {
	core::Vec3d position{};
	core::Vec3d target{};
};

class EntityRenderer {
public:
	EntityRenderer();
	~EntityRenderer();

	EntityRenderer(const EntityRenderer &) = delete;
	EntityRenderer &operator=(const EntityRenderer &) = delete;

	// Refresh per-entity animation clip + facing from `client`'s replicated
	// remote entities (spec §8.4's remote_entities()/interpolated_pos()). Call
	// once per frame, before draw().
	void sync(const net::ClientSession &client, const CameraView &camera,
			double dt_seconds);

	// Draw a billboard for every tracked entity. Call inside
	// BeginMode3D/EndMode3D.
	void draw(const CameraView &camera) const;

	std::size_t tracked_count() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace vb::render
