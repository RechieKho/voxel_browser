#include "vb/render/entity_renderer.hpp"

#include <cstdint>
#include <unordered_map>

#include <raylib.h>

#include "vb/core/ids.hpp"
#include "vb/net/session.hpp"
#include "vb/render/entity_visual.hpp"

namespace vb::render {

namespace {

constexpr int kDefaultFacings = 8;
constexpr float kPlaceholderWidth = 0.8f; // metres
constexpr float kPlaceholderHeight = 1.8f; // matches physics::MoveParams::height

Camera3D to_raylib_camera(const CameraView &v) {
	Camera3D cam{};
	cam.position = { static_cast<float>(v.position.x),
		static_cast<float>(v.position.y), static_cast<float>(v.position.z) };
	cam.target = { static_cast<float>(v.target.x),
		static_cast<float>(v.target.y), static_cast<float>(v.target.z) };
	cam.up = { 0.0f, 1.0f, 0.0f };
	cam.fovy = 60.0f; // unused by DrawBillboardPro's view-matrix math
	cam.projection = CAMERA_PERSPECTIVE;
	return cam;
}

// A cheap, deterministic hash -> hue so distinct entities are visually
// distinguishable even as flat placeholder quads (real art replaces this).
Color tint_for_entity(core::NetId id) {
	std::uint32_t h = static_cast<std::uint32_t>(id) * 2654435761u;
	h ^= h >> 16;
	const float hue = static_cast<float>(h % 360u);
	return ColorFromHSV(hue, 0.55f, 0.85f);
}

} // namespace

struct EntityRenderer::Impl {
	Texture2D placeholder{};
	std::unordered_map<core::NetId, EntityPresentationState> states;
};

EntityRenderer::EntityRenderer() : impl_(std::make_unique<Impl>()) {
	Image img = GenImageColor(1, 1, WHITE);
	impl_->placeholder = LoadTextureFromImage(img);
	UnloadImage(img);
}

EntityRenderer::~EntityRenderer() {
	if (impl_->placeholder.id != 0) {
		UnloadTexture(impl_->placeholder);
	}
}

void EntityRenderer::sync(const net::ClientSession &client,
		const CameraView &camera, double dt_seconds) {
	const auto &remote = client.remote_entities();

	for (auto it = impl_->states.begin(); it != impl_->states.end();) {
		if (remote.find(it->first) == remote.end()) {
			it = impl_->states.erase(it);
		} else {
			++it;
		}
	}

	for (const auto &[id, rec] : remote) {
		auto [it, inserted] =
				impl_->states.try_emplace(id, kDefaultFacings);
		(void)inserted;
		const core::Vec3d pos = client.interpolated_pos(id);
		it->second.update(pos, static_cast<double>(rec.rot.x), rec.vel,
				rec.flags, camera.position, dt_seconds);
	}
}

void EntityRenderer::draw(const CameraView &camera_view) const {
	const Camera3D camera = to_raylib_camera(camera_view);
	for (const auto &[id, state] : impl_->states) {
		const EntityPresentationState::Frame &frame = state.frame();
		const core::Vec3d pos = state.position();
		const Vector3 feet{ static_cast<float>(pos.x),
			static_cast<float>(pos.y), static_cast<float>(pos.z) };

		const Rectangle source{ 0.0f, 0.0f,
			static_cast<float>(impl_->placeholder.width),
			static_cast<float>(impl_->placeholder.height) };
		Vector2 size{ kPlaceholderWidth, kPlaceholderHeight };
		if (frame.pose.mirrored) {
			size.x = -size.x; // DrawBillboardPro flips the source horizontally
		}
		// Bottom-centre pivot: `feet` is the ground contact point, matching
		// Position/Collider's own anchor convention.
		const Vector2 origin{ size.x * 0.5f, 0.0f };

		DrawBillboardPro(camera, impl_->placeholder, source, feet,
				Vector3{ 0.0f, 1.0f, 0.0f }, size, origin, 0.0f,
				tint_for_entity(id));
	}
}

std::size_t EntityRenderer::tracked_count() const {
	return impl_->states.size();
}

} // namespace vb::render
