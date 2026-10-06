#include "editor_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>

#include <raygui.h>

#include "vb/render/texture_atlas.hpp"
#include "vb/world/daynight.hpp"

namespace vb::editor {

namespace {

constexpr float kTopBarHeight = 34.0f;
constexpr float kSideWidth = 270.0f;
constexpr float kErrorsHeight = 150.0f;

// Loads every texture file the catalog's blocks name from the pack folder,
// plus the generated missing-block checker.
render::VirtualFs load_textures(const Workspace &ws) {
	render::VirtualFs vfs;
	const world::BlockRegistry &registry = ws.catalog().registry();
	for (std::size_t i = 0; i < registry.size(); ++i) {
		const world::BlockType &type = registry.get(static_cast<core::BlockId>(i));
		if (type.texture.empty() || type.texture == kMissingTexturePath) {
			continue;
		}
		std::ifstream f(ws.pack_root() / type.texture, std::ios::binary | std::ios::ate);
		if (!f) {
			continue; // TextureAtlas::build falls back to a flat color
		}
		const auto size = static_cast<std::size_t>(f.tellg());
		f.seekg(0);
		std::vector<std::byte> bytes(size);
		f.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
		vfs[type.texture] = std::move(bytes);
	}

	Image checker = GenImageChecked(16, 16, 4, 4, Color{ 255, 0, 255, 255 }, Color{ 40, 0, 40, 255 });
	int png_size = 0;
	unsigned char *png = ExportImageToMemory(checker, ".png", &png_size);
	if (png) {
		vfs[kMissingTexturePath] = std::vector<std::byte>(
				reinterpret_cast<std::byte *>(png), reinterpret_cast<std::byte *>(png) + png_size);
		RL_FREE(png);
	}
	UnloadImage(checker);
	return vfs;
}

Camera3D to_camera(const OrbitCamera &c) {
	Camera3D cam{};
	const core::Vec3d eye = c.eye();
	const core::Vec3d target = c.target();
	cam.position = Vector3{ static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z) };
	cam.target = Vector3{ static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.z) };
	cam.up = Vector3{ 0.0f, 1.0f, 0.0f };
	cam.fovy = 45.0f;
	cam.projection = CAMERA_PERSPECTIVE;
	return cam;
}

bool inside(const Rectangle &r, Vector2 p) { return CheckCollisionPointRec(p, r); }

} // namespace

EditorApp::EditorApp(Workspace workspace, EditorOptions options) : workspace_(std::move(workspace)), options_(std::move(options)) {
	render::WindowConfig cfg;
	cfg.width = options_.width;
	cfg.height = options_.height;
	cfg.title = "vb_structure_editor";
	cfg.vsync = options_.screenshot.empty();
	window_ = std::make_unique<render::Window>(cfg);
	if (!IsWindowReady()) {
		std::cerr << "vb_structure_editor: could not open a window (no display or OpenGL 3.3?)\n";
		std::exit(EXIT_FAILURE);
	}
	rebuild_renderer();
	if (!options_.open_name.empty()) {
		open_structure(options_.open_name);
	}
	show_errors_ = !workspace_.errors().empty();
}

EditorApp::~EditorApp() {
	// The renderer owns GPU resources and must go before the window.
	view_.reset();
	renderer_.reset();
}

void EditorApp::rebuild_renderer() {
	renderer_.reset();
	view_.reset();
	renderer_ = std::make_unique<render::ChunkRenderer>();

	const render::VirtualFs vfs = load_textures(workspace_);
	render::TextureAtlas atlas = render::TextureAtlas::build(workspace_.catalog().registry(), vfs);
	std::vector<render::AtlasRect> rects;
	std::vector<Color> averages;
	for (std::size_t i = 0; i < atlas.block_count(); ++i) {
		const auto id = static_cast<core::BlockId>(i);
		rects.push_back(atlas.rect_for(id));
		averages.push_back(atlas.average_color_for(id));
	}
	renderer_->set_atlas(atlas.upload(), std::move(rects), std::move(averages));

	view_ = std::make_unique<VolumeView>(workspace_.catalog());
	if (doc_) {
		view_->rebuild(*doc_, variant_);
	}
}

void EditorApp::open_structure(const std::string &name) {
	std::string error;
	auto doc = workspace_.open_structure(name, &error);
	if (!doc) {
		status_ = error;
		return;
	}
	doc_ = std::move(doc);
	variant_ = 0;
	refresh_view();
	frame_camera();
	status_ = "opened " + doc_->name;
}

void EditorApp::reload() {
	const bool ok = workspace_.reload();
	// The catalog may have changed (textures, new blocks): rebuild the atlas
	// and renderer. The open structure stays in memory; names that no longer
	// exist are drawn as the missing marker, never dropped.
	rebuild_renderer();
	status_ = ok ? "reloaded" : "reload failed: kept the previous blocks";
	show_errors_ = !workspace_.errors().empty();
}

void EditorApp::refresh_view() {
	if (doc_ && view_) {
		view_->rebuild(*doc_, variant_);
	}
}

void EditorApp::frame_camera() {
	if (!doc_) {
		return;
	}
	const core::IVec3 size = doc_->size();
	camera_.fit({ kVolumeOrigin.x + size.x * 0.5, kVolumeOrigin.y + size.y * 0.5, kVolumeOrigin.z + size.z * 0.5 },
			{ static_cast<double>(size.x), static_cast<double>(size.y), static_cast<double>(size.z) });
}

bool EditorApp::mouse_over_ui() const {
	const Vector2 m = GetMousePosition();
	return std::any_of(ui_rects_.begin(), ui_rects_.end(), [&](const Rectangle &r) { return inside(r, m); });
}

void EditorApp::handle_input() {
	if (IsKeyPressed(KEY_F5)) {
		reload();
	}
	if (mouse_over_ui()) {
		return;
	}
	const Vector2 delta = GetMouseDelta();
	if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
		const bool pan = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsKeyDown(KEY_LEFT_SHIFT);
		if (pan) {
			camera_.pan(static_cast<double>(delta.x), static_cast<double>(delta.y));
		} else {
			camera_.orbit(static_cast<double>(delta.x), static_cast<double>(delta.y));
		}
	}
	const float wheel = GetMouseWheelMove();
	if (wheel != 0.0f) {
		camera_.zoom(static_cast<double>(wheel));
	}
	if (IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_F)) {
		frame_camera();
	}
}

void EditorApp::draw_world() {
	const Camera3D camera = to_camera(camera_);
	renderer_->sync(view_->store(), 64, 64);
	renderer_->set_fog(camera_.eye(), world::SkyColor{ 18, 18, 22 }, 100000.0f, 200000.0f);

	BeginMode3D(camera);
	if (doc_) {
		const core::IVec3 size = doc_->size();
		const float ox = static_cast<float>(kVolumeOrigin.x);
		const float oy = static_cast<float>(kVolumeOrigin.y);
		const float oz = static_cast<float>(kVolumeOrigin.z);

		// Ground grid on the plane the anchor cell stands on (its bottom
		// face), a few blocks wider than the structure on every side.
		const float ground_y = oy + static_cast<float>(doc_->anchor.y);
		const int pad = 4;
		const Color grid{ 90, 90, 100, 255 };
		for (int x = -pad; x <= size.x + pad; ++x) {
			DrawLine3D(Vector3{ ox + static_cast<float>(x), ground_y, oz - static_cast<float>(pad) },
					Vector3{ ox + static_cast<float>(x), ground_y, oz + static_cast<float>(size.z + pad) }, grid);
		}
		for (int z = -pad; z <= size.z + pad; ++z) {
			DrawLine3D(Vector3{ ox - static_cast<float>(pad), ground_y, oz + static_cast<float>(z) },
					Vector3{ ox + static_cast<float>(size.x + pad), ground_y, oz + static_cast<float>(z) }, grid);
		}
	}

	renderer_->draw(camera);

	if (doc_) {
		const core::IVec3 size = doc_->size();
		const float ox = static_cast<float>(kVolumeOrigin.x);
		const float oy = static_cast<float>(kVolumeOrigin.y);
		const float oz = static_cast<float>(kVolumeOrigin.z);
		// Bounds.
		DrawCubeWiresV(Vector3{ ox + static_cast<float>(size.x) * 0.5f, oy + static_cast<float>(size.y) * 0.5f,
							   oz + static_cast<float>(size.z) * 0.5f },
				Vector3{ static_cast<float>(size.x), static_cast<float>(size.y), static_cast<float>(size.z) },
				Color{ 120, 200, 255, 255 });
		// Anchor cell.
		const Vector3 anchor{ ox + static_cast<float>(doc_->anchor.x) + 0.5f, oy + static_cast<float>(doc_->anchor.y) + 0.5f,
			oz + static_cast<float>(doc_->anchor.z) + 0.5f };
		DrawCubeWiresV(anchor, Vector3{ 1.04f, 1.04f, 1.04f }, Color{ 255, 210, 60, 255 });
		DrawSphere(Vector3{ anchor.x, anchor.y - 0.5f, anchor.z }, 0.12f, Color{ 255, 210, 60, 255 });
	}
	EndMode3D();
}

void EditorApp::draw_top_bar() {
	const float w = static_cast<float>(GetScreenWidth());
	const Rectangle bar{ 0, 0, w, kTopBarHeight };
	ui_rects_.push_back(bar);
	GuiPanel(bar, nullptr);
	float x = 6.0f;
	if (GuiButton(Rectangle{ x, 4, 70, 26 }, "Open")) {
		show_open_ = !show_open_;
		open_active_ = -1;
	}
	x += 76.0f;
	if (GuiButton(Rectangle{ x, 4, 110, 26 }, "Reload (F5)")) {
		reload();
	}
	x += 116.0f;
	const std::string errors_label = "Errors (" + std::to_string(workspace_.errors().size()) + ")";
	if (GuiButton(Rectangle{ x, 4, 100, 26 }, errors_label.c_str())) {
		show_errors_ = !show_errors_;
	}
	x += 112.0f;
	std::string title = doc_ ? doc_->name : std::string("no structure open");
	title += "   |   pack '" + workspace_.pack_name() + "'";
	if (!status_.empty()) {
		title += "   |   " + status_;
	}
	GuiLabel(Rectangle{ x, 4, w - x - 8, 26 }, title.c_str());
}

void EditorApp::draw_side_panel() {
	const float h = static_cast<float>(GetScreenHeight());
	const float bottom = h - (show_errors_ ? kErrorsHeight : 0.0f);
	const Rectangle panel{ static_cast<float>(GetScreenWidth()) - kSideWidth, kTopBarHeight, kSideWidth, bottom - kTopBarHeight };
	ui_rects_.push_back(panel);
	GuiPanel(panel, "Structure");
	if (!doc_) {
		GuiLabel(Rectangle{ panel.x + 10, panel.y + 34, panel.width - 20, 24 }, "Use Open to pick a structure.");
		return;
	}
	float y = panel.y + 34;
	const auto line = [&](const std::string &text) {
		GuiLabel(Rectangle{ panel.x + 10, y, panel.width - 20, 22 }, text.c_str());
		y += 22;
	};
	const core::IVec3 size = doc_->size();
	line("size   " + std::to_string(size.x) + " x " + std::to_string(size.y) + " x " + std::to_string(size.z));
	line("anchor " + std::to_string(doc_->anchor.x) + ", " + std::to_string(doc_->anchor.y) + ", " + std::to_string(doc_->anchor.z));

	y += 8;
	line("Variant " + std::to_string(variant_ + 1) + " of " + std::to_string(doc_->variants.size()));
	if (GuiButton(Rectangle{ panel.x + 10, y, 40, 26 }, "<") && variant_ > 0) {
		--variant_;
		refresh_view();
	}
	if (GuiButton(Rectangle{ panel.x + 56, y, 40, 26 }, ">") && variant_ + 1 < doc_->variants.size()) {
		++variant_;
		refresh_view();
	}
	char weight[48];
	std::snprintf(weight, sizeof weight, "weight %.3g", doc_->variants[variant_].weight);
	GuiLabel(Rectangle{ panel.x + 106, y, panel.width - 116, 26 }, weight);
	y += 40;

	// Palette summary: how many cells use each block, with unknown ones flagged.
	line("Blocks used");
	std::size_t missing = 0;
	for (std::size_t i = 0; i < doc_->names.size(); ++i) {
		const Cell cell = static_cast<Cell>(i + 1);
		if (!doc_->uses(cell)) {
			continue;
		}
		const std::string &name = doc_->names.name(cell);
		const bool known = workspace_.catalog().known(name);
		missing += known ? 0 : 1;
		std::size_t count = 0;
		for (const Cell c : doc_->variants[variant_].volume.cells()) {
			count += c == cell ? 1 : 0;
		}
		if (y < panel.y + panel.height - 28) {
			line((known ? "  " : "! ") + name + "  x" + std::to_string(count));
		}
	}
	if (missing > 0) {
		y = panel.y + panel.height - 24;
		GuiLabel(Rectangle{ panel.x + 10, y, panel.width - 20, 22 }, "! = not in the block data script");
	}
}

void EditorApp::draw_open_dialog() {
	if (!show_open_) {
		return;
	}
	const float w = 420.0f;
	const float h = 360.0f;
	const Rectangle box{ (static_cast<float>(GetScreenWidth()) - w) * 0.5f, 90.0f, w, h };
	ui_rects_.push_back(box);
	if (GuiWindowBox(box, "Open structure")) {
		show_open_ = false;
		return;
	}
	std::string items;
	for (const StructureEntry &entry : workspace_.structures()) {
		if (!items.empty()) {
			items += ';';
		}
		items += entry.ok ? entry.name : (entry.name + "  (error)");
	}
	if (workspace_.structures().empty()) {
		GuiLabel(Rectangle{ box.x + 12, box.y + 40, w - 24, 24 }, "No files in structures/ yet.");
		return;
	}
	GuiListView(Rectangle{ box.x + 12, box.y + 36, w - 24, h - 90 }, items.c_str(), &open_scroll_, &open_active_);
	const bool can_open = open_active_ >= 0 && static_cast<std::size_t>(open_active_) < workspace_.structures().size() &&
			workspace_.structures()[static_cast<std::size_t>(open_active_)].ok;
	if (!can_open) {
		GuiDisable();
	}
	if (GuiButton(Rectangle{ box.x + w - 112, box.y + h - 46, 100, 32 }, "Open") && can_open) {
		open_structure(workspace_.structures()[static_cast<std::size_t>(open_active_)].name);
		show_open_ = false;
	}
	GuiEnable();
}

void EditorApp::draw_errors_panel() {
	if (!show_errors_) {
		return;
	}
	const Rectangle panel{ 0, static_cast<float>(GetScreenHeight()) - kErrorsHeight, static_cast<float>(GetScreenWidth()), kErrorsHeight };
	ui_rects_.push_back(panel);
	GuiPanel(panel, "Errors");
	if (workspace_.errors().empty()) {
		GuiLabel(Rectangle{ panel.x + 10, panel.y + 34, panel.width - 20, 24 }, "No errors.");
		return;
	}
	std::string items;
	for (const EditorError &e : workspace_.errors()) {
		if (!items.empty()) {
			items += ';';
		}
		// ';' separates list items, so keep messages on one line without it.
		std::string text = e.message;
		std::replace(text.begin(), text.end(), ';', ',');
		std::replace(text.begin(), text.end(), '\n', ' ');
		items += text;
	}
	GuiListView(Rectangle{ panel.x + 8, panel.y + 28, panel.width - 16, panel.height - 36 }, items.c_str(), &errors_scroll_,
			&errors_active_);
}

void EditorApp::draw_ui() {
	ui_rects_.clear();
	draw_top_bar();
	draw_side_panel();
	draw_errors_panel();
	draw_open_dialog();
}

int EditorApp::run() {
	int frame = 0;
	while (!window_->should_close()) {
		handle_input();
		window_->begin_frame();
		draw_world();
		draw_ui();
		window_->end_frame();
		++frame;
		if (!options_.screenshot.empty() && frame >= std::max(options_.frames, 8)) {
			TakeScreenshot(options_.screenshot.string().c_str());
			break;
		}
	}
	return EXIT_SUCCESS;
}

} // namespace vb::editor
