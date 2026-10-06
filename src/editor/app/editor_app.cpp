#include "editor_app.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include <raygui.h>

#include "vb/core/noise.hpp"
#include "vb/render/texture_atlas.hpp"
#include "vb/world/daynight.hpp"

namespace vb::editor {

namespace {

constexpr float kTopBarHeight = 34.0f;
constexpr float kToolBarWidth = 118.0f;
constexpr float kSideWidth = 280.0f;
constexpr float kErrorsHeight = 150.0f;
constexpr float kRowHeight = 26.0f;

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

Vector3 cell_center(core::IVec3 c) {
	return Vector3{ static_cast<float>(kVolumeOrigin.x + c.x) + 0.5f, static_cast<float>(kVolumeOrigin.y + c.y) + 0.5f,
		static_cast<float>(kVolumeOrigin.z + c.z) + 0.5f };
}

std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// GuiTextBox needs a fixed-capacity buffer.
bool text_box(Rectangle bounds, std::string &s, bool &editing) {
	constexpr int kCap = 96;
	s.resize(kCap, '\0');
	const bool toggled = GuiTextBox(bounds, s.data(), kCap, editing) != 0;
	if (toggled) {
		editing = !editing;
	}
	s.resize(std::strlen(s.c_str()));
	return toggled;
}

const char *tool_name(Tool t) {
	switch (t) {
		case Tool::kPlace:
			return "Place";
		case Tool::kRemove:
			return "Remove";
		case Tool::kPaint:
			return "Paint";
		case Tool::kPick:
			return "Pick block";
		case Tool::kBox:
			return "Box fill";
		case Tool::kLine:
			return "Line";
		case Tool::kFlood:
			return "Flood";
		case Tool::kSelect:
			return "Select";
		case Tool::kAnchor:
			return "Anchor";
	}
	return "";
}

bool parse_ivec(const std::string &s, core::IVec3 &out) {
	return std::sscanf(s.c_str(), "%d,%d,%d", &out.x, &out.y, &out.z) == 3;
}

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
	// Escape cancels tools and dialogs; closing the window is the window's own
	// close button.
	SetExitKey(KEY_NULL);
	rebuild_renderer();
	rebuild_palette();
	if (!options_.open_name.empty()) {
		open_structure(options_.open_name);
	}
	show_errors_ = !workspace_.errors().empty();
}

EditorApp::~EditorApp() {
	// The renderer owns GPU resources and must go before the window.
	view_.reset();
	preview_.reset();
	preview_renderer_.reset();
	renderer_.reset();
}

// ---------------------------------------------------------------------------
// Loading and documents

// A renderer with the pack's atlas. The renderer takes ownership of the
// uploaded texture, so every renderer needs its own upload.
static std::unique_ptr<render::ChunkRenderer> make_renderer(const Workspace &workspace) {
	auto renderer = std::make_unique<render::ChunkRenderer>();
	const render::VirtualFs vfs = load_textures(workspace);
	render::TextureAtlas atlas = render::TextureAtlas::build(workspace.catalog().registry(), vfs);
	std::vector<render::AtlasRect> rects;
	std::vector<Color> averages;
	for (std::size_t i = 0; i < atlas.block_count(); ++i) {
		const auto id = static_cast<core::BlockId>(i);
		rects.push_back(atlas.rect_for(id));
		averages.push_back(atlas.average_color_for(id));
	}
	renderer->set_atlas(atlas.upload(), std::move(rects), std::move(averages));
	return renderer;
}

void EditorApp::rebuild_renderer() {
	renderer_.reset();
	view_.reset();
	preview_renderer_.reset();
	preview_.reset();
	preview_pending_ = true;
	renderer_ = make_renderer(workspace_);

	view_ = std::make_unique<VolumeView>(workspace_.catalog());
	seen_revision_ = 0; // force a rebuild of the view on the next frame
}

void EditorApp::rebuild_palette() {
	palette_.clear();
	palette_.push_back({ "", true, core::BlockId::kAir });
	palette_.push_back({ "base:air", false, core::BlockId::kAir });
	for (const std::string &name : workspace_.catalog().block_names()) {
		palette_.push_back({ name, false, workspace_.catalog().registry().find(name) });
	}
}

void EditorApp::open_structure(const std::string &name) {
	std::string error;
	auto doc = workspace_.open_structure(name, &error);
	if (!doc) {
		set_status(error);
		return;
	}
	session_ = std::make_unique<EditSession>(std::move(*doc));
	session_->set_brush("base:stone");
	slice_ = VolumeView::kNoSlice;
	cancel_drag();
	seen_revision_ = 0;
	frame_camera();
	set_status("opened " + session_->doc().name);
}

void EditorApp::new_structure(const std::string &name, core::IVec3 size, core::IVec3 anchor) {
	session_ = std::make_unique<EditSession>(StructureDoc::create(name, size, anchor));
	session_->set_brush("base:stone");
	slice_ = VolumeView::kNoSlice;
	cancel_drag();
	seen_revision_ = 0;
	frame_camera();
	set_status("new structure " + name);
}

void EditorApp::reload() {
	const bool ok = workspace_.reload();
	// The catalog may have changed (textures, new blocks): rebuild the atlas,
	// renderer and palette. The open structure stays in memory; names that no
	// longer exist are drawn as the missing marker, never dropped.
	rebuild_renderer();
	rebuild_palette();
	set_status(ok ? "reloaded" : "reload failed: kept the previous blocks");
	show_errors_ = !workspace_.errors().empty();
}

void EditorApp::refresh_view() {
	if (!session_ || !view_) {
		return;
	}
	const StructureDoc &doc = session_->doc();
	view_->rebuild(doc, session_->variant(), slice_);
	seen_revision_ = session_->revision();

	ghosts_.clear();
	const Volume &vol = session_->volume();
	const core::IVec3 size = vol.size();
	for (int y = 0; y < size.y && y <= slice_; ++y) {
		for (int z = 0; z < size.z; ++z) {
			for (int x = 0; x < size.x; ++x) {
				const Cell c = vol.get(x, y, z);
				if (c != kKeepCell && workspace_.catalog().render_id(doc.names.name(c)) == core::BlockId::kAir) {
					ghosts_.push_back({ x, y, z });
				}
			}
		}
	}
}

void EditorApp::frame_camera() {
	if (!session_) {
		return;
	}
	const core::IVec3 size = session_->doc().size();
	camera_.fit({ kVolumeOrigin.x + size.x * 0.5, kVolumeOrigin.y + size.y * 0.5, kVolumeOrigin.z + size.z * 0.5 },
			{ static_cast<double>(size.x), static_cast<double>(size.y), static_cast<double>(size.z) });
}

bool EditorApp::save() {
	if (!session_) {
		return false;
	}
	std::string error;
	if (!workspace_.save(session_->doc(), &error)) {
		set_status("save failed: " + error);
		return false;
	}
	session_->mark_saved();
	set_status("saved " + workspace_.path_for(session_->doc().name).generic_string());
	return true;
}

bool EditorApp::save_as(const std::string &name) {
	if (!session_ || name.empty()) {
		return false;
	}
	const std::string old = session_->doc().name;
	session_->doc().name = name;
	if (!save()) {
		session_->doc().name = old;
		return false;
	}
	return true;
}

void EditorApp::guard_unsaved(std::function<void()> action) {
	if (session_ && session_->dirty()) {
		after_unsaved_ = std::move(action);
		dialog_ = Dialog::kUnsaved;
	} else {
		action();
	}
}

// ---------------------------------------------------------------------------
// Input

bool EditorApp::mouse_over_ui() const {
	const Vector2 m = GetMousePosition();
	return std::any_of(ui_rects_.begin(), ui_rects_.end(), [&](const Rectangle &r) { return inside(r, m); });
}

void EditorApp::cancel_drag() { drag_start_.reset(); }

PickResult EditorApp::current_pick() const {
	if (!session_) {
		return {};
	}
	const Ray ray = GetScreenToWorldRay(GetMousePosition(), to_camera(camera_));
	PickOptions opt;
	opt.max_y = slice_;
	opt.ground_y = session_->doc().anchor.y;
	return pick_cell(session_->volume(),
			{ static_cast<double>(ray.position.x) - kVolumeOrigin.x, static_cast<double>(ray.position.y) - kVolumeOrigin.y,
					static_cast<double>(ray.position.z) - kVolumeOrigin.z },
			{ static_cast<double>(ray.direction.x), static_cast<double>(ray.direction.y), static_cast<double>(ray.direction.z) }, opt);
}

// The cell a tool acts on: the existing cell when the ray hit one, the ground
// cell otherwise.
core::IVec3 EditorApp::target_cell(const PickResult &pick) const { return pick.cell; }

void EditorApp::handle_shortcuts() {
	if (search_edit_ || dialog_name_edit_ || dialog_ != Dialog::kNone) {
		return;
	}
	const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
	const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
	if (IsKeyPressed(KEY_F5)) {
		reload();
	}
	if (IsKeyPressed(KEY_ESCAPE)) {
		cancel_drag();
		show_open_ = false;
	}
	if (!session_) {
		return;
	}
	if (ctrl && IsKeyPressed(KEY_Z) && !shift) {
		session_->undo();
	} else if (ctrl && (IsKeyPressed(KEY_Y) || (shift && IsKeyPressed(KEY_Z)))) {
		session_->redo();
	} else if (ctrl && IsKeyPressed(KEY_S)) {
		save();
	} else if (ctrl && IsKeyPressed(KEY_C)) {
		session_->copy_selection();
	} else if (ctrl && IsKeyPressed(KEY_X)) {
		session_->cut_selection();
	} else if (ctrl && IsKeyPressed(KEY_V)) {
		const PickResult pick = current_pick();
		if (pick.hit) {
			session_->paste(pick.on_cell ? pick.place : pick.cell);
		}
	} else if (IsKeyPressed(KEY_DELETE) && session_->selection()) {
		const Box sel = *session_->selection();
		const auto previous = session_->brush();
		session_->set_brush(std::nullopt);
		session_->box_fill(sel.min, sel.max);
		session_->set_brush(previous);
	}

	// Arrow keys nudge the selection (x/z) and Page Up/Down move it in y.
	if (session_->selection()) {
		core::IVec3 d{ 0, 0, 0 };
		d.x += IsKeyPressed(KEY_RIGHT) - IsKeyPressed(KEY_LEFT);
		d.z += IsKeyPressed(KEY_DOWN) - IsKeyPressed(KEY_UP);
		d.y += IsKeyPressed(KEY_PAGE_UP) - IsKeyPressed(KEY_PAGE_DOWN);
		if (d.x != 0 || d.y != 0 || d.z != 0) {
			session_->move_selection(d);
		}
	}

	// Tool hotkeys.
	if (!ctrl) {
		struct Hot {
			int key;
			Tool tool;
		};
		static constexpr Hot kHot[] = { { KEY_ONE, Tool::kPlace }, { KEY_TWO, Tool::kRemove }, { KEY_THREE, Tool::kPaint },
			{ KEY_FOUR, Tool::kPick }, { KEY_FIVE, Tool::kBox }, { KEY_SIX, Tool::kLine }, { KEY_SEVEN, Tool::kFlood },
			{ KEY_EIGHT, Tool::kSelect }, { KEY_NINE, Tool::kAnchor } };
		for (const Hot &h : kHot) {
			if (IsKeyPressed(h.key)) {
				tool_ = h.tool;
				cancel_drag();
			}
		}
		if (IsKeyPressed(KEY_X)) {
			session_->symmetry.mirror_x = !session_->symmetry.mirror_x;
		}
		if (IsKeyPressed(KEY_Z)) {
			session_->symmetry.mirror_z = !session_->symmetry.mirror_z;
		}
	}
}

void EditorApp::apply_tool(const PickResult &pick, bool shift, bool ctrl, bool alt) {
	if (!session_ || !pick.hit) {
		return;
	}
	EditSession &s = *session_;
	const core::IVec3 here = target_cell(pick);
	const core::IVec3 empty = pick.on_cell ? pick.place : pick.cell;

	// Modifier shortcuts work with every tool: shift removes, ctrl picks the
	// block under the cursor, alt paints.
	if (ctrl && pick.on_cell) {
		s.set_brush(s.eyedrop(here));
		set_status("brush: " + (s.brush() ? *s.brush() : std::string("keep")));
		return;
	}
	if (shift && pick.on_cell) {
		s.remove(here);
		return;
	}
	if (alt && pick.on_cell) {
		s.paint(here);
		return;
	}

	switch (tool_) {
		case Tool::kPlace:
			s.place(empty);
			break;
		case Tool::kRemove:
			if (pick.on_cell) {
				s.remove(here);
			}
			break;
		case Tool::kPaint:
			if (pick.on_cell) {
				s.paint(here);
			}
			break;
		case Tool::kPick:
			if (pick.on_cell) {
				s.set_brush(s.eyedrop(here));
				set_status("brush: " + (s.brush() ? *s.brush() : std::string("keep")));
			}
			break;
		case Tool::kBox:
		case Tool::kLine: {
			if (!drag_start_) {
				drag_start_ = empty;
			} else {
				if (tool_ == Tool::kBox) {
					s.box_fill(*drag_start_, empty);
				} else {
					s.line(*drag_start_, empty);
				}
				drag_start_.reset();
			}
			break;
		}
		case Tool::kFlood:
			s.flood_replace(pick.on_cell ? here : empty);
			break;
		case Tool::kSelect:
			if (!drag_start_) {
				drag_start_ = pick.on_cell ? here : empty;
			} else {
				s.select(box_between(*drag_start_, pick.on_cell ? here : empty));
				drag_start_.reset();
			}
			break;
		case Tool::kAnchor:
			s.set_anchor(empty);
			break;
	}
}

void EditorApp::handle_viewport_click() {
	if (!session_ || dialog_ != Dialog::kNone || mouse_over_ui() || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
		return;
	}
	const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
	const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
	const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
	apply_tool(current_pick(), shift, ctrl, alt);
}

void EditorApp::handle_input() {
	if (preview_mode()) {
		handle_preview_input();
		return;
	}
	handle_shortcuts();
	if (mouse_over_ui() || dialog_ != Dialog::kNone) {
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
	handle_viewport_click();
}

// ---------------------------------------------------------------------------
// World drawing

void EditorApp::draw_world() {
	if (preview_mode()) {
		draw_preview_world();
		return;
	}
	if (session_ && (session_->revision() != seen_revision_)) {
		refresh_view();
	}
	const Camera3D camera = to_camera(camera_);
	renderer_->sync(view_->store(), 64, 64);
	renderer_->set_fog(camera_.eye(), world::SkyColor{ 18, 18, 22 }, 100000.0f, 200000.0f);

	BeginMode3D(camera);
	if (session_) {
		const core::IVec3 size = session_->doc().size();
		const float ox = static_cast<float>(kVolumeOrigin.x);
		const float oy = static_cast<float>(kVolumeOrigin.y);
		const float oz = static_cast<float>(kVolumeOrigin.z);

		// Ground grid on the plane the anchor cell stands on (its bottom
		// face), a few blocks wider than the structure on every side.
		const float ground_y = oy + static_cast<float>(session_->doc().anchor.y);
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
	draw_overlays();
	EndMode3D();
}

void EditorApp::draw_overlays() {
	if (!session_) {
		return;
	}
	const StructureDoc &doc = session_->doc();
	const core::IVec3 size = doc.size();
	const float ox = static_cast<float>(kVolumeOrigin.x);
	const float oy = static_cast<float>(kVolumeOrigin.y);
	const float oz = static_cast<float>(kVolumeOrigin.z);

	// Bounds (cut off at the layer slice) and the symmetry planes.
	const int top = std::min(size.y - 1, slice_);
	DrawCubeWiresV(Vector3{ ox + static_cast<float>(size.x) * 0.5f, oy + static_cast<float>(top + 1) * 0.5f,
						   oz + static_cast<float>(size.z) * 0.5f },
			Vector3{ static_cast<float>(size.x), static_cast<float>(top + 1), static_cast<float>(size.z) }, Color{ 120, 200, 255, 255 });
	const Color plane{ 255, 120, 200, 140 };
	if (session_->symmetry.mirror_x) {
		const float x = ox + static_cast<float>(size.x) * 0.5f;
		DrawLine3D(Vector3{ x, oy, oz }, Vector3{ x, oy, oz + static_cast<float>(size.z) }, plane);
		DrawLine3D(Vector3{ x, oy + static_cast<float>(top + 1), oz }, Vector3{ x, oy + static_cast<float>(top + 1), oz + static_cast<float>(size.z) }, plane);
	}
	if (session_->symmetry.mirror_z) {
		const float z = oz + static_cast<float>(size.z) * 0.5f;
		DrawLine3D(Vector3{ ox, oy, z }, Vector3{ ox + static_cast<float>(size.x), oy, z }, plane);
		DrawLine3D(Vector3{ ox, oy + static_cast<float>(top + 1), z }, Vector3{ ox + static_cast<float>(size.x), oy + static_cast<float>(top + 1), z }, plane);
	}

	// Anchor cell.
	const Vector3 anchor = cell_center(doc.anchor);
	DrawCubeWiresV(anchor, Vector3{ 1.04f, 1.04f, 1.04f }, Color{ 255, 210, 60, 255 });
	DrawSphere(Vector3{ anchor.x, anchor.y - 0.5f, anchor.z }, 0.12f, Color{ 255, 210, 60, 255 });

	// Explicit-air cells carve terrain: show them as faint ghost cubes.
	for (const core::IVec3 g : ghosts_) {
		DrawCubeWiresV(cell_center(g), Vector3{ 0.96f, 0.96f, 0.96f }, Color{ 150, 220, 255, 90 });
	}

	// Selection and the pending first corner of a box, line or selection.
	if (const auto &sel = session_->selection()) {
		const core::IVec3 sz = sel->size();
		DrawCubeWiresV(Vector3{ ox + static_cast<float>(sel->min.x) + static_cast<float>(sz.x) * 0.5f,
							   oy + static_cast<float>(sel->min.y) + static_cast<float>(sz.y) * 0.5f,
							   oz + static_cast<float>(sel->min.z) + static_cast<float>(sz.z) * 0.5f },
				Vector3{ static_cast<float>(sz.x) + 0.04f, static_cast<float>(sz.y) + 0.04f, static_cast<float>(sz.z) + 0.04f },
				Color{ 255, 255, 120, 255 });
	}

	if (dialog_ != Dialog::kNone || mouse_over_ui()) {
		return;
	}
	const PickResult pick = current_pick();
	if (!pick.hit) {
		return;
	}
	const core::IVec3 empty = pick.on_cell ? pick.place : pick.cell;
	const bool acts_on_empty = tool_ == Tool::kPlace || tool_ == Tool::kBox || tool_ == Tool::kLine || tool_ == Tool::kAnchor;
	const core::IVec3 hover = acts_on_empty ? empty : pick.cell;
	const Color color = tool_ == Tool::kRemove ? Color{ 255, 90, 90, 255 } : Color{ 120, 255, 140, 255 };
	DrawCubeWiresV(cell_center(hover), Vector3{ 1.02f, 1.02f, 1.02f }, color);
	if (pick.on_cell) {
		// The face the cursor is on.
		const Vector3 c = cell_center(pick.cell);
		const Vector3 f{ c.x + static_cast<float>(pick.normal.x) * 0.5f, c.y + static_cast<float>(pick.normal.y) * 0.5f,
			c.z + static_cast<float>(pick.normal.z) * 0.5f };
		DrawSphere(f, 0.08f, color);
	}
	if (drag_start_ && (tool_ == Tool::kBox || tool_ == Tool::kLine || tool_ == Tool::kSelect)) {
		const core::IVec3 a = *drag_start_;
		const core::IVec3 b = tool_ == Tool::kSelect && pick.on_cell ? pick.cell : empty;
		if (tool_ == Tool::kLine) {
			for (const core::IVec3 p : line_positions(a, b)) {
				DrawCubeWiresV(cell_center(p), Vector3{ 1.0f, 1.0f, 1.0f }, Color{ 255, 220, 120, 255 });
			}
		} else {
			const Box box = box_between(a, b);
			const core::IVec3 sz = box.size();
			DrawCubeWiresV(Vector3{ ox + static_cast<float>(box.min.x) + static_cast<float>(sz.x) * 0.5f,
								   oy + static_cast<float>(box.min.y) + static_cast<float>(sz.y) * 0.5f,
								   oz + static_cast<float>(box.min.z) + static_cast<float>(sz.z) * 0.5f },
					Vector3{ static_cast<float>(sz.x), static_cast<float>(sz.y), static_cast<float>(sz.z) }, Color{ 255, 220, 120, 255 });
		}
	}
}

// ---------------------------------------------------------------------------
// UI

void EditorApp::draw_block_icon(Rectangle where, const PaletteEntry &entry) const {
	if (entry.keep) {
		DrawRectangleRec(where, Color{ 60, 60, 66, 255 });
		DrawRectangleLinesEx(where, 1.0f, Color{ 150, 150, 160, 255 });
		DrawLine(static_cast<int>(where.x), static_cast<int>(where.y + where.height), static_cast<int>(where.x + where.width),
				static_cast<int>(where.y), Color{ 200, 80, 80, 255 });
		return;
	}
	if (entry.id == core::BlockId::kAir) {
		DrawRectangleRec(where, Color{ 30, 40, 55, 255 });
		DrawRectangleLinesEx(where, 1.0f, Color{ 150, 220, 255, 255 });
		return;
	}
	if (renderer_ && renderer_->has_atlas()) {
		const Texture2D tex = renderer_->atlas_texture();
		const render::AtlasRect &r = renderer_->atlas_rect_for(entry.id);
		const Rectangle src{ r.u0 * static_cast<float>(tex.width), r.v0 * static_cast<float>(tex.height),
			(r.u1 - r.u0) * static_cast<float>(tex.width), (r.v1 - r.v0) * static_cast<float>(tex.height) };
		DrawTexturePro(tex, src, where, Vector2{ 0, 0 }, 0.0f, WHITE);
	}
}

void EditorApp::draw_top_bar() {
	const float w = static_cast<float>(GetScreenWidth());
	const Rectangle bar{ 0, 0, w, kTopBarHeight };
	register_ui(bar);
	GuiPanel(bar, nullptr);
	float x = 6.0f;
	const auto button = [&](const char *label, float width) {
		const bool clicked = GuiButton(Rectangle{ x, 4, width, 26 }, label) != 0;
		x += width + 6.0f;
		return clicked;
	};
	if (button("New", 50)) {
		dialog_ = Dialog::kNew;
		dialog_name_ = workspace_.name_prefix();
		dialog_name_edit_ = false;
	}
	if (button("Open", 56)) {
		show_open_ = !show_open_;
		open_active_ = -1;
	}
	if (!session_) {
		GuiDisable();
	}
	if (button("Save", 56)) {
		save();
	}
	if (button("Save as", 66)) {
		dialog_ = Dialog::kSaveAs;
		dialog_name_ = session_ ? session_->doc().name : std::string();
		dialog_name_edit_ = false;
	}
	if (!session_ || !session_->can_undo()) {
		GuiDisable();
	}
	if (button("Undo", 56)) {
		session_->undo();
	}
	if (!session_) {
		GuiDisable();
	} else {
		GuiEnable();
	}
	if (!session_ || !session_->can_redo()) {
		GuiDisable();
	}
	if (button("Redo", 56)) {
		session_->redo();
	}
	GuiEnable();
	if (button("Reload", 66)) {
		reload();
	}
	const std::string errors_label = "Errors (" + std::to_string(workspace_.errors().size()) + ")";
	if (button(errors_label.c_str(), 92)) {
		show_errors_ = !show_errors_;
	}
	std::string title = session_ ? session_->doc().name + (session_->dirty() ? " *" : "") : std::string("no structure open");
	title += "   |   pack '" + workspace_.pack_name() + "'";
	if (!status_.empty()) {
		title += "   |   " + status_;
	}
	GuiLabel(Rectangle{ x + 4, 4, w - x - 8, 26 }, title.c_str());
}

void EditorApp::draw_tool_bar() {
	const float h = static_cast<float>(GetScreenHeight());
	const float bottom = h - (show_errors_ ? kErrorsHeight : 0.0f);
	const Rectangle panel{ 0, kTopBarHeight, kToolBarWidth, bottom - kTopBarHeight };
	register_ui(panel);
	GuiPanel(panel, "Tools");
	float y = panel.y + 28;
	static constexpr Tool kTools[] = { Tool::kPlace, Tool::kRemove, Tool::kPaint, Tool::kPick, Tool::kBox, Tool::kLine, Tool::kFlood,
		Tool::kSelect, Tool::kAnchor };
	int hotkey = 1;
	for (const Tool t : kTools) {
		bool active = tool_ == t;
		const std::string label = std::to_string(hotkey++) + " " + tool_name(t);
		if (GuiToggle(Rectangle{ panel.x + 8, y, panel.width - 16, 26 }, label.c_str(), &active) && active) {
			tool_ = t;
			cancel_drag();
		}
		y += 30;
	}
	if (!session_) {
		return;
	}
	y += 6;
	GuiLabel(Rectangle{ panel.x + 8, y, panel.width - 16, 20 }, "Mirror");
	y += 22;
	bool mx = session_->symmetry.mirror_x;
	bool mz = session_->symmetry.mirror_z;
	GuiCheckBox(Rectangle{ panel.x + 10, y, 18, 18 }, "X", &mx);
	GuiCheckBox(Rectangle{ panel.x + 62, y, 18, 18 }, "Z", &mz);
	session_->symmetry.mirror_x = mx;
	session_->symmetry.mirror_z = mz;
	y += 32;

	// Layer slice: hide everything above a layer.
	const int max_layer = session_->doc().size().y - 1;
	GuiLabel(Rectangle{ panel.x + 8, y, panel.width - 16, 20 }, "Layer slice");
	y += 22;
	float layer = static_cast<float>(std::min(slice_, max_layer));
	GuiSlider(Rectangle{ panel.x + 8, y, panel.width - 16, 18 }, nullptr, nullptr, &layer, 0.0f, static_cast<float>(max_layer));
	const int chosen = static_cast<int>(std::lround(layer));
	const int next = chosen >= max_layer ? VolumeView::kNoSlice : chosen;
	if (next != slice_) {
		slice_ = next;
		seen_revision_ = 0;
	}
	y += 22;
	const std::string slice_text = slice_ >= max_layer ? "all layers" : "y <= " + std::to_string(slice_);
	GuiLabel(Rectangle{ panel.x + 8, y, panel.width - 16, 20 }, slice_text.c_str());
	y += 28;

	// Selection actions.
	GuiLabel(Rectangle{ panel.x + 8, y, panel.width - 16, 20 }, "Selection");
	y += 22;
	const bool has_sel = session_->selection().has_value();
	if (!has_sel) {
		GuiDisable();
	}
	if (GuiButton(Rectangle{ panel.x + 8, y, 48, 24 }, "Copy")) {
		session_->copy_selection();
	}
	if (GuiButton(Rectangle{ panel.x + 60, y, 48, 24 }, "Cut")) {
		session_->cut_selection();
	}
	GuiEnable();
	y += 28;
	if (session_->clipboard().empty()) {
		GuiDisable();
	}
	if (GuiButton(Rectangle{ panel.x + 8, y, panel.width - 16, 24 }, "Paste (Ctrl+V)")) {
		const PickResult pick = current_pick();
		session_->paste(pick.hit ? (pick.on_cell ? pick.place : pick.cell) : core::IVec3{ 0, 0, 0 });
	}
	GuiEnable();
}

void EditorApp::draw_palette(Rectangle area) {
	GuiLabel(Rectangle{ area.x, area.y, 60, 22 }, "Search");
	text_box(Rectangle{ area.x + 56, area.y, area.width - 56, 22 }, search_, search_edit_);
	const Rectangle list{ area.x, area.y + 28, area.width, area.height - 28 };

	const std::string q = lower(search_);
	std::vector<const PaletteEntry *> shown;
	for (const PaletteEntry &e : palette_) {
		const std::string label = e.keep ? "keep" : e.name;
		if (q.empty() || lower(label).find(q) != std::string::npos) {
			shown.push_back(&e);
		}
	}
	const float content = static_cast<float>(shown.size()) * kRowHeight;
	const float max_scroll = std::max(0.0f, content - list.height);
	if (inside(list, GetMousePosition())) {
		palette_scroll_ -= GetMouseWheelMove() * kRowHeight * 2.0f;
	}
	palette_scroll_ = std::clamp(palette_scroll_, 0.0f, max_scroll);

	BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width), static_cast<int>(list.height));
	float y = list.y - palette_scroll_;
	for (const PaletteEntry *e : shown) {
		const Rectangle row{ list.x, y, list.width, kRowHeight - 2.0f };
		y += kRowHeight;
		if (row.y + row.height < list.y || row.y > list.y + list.height) {
			continue;
		}
		const bool selected = session_ && ((e->keep && !session_->brush()) || (!e->keep && session_->brush() && *session_->brush() == e->name));
		DrawRectangleRec(row, selected ? Color{ 70, 110, 170, 255 } : Color{ 232, 232, 236, 255 });
		draw_block_icon(Rectangle{ row.x + 3, row.y + 3, row.height - 6, row.height - 6 }, *e);
		const std::string label = e->keep ? "Keep (erase)" : e->name;
		DrawText(label.c_str(), static_cast<int>(row.x + row.height + 6), static_cast<int>(row.y + 7), 10,
				selected ? WHITE : Color{ 30, 30, 36, 255 });
		if (session_ && inside(row, GetMousePosition()) && inside(list, GetMousePosition()) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
			session_->set_brush(e->keep ? std::nullopt : std::optional<std::string>(e->name));
		}
	}
	EndScissorMode();
}

void EditorApp::draw_side_panel() {
	const float h = static_cast<float>(GetScreenHeight());
	const float bottom = h - (show_errors_ ? kErrorsHeight : 0.0f);
	const Rectangle panel{ static_cast<float>(GetScreenWidth()) - kSideWidth, kTopBarHeight, kSideWidth, bottom - kTopBarHeight };
	register_ui(panel);
	GuiPanel(panel, "Structure");
	if (!session_) {
		GuiLabel(Rectangle{ panel.x + 10, panel.y + 34, panel.width - 20, 24 }, "New or Open to start.");
		return;
	}
	float y = panel.y + 30;
	const auto line = [&](const std::string &text) {
		GuiLabel(Rectangle{ panel.x + 10, y, panel.width - 20, 20 }, text.c_str());
		y += 20;
	};
	const StructureDoc &doc = session_->doc();
	const core::IVec3 size = doc.size();
	line("size   " + std::to_string(size.x) + " x " + std::to_string(size.y) + " x " + std::to_string(size.z) +
			"     anchor " + std::to_string(doc.anchor.x) + "," + std::to_string(doc.anchor.y) + "," + std::to_string(doc.anchor.z));
	if (GuiButton(Rectangle{ panel.x + 10, y, 100, 24 }, "Resize...")) {
		dialog_ = Dialog::kResize;
		dialog_size_[0] = size.x;
		dialog_size_[1] = size.y;
		dialog_size_[2] = size.z;
		dialog_side_[0] = dialog_side_[1] = dialog_side_[2] = 0;
		std::fill(std::begin(dialog_value_edit_), std::end(dialog_value_edit_), false);
	}
	y += 32;

	line("Variant " + std::to_string(session_->variant() + 1) + " of " + std::to_string(doc.variants.size()));
	if (GuiButton(Rectangle{ panel.x + 10, y, 40, 24 }, "<") && session_->variant() > 0) {
		session_->set_variant(session_->variant() - 1);
	}
	if (GuiButton(Rectangle{ panel.x + 56, y, 40, 24 }, ">") && session_->variant() + 1 < doc.variants.size()) {
		session_->set_variant(session_->variant() + 1);
	}
	char weight[48];
	std::snprintf(weight, sizeof weight, "weight %.3g", doc.variants[session_->variant()].weight);
	GuiLabel(Rectangle{ panel.x + 106, y, panel.width - 116, 24 }, weight);
	y += 34;

	// Tabs, two rows.
	static constexpr const char *kTabLabels[] = { "Blocks", "Generate", "Variants", "Placement", "Preview" };
	for (int i = 0; i < 5; ++i) {
		const int per_row = i < 3 ? 3 : 2;
		const int col = i < 3 ? i : i - 3;
		const float tab_w = (panel.width - 16) / static_cast<float>(per_row);
		bool active = static_cast<int>(side_tab_) == i;
		if (GuiToggle(Rectangle{ panel.x + 8 + static_cast<float>(col) * tab_w, y + (i < 3 ? 0.0f : 28.0f), tab_w - 2, 24 }, kTabLabels[i],
					&active) &&
				active) {
			side_tab_ = static_cast<SideTab>(i);
			if (side_tab_ == SideTab::kPreview) {
				load_other_structures();
				mark_preview_dirty();
				preview_changed_at_ = GetTime() - 1.0; // no debounce when entering
			}
		}
	}
	y += 60;
	const Rectangle area{ panel.x + 8, y, panel.width - 16, panel.y + panel.height - y - 8 };
	switch (side_tab_) {
		case SideTab::kBlocks: {
			const std::string brush = session_->brush() ? *session_->brush() : std::string("keep (erase)");
			GuiLabel(Rectangle{ area.x, area.y, area.width, 20 }, ("Brush: " + brush).c_str());
			draw_palette(Rectangle{ area.x, area.y + 22, area.width, area.height - 22 });
			break;
		}
		case SideTab::kGenerate:
			draw_generate_tab(area);
			break;
		case SideTab::kVariants:
			draw_variants_tab(area);
			break;
		case SideTab::kPlacement:
			draw_placement_tab(area);
			break;
		case SideTab::kPreview:
			draw_preview_tab(area);
			break;
	}
}

void EditorApp::draw_generate_tab(Rectangle area) {
	const auto &generators = all_generators();
	std::string names;
	for (const auto &g : generators) {
		names += (names.empty() ? "" : ";") + g->label();
	}
	GuiComboBox(Rectangle{ area.x, area.y, area.width, 24 }, names.c_str(), &gen_index_);
	gen_index_ = std::clamp(gen_index_, 0, static_cast<int>(generators.size()) - 1);
	const Generator &generator = *generators[static_cast<std::size_t>(gen_index_)];
	ParamValues &values = gen_params_[generator.id()];
	if (values.empty()) {
		values = default_params(generator);
	}

	float y = area.y + 32;
	for (const ParamDesc &d : generator.params()) {
		ParamValue &value = values[d.key];
		switch (d.kind) {
			case ParamKind::kInt:
			case ParamKind::kFloat: {
				char label[96];
				if (d.kind == ParamKind::kInt) {
					std::snprintf(label, sizeof label, "%s: %d", d.label.c_str(), static_cast<int>(std::lround(value.number)));
				} else {
					std::snprintf(label, sizeof label, "%s: %.2f", d.label.c_str(), value.number);
				}
				GuiLabel(Rectangle{ area.x, y, area.width, 18 }, label);
				float f = static_cast<float>(value.number);
				GuiSlider(Rectangle{ area.x, y + 18, area.width, 14 }, nullptr, nullptr, &f, static_cast<float>(d.min), static_cast<float>(d.max));
				value.number = d.kind == ParamKind::kInt ? std::round(static_cast<double>(f)) : static_cast<double>(f);
				y += 38;
				break;
			}
			case ParamKind::kBlock: {
				GuiLabel(Rectangle{ area.x, y, area.width * 0.4f, 24 }, d.label.c_str());
				// Click to use the current brush.
				const std::string shown = value.text.empty() ? std::string("(brush)") : value.text;
				if (GuiButton(Rectangle{ area.x + area.width * 0.4f, y, area.width * 0.6f, 24 }, shown.c_str()) && session_->brush()) {
					value.text = *session_->brush();
				}
				y += 30;
				break;
			}
			case ParamKind::kChoice: {
				std::string choices;
				for (const std::string &c : d.choices) {
					choices += (choices.empty() ? "" : ";") + c;
				}
				GuiLabel(Rectangle{ area.x, y, area.width * 0.4f, 24 }, d.label.c_str());
				int index = static_cast<int>(value.number);
				GuiComboBox(Rectangle{ area.x + area.width * 0.4f, y, area.width * 0.6f, 24 }, choices.c_str(), &index);
				value.number = index;
				y += 30;
				break;
			}
		}
	}
	y += 4;
	GuiLabel(Rectangle{ area.x, y, 46, 24 }, "Seed");
	if (GuiValueBox(Rectangle{ area.x + 48, y, 90, 24 }, nullptr, &gen_seed_, 0, 1000000000, gen_value_edit_[0])) {
		gen_value_edit_[0] = !gen_value_edit_[0];
	}
	if (GuiButton(Rectangle{ area.x + 144, y, area.width - 144, 24 }, "Random")) {
		gen_seed_ = static_cast<int>(core::noise::mix64(static_cast<std::uint64_t>(gen_seed_) + 0x9E3779B9ULL) % 1000000000ULL);
	}
	y += 32;
	if (GuiButton(Rectangle{ area.x, y, area.width, 28 }, "Generate (replaces this variant)")) {
		session_->generate(generator, values, static_cast<std::uint64_t>(gen_seed_));
		set_status("generated " + generator.label());
	}
	y += 34;
	GuiLabel(Rectangle{ area.x, y, 20, 24 }, "N");
	if (GuiValueBox(Rectangle{ area.x + 22, y, 50, 24 }, nullptr, &bake_count_, 1, 32, gen_value_edit_[1])) {
		gen_value_edit_[1] = !gen_value_edit_[1];
	}
	if (GuiButton(Rectangle{ area.x + 78, y, area.width - 78, 24 }, "Bake xN (adds variants)")) {
		session_->bake(generator, values, static_cast<std::uint64_t>(gen_seed_), bake_count_);
		set_status("baked " + std::to_string(bake_count_) + " variants");
	}
}

void EditorApp::draw_variants_tab(Rectangle area) {
	const StructureDoc &doc = session_->doc();
	float y = area.y;
	if (GuiButton(Rectangle{ area.x, y, area.width / 2 - 2, 24 }, "Add blank")) {
		session_->add_variant();
	}
	if (GuiButton(Rectangle{ area.x + area.width / 2 + 2, y, area.width / 2 - 2, 24 }, "Duplicate")) {
		session_->duplicate_variant(session_->variant());
	}
	y += 30;
	GuiLabel(Rectangle{ area.x, y, area.width, 18 }, "One is chosen per placement, by weight.");
	y += 24;

	double total = 0.0;
	for (const DocVariant &v : doc.variants) {
		total += v.weight;
	}
	for (std::size_t i = 0; i < doc.variants.size(); ++i) {
		const Rectangle row{ area.x, y, area.width, 28 };
		if (row.y + row.height > area.y + area.height) {
			break;
		}
		const bool selected = i == session_->variant();
		DrawRectangleRec(row, selected ? Color{ 70, 110, 170, 255 } : Color{ 232, 232, 236, 255 });
		char label[48];
		std::snprintf(label, sizeof label, "%zu  (%.0f%%)", i + 1, total > 0 ? doc.variants[i].weight / total * 100.0 : 0.0);
		const Rectangle label_r{ row.x + 4, row.y + 2, 74, 24 };
		DrawText(label, static_cast<int>(label_r.x), static_cast<int>(label_r.y + 8), 10, selected ? WHITE : Color{ 30, 30, 36, 255 });
		if (inside(label_r, GetMousePosition()) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
			session_->set_variant(i);
		}

		// Weight: click to type, Enter to commit.
		const Rectangle weight_r{ row.x + 80, row.y + 2, 54, 24 };
		if (weight_edit_ == static_cast<int>(i)) {
			bool editing = true;
			if (text_box(weight_r, weight_text_, editing)) {
				const double w = std::strtod(weight_text_.c_str(), nullptr);
				session_->set_weight(i, w);
				weight_edit_ = -1;
			}
		} else {
			char w[32];
			std::snprintf(w, sizeof w, "%.3g", doc.variants[i].weight);
			if (GuiButton(weight_r, w)) {
				weight_edit_ = static_cast<int>(i);
				weight_text_ = w;
			}
		}
		if (GuiButton(Rectangle{ row.x + 138, row.y + 2, 28, 24 }, "^")) {
			session_->move_variant(i, -1);
		}
		if (GuiButton(Rectangle{ row.x + 168, row.y + 2, 28, 24 }, "v")) {
			session_->move_variant(i, 1);
		}
		if (GuiButton(Rectangle{ row.x + 198, row.y + 2, 28, 24 }, "x")) {
			session_->delete_variant(i);
		}
		y += 32;
	}
}

// ---------------------------------------------------------------------------
// Placement tab

void EditorApp::draw_placement_tab(Rectangle area) {
	const worldgen::PlacementSpec current = session_->doc().placement;
	worldgen::PlacementSpec next = current;
	float y = area.y;

	GuiLabel(Rectangle{ area.x, y, area.width, 20 }, "Defaults a biome entry can override");
	y += 24;

	// on: the block directly under the anchor.
	GuiLabel(Rectangle{ area.x, y, area.width, 20 }, "On (block under the anchor)");
	y += 22;
	std::vector<std::string> on = next.on.value_or(std::vector<std::string>{});
	if (on.empty()) {
		GuiLabel(Rectangle{ area.x + 6, y, area.width - 6, 20 }, "any solid block");
		y += 22;
	}
	for (std::size_t i = 0; i < on.size();) {
		GuiLabel(Rectangle{ area.x + 6, y, area.width - 40, 22 }, on[i].c_str());
		if (GuiButton(Rectangle{ area.x + area.width - 30, y, 28, 22 }, "x")) {
			on.erase(on.begin() + static_cast<std::ptrdiff_t>(i));
			next.on = on;
			continue;
		}
		y += 24;
		++i;
	}
	const bool can_add = session_->brush() && std::find(on.begin(), on.end(), *session_->brush()) == on.end();
	if (!can_add) {
		GuiDisable();
	}
	if (GuiButton(Rectangle{ area.x, y, area.width, 24 }, "+ add the current brush")) {
		on.push_back(*session_->brush());
		next.on = on;
	}
	GuiEnable();
	y += 32;

	// replace
	GuiLabel(Rectangle{ area.x, y, area.width * 0.35f, 24 }, "Replace");
	int replace = static_cast<int>(next.replace.value_or(worldgen::ReplacePolicy::kAir));
	GuiComboBox(Rectangle{ area.x + area.width * 0.35f, y, area.width * 0.65f, 24 }, "air;air + plants;everything", &replace);
	next.replace = static_cast<worldgen::ReplacePolicy>(replace);
	y += 32;

	bool rotate = next.rotate.value_or(worldgen::kDefaultRotate);
	bool mirror = next.mirror.value_or(worldgen::kDefaultMirror);
	GuiCheckBox(Rectangle{ area.x, y, 18, 18 }, "Rotate", &rotate);
	GuiCheckBox(Rectangle{ area.x + 110, y, 18, 18 }, "Mirror", &mirror);
	next.rotate = rotate;
	next.mirror = mirror;
	y += 30;

	const auto int_field = [&](const char *label, std::optional<int> &field, int fallback, int lo, int hi, int slot) {
		int value = field.value_or(fallback);
		GuiLabel(Rectangle{ area.x, y, area.width * 0.55f, 24 }, label);
		if (GuiValueBox(Rectangle{ area.x + area.width * 0.55f, y, area.width * 0.45f, 24 }, nullptr, &value, lo, hi, placement_edit_[slot])) {
			placement_edit_[slot] = !placement_edit_[slot];
		}
		if (value != field.value_or(fallback)) {
			field = value;
		}
		y += 30;
	};
	int_field("Min spacing", next.min_spacing, worldgen::kDefaultMinSpacing, 1, 64, 0);
	int_field("Max slope", next.max_slope, worldgen::kDefaultMaxSlope, 0, 64, 1);
	int_field("Lowest y", next.y_min, worldgen::kDefaultYMin, -64, 512, 2);
	int_field("Highest y", next.y_max, worldgen::kDefaultYMax, -64, 512, 3);

	// Cluster: commit on release so a drag is one undo step.
	if (!cluster_dragging_) {
		cluster_ui_ = static_cast<float>(next.cluster.value_or(worldgen::kDefaultCluster));
	}
	char label[48];
	std::snprintf(label, sizeof label, "Cluster: %.2f", static_cast<double>(cluster_ui_));
	GuiLabel(Rectangle{ area.x, y, area.width, 18 }, label);
	const Rectangle slider{ area.x, y + 18, area.width, 14 };
	GuiSlider(slider, nullptr, nullptr, &cluster_ui_, 0.0f, 1.0f);
	if (inside(slider, GetMousePosition()) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
		cluster_dragging_ = true;
	}
	if (cluster_dragging_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
		cluster_dragging_ = false;
		next.cluster = std::round(static_cast<double>(cluster_ui_) * 100.0) / 100.0;
	}
	y += 40;

	if (GuiButton(Rectangle{ area.x, y, area.width, 24 }, "Reset to engine defaults")) {
		next = {};
	}
	y += 30;
	GuiLabel(Rectangle{ area.x, y, area.width, 36 },
			"Spawn rate is set where the structure\nis used (a biome's decoration entry).");

	if (!(next == current)) {
		session_->set_placement(next);
	}
}

// ---------------------------------------------------------------------------
// Preview

void EditorApp::load_other_structures() {
	other_specs_.clear();
	if (!session_) {
		return;
	}
	for (const StructureEntry &entry : workspace_.structures()) {
		if (!entry.ok || entry.name == session_->doc().name) {
			continue;
		}
		std::string error;
		if (auto doc = workspace_.open_structure(entry.name, &error)) {
			other_specs_.push_back(doc->to_spec());
		}
	}
}

void EditorApp::mark_preview_dirty() {
	preview_pending_ = true;
	preview_changed_at_ = GetTime();
}

void EditorApp::update_preview() {
	if (!session_) {
		return;
	}
	if (!preview_) {
		preview_ = std::make_unique<TerrainPreview>(workspace_.catalog());
		preview_pending_ = true;
	}
	// Any edit (cells, variants, placement) re-runs the preview, debounced so
	// a drag or typing doesn't regenerate on every frame.
	if (session_->revision() != preview_seen_revision_) {
		preview_seen_revision_ = session_->revision();
		mark_preview_dirty();
	}
	if (preview_pending_ && GetTime() - preview_changed_at_ >= 0.4) {
		preview_pending_ = false;
		preview_input_.structures.clear();
		preview_input_.structures.push_back(session_->doc().to_spec());
		for (const worldgen::StructureSpec &other : other_specs_) {
			preview_input_.structures.push_back(other);
		}
		const bool first = !preview_->started();
		preview_->start(preview_input_);
		if (first) {
			const core::Vec3d p = preview_->spawn_point();
			fly_.set_position(p);
			fly_.set_look(0.0, -35.0);
		}
	}
	preview_->poll();
}

void EditorApp::handle_preview_input() {
	if (IsKeyPressed(KEY_F5)) {
		reload();
	}
	update_preview();
	if (mouse_over_ui() || dialog_ != Dialog::kNone || preview_value_edit_[0] || preview_value_edit_[1]) {
		return;
	}
	render::LookMoveInput in;
	if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
		const Vector2 d = GetMouseDelta();
		in.look_delta = { static_cast<double>(d.x), static_cast<double>(d.y) };
	}
	in.move_axis.z = (IsKeyDown(KEY_W) ? 1.0 : 0.0) - (IsKeyDown(KEY_S) ? 1.0 : 0.0);
	in.move_axis.x = (IsKeyDown(KEY_D) ? 1.0 : 0.0) - (IsKeyDown(KEY_A) ? 1.0 : 0.0);
	in.move_axis.y = (IsKeyDown(KEY_E) || IsKeyDown(KEY_SPACE) ? 1.0 : 0.0) - (IsKeyDown(KEY_Q) ? 1.0 : 0.0);
	in.sprint = IsKeyDown(KEY_LEFT_SHIFT);
	fly_.set_speed(28.0);
	fly_.update(in, static_cast<double>(GetFrameTime()));
	if (IsKeyPressed(KEY_R) && preview_) {
		fly_.set_position(preview_->spawn_point());
		fly_.set_look(0.0, -35.0);
	}
}

void EditorApp::draw_preview_world() {
	update_preview();
	if (!preview_renderer_) {
		preview_renderer_ = make_renderer(workspace_);
	}
	Camera3D camera{};
	const core::Vec3d eye = fly_.position();
	const core::Vec3d target = fly_.target();
	camera.position = Vector3{ static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z) };
	camera.target = Vector3{ static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.z) };
	camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
	camera.fovy = 60.0f;
	camera.projection = CAMERA_PERSPECTIVE;

	preview_renderer_->sync(preview_->store(), 64, 64);
	preview_renderer_->set_fog(eye, world::SkyColor{ 150, 190, 230 }, 160.0f, 420.0f);
	ClearBackground(Color{ 150, 190, 230, 255 });
	BeginMode3D(camera);
	preview_renderer_->draw(camera);
	EndMode3D();
}

void EditorApp::draw_preview_tab(Rectangle area) {
	TerrainConfig &t = preview_input_.terrain;
	float y = area.y;
	const auto label = [&](const std::string &text) {
		GuiLabel(Rectangle{ area.x, y, area.width, 18 }, text.c_str());
		y += 18;
	};
	bool changed = false;
	const auto slider = [&](const char *name, double &value, double lo, double hi, bool integer) {
		char text[64];
		std::snprintf(text, sizeof text, integer ? "%s: %.0f" : "%s: %.1f", name, value);
		label(text);
		float f = static_cast<float>(value);
		GuiSlider(Rectangle{ area.x, y, area.width, 14 }, nullptr, nullptr, &f, static_cast<float>(lo), static_cast<float>(hi));
		const double v = integer ? std::round(static_cast<double>(f)) : std::round(static_cast<double>(f) * 10.0) / 10.0;
		if (std::abs(v - value) > 1e-9 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
			value = v;
			changed = true;
		}
		y += 22;
	};

	if (!preview_) {
		GuiLabel(Rectangle{ area.x, y, area.width, 20 }, "starting...");
		return;
	}
	const PreviewStats &stats = preview_->stats();
	char counters[96];
	std::snprintf(counters, sizeof counters, "placed: %zu this structure, %zu others", stats.edited_placements, stats.other_placements);
	label(counters);
	label(preview_->busy() ? "generating terrain..." : (stats.error.empty() ? "ready  (WASD, Q/E, right-drag, R resets)" : "not built"));
	y += 4;

	double amplitude = t.amplitude;
	double scale = t.scale;
	double sea = t.sea_level;
	double density = preview_input_.spawn_rate;
	slider("Hilliness", amplitude, 0.0, 30.0, false);
	slider("Feature size", scale, 16.0, 128.0, true);
	slider("Sea level", sea, 20.0, 100.0, true);
	slider("Density per 32x32", density, 0.0, 8.0, false);
	t.amplitude = amplitude;
	t.scale = scale;
	t.sea_level = static_cast<int>(sea);
	preview_input_.spawn_rate = density;

	const auto block_button = [&](const char *name, std::string &field) {
		GuiLabel(Rectangle{ area.x, y, area.width * 0.35f, 22 }, name);
		if (GuiButton(Rectangle{ area.x + area.width * 0.35f, y, area.width * 0.65f, 22 }, field.c_str()) && session_->brush()) {
			field = *session_->brush();
			changed = true;
		}
		y += 26;
	};
	block_button("Surface", t.surface);
	block_button("Filler", t.filler);
	block_button("Stone", t.stone);
	label("(click a block button to use the Blocks tab brush)");

	GuiLabel(Rectangle{ area.x, y, 40, 24 }, "Seed");
	if (GuiValueBox(Rectangle{ area.x + 42, y, 80, 24 }, nullptr, &preview_seed_box_, 0, 1000000000, preview_value_edit_[0])) {
		preview_value_edit_[0] = !preview_value_edit_[0];
		if (!preview_value_edit_[0]) {
			t.seed = static_cast<std::uint64_t>(preview_seed_box_);
			changed = true;
		}
	}
	if (GuiButton(Rectangle{ area.x + 128, y, area.width - 128, 24 }, "Re-roll")) {
		preview_seed_box_ = static_cast<int>(core::noise::mix64(static_cast<std::uint64_t>(preview_seed_box_) + 0x9E3779B9ULL) % 1000000000ULL);
		t.seed = static_cast<std::uint64_t>(preview_seed_box_);
		changed = true;
	}
	y += 30;

	GuiLabel(Rectangle{ area.x, y, 60, 24 }, "Patch");
	int patch = t.patch;
	if (GuiValueBox(Rectangle{ area.x + 42, y, 50, 24 }, nullptr, &patch, 2, 16, preview_value_edit_[1])) {
		preview_value_edit_[1] = !preview_value_edit_[1];
	}
	if (patch != t.patch) {
		t.patch = patch;
		changed = true;
	}
	GuiLabel(Rectangle{ area.x + 98, y, area.width - 98, 24 }, "columns per side");
	y += 30;

	bool others = preview_input_.include_others;
	GuiCheckBox(Rectangle{ area.x, y, 18, 18 }, "Also place the folder's other structures", &others);
	if (others != preview_input_.include_others) {
		preview_input_.include_others = others;
		changed = true;
	}
	y += 28;

	if (!stats.error.empty()) {
		DrawRectangle(static_cast<int>(area.x), static_cast<int>(y), static_cast<int>(area.width), 44, Color{ 200, 70, 70, 255 });
		DrawText(stats.error.substr(0, 46).c_str(), static_cast<int>(area.x + 4), static_cast<int>(y + 4), 10, WHITE);
		if (stats.error.size() > 46) {
			DrawText(stats.error.substr(46, 46).c_str(), static_cast<int>(area.x + 4), static_cast<int>(y + 20), 10, WHITE);
		}
		y += 50;
	}
	for (const std::string &warning : stats.warnings) {
		if (y + 36 > area.y + area.height) {
			break;
		}
		DrawRectangle(static_cast<int>(area.x), static_cast<int>(y), static_cast<int>(area.width), 44, Color{ 230, 180, 60, 255 });
		DrawText(warning.substr(0, 46).c_str(), static_cast<int>(area.x + 4), static_cast<int>(y + 4), 10, Color{ 40, 30, 0, 255 });
		if (warning.size() > 46) {
			DrawText(warning.substr(46, 46).c_str(), static_cast<int>(area.x + 4), static_cast<int>(y + 18), 10, Color{ 40, 30, 0, 255 });
		}
		if (warning.size() > 92) {
			DrawText(warning.substr(92, 46).c_str(), static_cast<int>(area.x + 4), static_cast<int>(y + 32), 10, Color{ 40, 30, 0, 255 });
		}
		y += 50;
	}
	if (changed) {
		mark_preview_dirty();
	}
}

void EditorApp::draw_open_dialog() {
	if (!show_open_) {
		return;
	}
	const float w = 420.0f;
	const float h = 360.0f;
	const Rectangle box{ (static_cast<float>(GetScreenWidth()) - w) * 0.5f, 90.0f, w, h };
	register_ui(box);
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
		const std::string name = workspace_.structures()[static_cast<std::size_t>(open_active_)].name;
		show_open_ = false;
		guard_unsaved([this, name] { open_structure(name); });
	}
	GuiEnable();
}

void EditorApp::draw_errors_panel() {
	if (!show_errors_) {
		return;
	}
	const Rectangle panel{ 0, static_cast<float>(GetScreenHeight()) - kErrorsHeight, static_cast<float>(GetScreenWidth()), kErrorsHeight };
	register_ui(panel);
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

void EditorApp::draw_dialogs() {
	if (dialog_ == Dialog::kNone) {
		return;
	}
	const float sw = static_cast<float>(GetScreenWidth());
	const float sh = static_cast<float>(GetScreenHeight());
	// Dim everything behind the dialog and swallow viewport input.
	DrawRectangle(0, 0, static_cast<int>(sw), static_cast<int>(sh), Color{ 0, 0, 0, 120 });
	register_ui(Rectangle{ 0, 0, sw, sh });

	const auto value_box = [&](Rectangle r, const char *label, int *value, int lo, int hi, int slot) {
		GuiLabel(Rectangle{ r.x - 22, r.y, 20, r.height }, label);
		if (GuiValueBox(r, nullptr, value, lo, hi, dialog_value_edit_[slot])) {
			dialog_value_edit_[slot] = !dialog_value_edit_[slot];
		}
	};

	if (dialog_ == Dialog::kNew) {
		const Rectangle box{ (sw - 420) * 0.5f, 140, 420, 290 };
		if (GuiWindowBox(box, "New structure")) {
			dialog_ = Dialog::kNone;
			return;
		}
		GuiLabel(Rectangle{ box.x + 14, box.y + 38, 100, 22 }, "Name");
		text_box(Rectangle{ box.x + 14, box.y + 60, box.width - 28, 26 }, dialog_name_, dialog_name_edit_);
		GuiLabel(Rectangle{ box.x + 14, box.y + 96, 200, 22 }, "Size (1..64)");
		value_box(Rectangle{ box.x + 40, box.y + 120, 80, 26 }, "x", &dialog_size_[0], 1, worldgen::kMaxStructureDim, 0);
		value_box(Rectangle{ box.x + 160, box.y + 120, 80, 26 }, "y", &dialog_size_[1], 1, worldgen::kMaxStructureDim, 1);
		value_box(Rectangle{ box.x + 280, box.y + 120, 80, 26 }, "z", &dialog_size_[2], 1, worldgen::kMaxStructureDim, 2);
		GuiLabel(Rectangle{ box.x + 14, box.y + 158, 300, 22 }, "Anchor (the cell on the ground block)");
		value_box(Rectangle{ box.x + 40, box.y + 182, 80, 26 }, "x", &dialog_anchor_[0], 0, worldgen::kMaxStructureDim - 1, 3);
		value_box(Rectangle{ box.x + 160, box.y + 182, 80, 26 }, "y", &dialog_anchor_[1], 0, worldgen::kMaxStructureDim - 1, 4);
		value_box(Rectangle{ box.x + 280, box.y + 182, 80, 26 }, "z", &dialog_anchor_[2], 0, worldgen::kMaxStructureDim - 1, 5);
		const bool name_ok = dialog_name_.find(':') != std::string::npos && dialog_name_.back() != ':';
		if (!name_ok) {
			GuiLabel(Rectangle{ box.x + 14, box.y + 222, box.width - 28, 22 }, "Name must look like pack:name");
			GuiDisable();
		}
		if (GuiButton(Rectangle{ box.x + box.width - 114, box.y + box.height - 44, 100, 30 }, "Create")) {
			const core::IVec3 size{ dialog_size_[0], dialog_size_[1], dialog_size_[2] };
			const core::IVec3 anchor{ std::min(dialog_anchor_[0], size.x - 1), std::min(dialog_anchor_[1], size.y - 1),
				std::min(dialog_anchor_[2], size.z - 1) };
			const std::string name = dialog_name_;
			dialog_ = Dialog::kNone;
			guard_unsaved([this, name, size, anchor] { new_structure(name, size, anchor); });
		}
		GuiEnable();
		return;
	}

	if (dialog_ == Dialog::kSaveAs) {
		const Rectangle box{ (sw - 420) * 0.5f, 160, 420, 190 };
		if (GuiWindowBox(box, "Save as")) {
			dialog_ = Dialog::kNone;
			return;
		}
		GuiLabel(Rectangle{ box.x + 14, box.y + 38, 100, 22 }, "Name");
		text_box(Rectangle{ box.x + 14, box.y + 60, box.width - 28, 26 }, dialog_name_, dialog_name_edit_);
		const bool name_ok = dialog_name_.find(':') != std::string::npos && dialog_name_.back() != ':';
		const bool exists = name_ok && std::filesystem::exists(workspace_.path_for(dialog_name_)) &&
				(!session_ || dialog_name_ != session_->doc().name);
		if (exists) {
			GuiLabel(Rectangle{ box.x + 14, box.y + 96, box.width - 28, 22 }, "A structure with this file name exists and will be overwritten.");
		} else if (!name_ok) {
			GuiLabel(Rectangle{ box.x + 14, box.y + 96, box.width - 28, 22 }, "Name must look like pack:name");
			GuiDisable();
		}
		if (GuiButton(Rectangle{ box.x + box.width - 114, box.y + box.height - 44, 100, 30 }, exists ? "Overwrite" : "Save")) {
			if (save_as(dialog_name_)) {
				dialog_ = Dialog::kNone;
			}
		}
		GuiEnable();
		return;
	}

	if (dialog_ == Dialog::kResize) {
		const Rectangle box{ (sw - 460) * 0.5f, 140, 460, 270 };
		if (GuiWindowBox(box, "Resize structure")) {
			dialog_ = Dialog::kNone;
			return;
		}
		GuiLabel(Rectangle{ box.x + 14, box.y + 38, 300, 22 }, "New size (1..64) and the side that stays put");
		const char *axes[3] = { "x", "y", "z" };
		for (int i = 0; i < 3; ++i) {
			const float y = box.y + 70 + static_cast<float>(i) * 40;
			value_box(Rectangle{ box.x + 40, y, 90, 28 }, axes[i], &dialog_size_[i], 1, worldgen::kMaxStructureDim, i);
			GuiComboBox(Rectangle{ box.x + 160, y, 150, 28 }, i == 1 ? "bottom;middle;top" : "min;center;max", &dialog_side_[i]);
		}
		GuiLabel(Rectangle{ box.x + 14, box.y + 196, box.width - 28, 22 }, "Content keeps its place on the chosen side; the anchor moves with it.");
		if (GuiButton(Rectangle{ box.x + box.width - 114, box.y + box.height - 44, 100, 30 }, "Resize")) {
			const auto side = [](int v) { return v == 0 ? ResizeSide::kMin : (v == 1 ? ResizeSide::kCenter : ResizeSide::kMax); };
			session_->resize({ dialog_size_[0], dialog_size_[1], dialog_size_[2] }, { side(dialog_side_[0]), side(dialog_side_[1]), side(dialog_side_[2]) });
			dialog_ = Dialog::kNone;
		}
		return;
	}

	if (dialog_ == Dialog::kUnsaved) {
		const Rectangle box{ (sw - 420) * 0.5f, 200, 420, 150 };
		if (GuiWindowBox(box, "Unsaved changes")) {
			dialog_ = Dialog::kNone;
			after_unsaved_ = nullptr;
			return;
		}
		GuiLabel(Rectangle{ box.x + 14, box.y + 44, box.width - 28, 24 }, "This structure has changes that are not saved.");
		if (GuiButton(Rectangle{ box.x + 14, box.y + box.height - 44, 110, 30 }, "Save")) {
			if (save()) {
				dialog_ = Dialog::kNone;
				auto action = std::move(after_unsaved_);
				after_unsaved_ = nullptr;
				if (action) {
					action();
				}
			} else {
				dialog_ = Dialog::kNone;
				after_unsaved_ = nullptr;
			}
		}
		if (GuiButton(Rectangle{ box.x + 140, box.y + box.height - 44, 110, 30 }, "Discard")) {
			dialog_ = Dialog::kNone;
			auto action = std::move(after_unsaved_);
			after_unsaved_ = nullptr;
			if (session_) {
				session_->mark_saved();
			}
			if (action) {
				action();
			}
		}
		if (GuiButton(Rectangle{ box.x + 266, box.y + box.height - 44, 110, 30 }, "Cancel")) {
			dialog_ = Dialog::kNone;
			after_unsaved_ = nullptr;
		}
	}
}

void EditorApp::draw_ui() {
	ui_rects_.clear();
	draw_top_bar();
	draw_tool_bar();
	draw_side_panel();
	draw_errors_panel();
	draw_open_dialog();
	draw_dialogs();
}

// ---------------------------------------------------------------------------
// Dev/testing script

// "cmd;cmd;..." with commands:
//   new <name> <sx,sy,sz> [ax,ay,az]   brush <block|keep>   tool <number 1-9>
//   place|remove|paint|flood x,y,z      box|line x,y,z x,y,z
//   mirror x|z|off                      slice <layer>        select x,y,z x,y,z
//   copy|cut                            paste x,y,z          variant <n>
//   set <generator> <key> <value>   generate <id> [seed]   bake <id> <n> [seed]
//   rule key=value...   tab <0-4>   save
void EditorApp::run_script(const std::string &script) {
	std::stringstream commands(script);
	std::string command;
	while (std::getline(commands, command, ';')) {
		std::stringstream words(command);
		std::vector<std::string> w;
		for (std::string word; words >> word;) {
			w.push_back(word);
		}
		if (w.empty()) {
			continue;
		}
		core::IVec3 a{}, b{};
		if (w[0] == "new" && w.size() >= 3 && parse_ivec(w[2], a)) {
			core::IVec3 anchor{ a.x / 2, 0, a.z / 2 };
			if (w.size() >= 4) {
				parse_ivec(w[3], anchor);
			}
			new_structure(w[1], a, anchor);
		} else if (!session_) {
			continue;
		} else if (w[0] == "brush" && w.size() >= 2) {
			session_->set_brush(w[1] == "keep" ? std::nullopt : std::optional<std::string>(w[1]));
		} else if (w[0] == "place" && w.size() >= 2 && parse_ivec(w[1], a)) {
			session_->place(a);
		} else if (w[0] == "remove" && w.size() >= 2 && parse_ivec(w[1], a)) {
			session_->remove(a);
		} else if (w[0] == "paint" && w.size() >= 2 && parse_ivec(w[1], a)) {
			session_->paint(a);
		} else if (w[0] == "flood" && w.size() >= 2 && parse_ivec(w[1], a)) {
			session_->flood_replace(a);
		} else if (w[0] == "box" && w.size() >= 3 && parse_ivec(w[1], a) && parse_ivec(w[2], b)) {
			session_->box_fill(a, b);
		} else if (w[0] == "line" && w.size() >= 3 && parse_ivec(w[1], a) && parse_ivec(w[2], b)) {
			session_->line(a, b);
		} else if (w[0] == "select" && w.size() >= 3 && parse_ivec(w[1], a) && parse_ivec(w[2], b)) {
			session_->select(box_between(a, b));
		} else if (w[0] == "copy") {
			session_->copy_selection();
		} else if (w[0] == "cut") {
			session_->cut_selection();
		} else if (w[0] == "paste" && w.size() >= 2 && parse_ivec(w[1], a)) {
			session_->paste(a);
		} else if (w[0] == "mirror" && w.size() >= 2) {
			session_->symmetry.mirror_x = w[1] == "x" || w[1] == "xz";
			session_->symmetry.mirror_z = w[1] == "z" || w[1] == "xz";
		} else if (w[0] == "slice" && w.size() >= 2) {
			slice_ = std::atoi(w[1].c_str());
			seen_revision_ = 0;
		} else if (w[0] == "variant" && w.size() >= 2) {
			session_->set_variant(static_cast<std::size_t>(std::atoi(w[1].c_str())));
		} else if (w[0] == "tool" && w.size() >= 2) {
			static constexpr Tool kTools[] = { Tool::kPlace, Tool::kRemove, Tool::kPaint, Tool::kPick, Tool::kBox, Tool::kLine,
				Tool::kFlood, Tool::kSelect, Tool::kAnchor };
			const int n = std::atoi(w[1].c_str());
			if (n >= 1 && n <= 9) {
				tool_ = kTools[n - 1];
			}
		} else if (w[0] == "set" && w.size() >= 4) {
			// set <generator> <key> <value>: a generator parameter (a number
			// or, for block parameters, a block name).
			if (const Generator *g = find_generator(w[1])) {
				ParamValues &values = gen_params_[g->id()];
				if (values.empty()) {
					values = default_params(*g);
				}
				ParamValue &v = values[w[2]];
				v.number = std::atof(w[3].c_str());
				v.text = w[3];
			}
		} else if (w[0] == "generate" && w.size() >= 2) {
			if (const Generator *g = find_generator(w[1])) {
				const auto seed = static_cast<std::uint64_t>(w.size() >= 3 ? std::atoll(w[2].c_str()) : 1);
				ParamValues &values = gen_params_[g->id()];
				session_->generate(*g, values.empty() ? default_params(*g) : values, seed);
			}
		} else if (w[0] == "bake" && w.size() >= 3) {
			if (const Generator *g = find_generator(w[1])) {
				const auto seed = static_cast<std::uint64_t>(w.size() >= 4 ? std::atoll(w[3].c_str()) : 1);
				ParamValues &values = gen_params_[g->id()];
				session_->bake(*g, values.empty() ? default_params(*g) : values, seed, std::atoi(w[2].c_str()));
			}
		} else if (w[0] == "rule") {
			// rule key=value...: the structure's placement defaults, keys on
			// (comma list), replace, rotate, mirror, spacing, slope, cluster,
			// ymin, ymax.
			worldgen::PlacementSpec p = session_->doc().placement;
			for (std::size_t i = 1; i < w.size(); ++i) {
				const auto eq = w[i].find('=');
				if (eq == std::string::npos) {
					continue;
				}
				const std::string key = w[i].substr(0, eq);
				const std::string value = w[i].substr(eq + 1);
				if (key == "on") {
					std::vector<std::string> names;
					std::stringstream parts(value);
					for (std::string part; std::getline(parts, part, ',');) {
						names.push_back(part);
					}
					p.on = names;
				} else if (key == "replace") {
					p.replace = worldgen::parse_replace_policy(value);
				} else if (key == "rotate") {
					p.rotate = value == "1" || value == "true";
				} else if (key == "mirror") {
					p.mirror = value == "1" || value == "true";
				} else if (key == "spacing") {
					p.min_spacing = std::atoi(value.c_str());
				} else if (key == "slope") {
					p.max_slope = std::atoi(value.c_str());
				} else if (key == "cluster") {
					p.cluster = std::atof(value.c_str());
				} else if (key == "ymin") {
					p.y_min = std::atoi(value.c_str());
				} else if (key == "ymax") {
					p.y_max = std::atoi(value.c_str());
				}
			}
			session_->set_placement(p);
		} else if (w[0] == "tab" && w.size() >= 2) {
			side_tab_ = static_cast<SideTab>(std::clamp(std::atoi(w[1].c_str()), 0, 4));
			if (side_tab_ == SideTab::kPreview) {
				load_other_structures();
				mark_preview_dirty();
				preview_changed_at_ = GetTime() - 1.0;
			}
		} else if (w[0] == "save") {
			save();
		} else {
			std::cerr << "vb_structure_editor: unknown script command '" << command << "'\n";
		}
	}
}

// ---------------------------------------------------------------------------

int EditorApp::run() {
	if (!options_.script.empty()) {
		run_script(options_.script);
		frame_camera();
	}
	int frame = 0;
	while (!quit_) {
		if (window_->should_close()) {
			// The window's close button: ask first if there is unsaved work.
			guard_unsaved([this] { quit_ = true; });
		}
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
