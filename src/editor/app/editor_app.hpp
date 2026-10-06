#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include "vb/editor/orbit_camera.hpp"
#include "vb/editor/structure_doc.hpp"
#include "vb/editor/volume_view.hpp"
#include "vb/editor/workspace.hpp"
#include "vb/render/chunk_renderer.hpp"
#include "vb/render/window.hpp"

// The structure editor window (docs/structure-editor.md §I). All state that
// matters lives in the headless model (vb_editor_model); this class owns the
// window, the renderer and the panels and turns input into model calls.

namespace vb::editor {

struct EditorOptions {
	std::filesystem::path block_script;
	std::string open_name; // --open <name>
	// Dev/testing: render this many frames, save a screenshot to this path
	// and exit (needs a display; CI uses Xvfb).
	std::filesystem::path screenshot;
	int frames = 0;
	int width = 1360;
	int height = 800;
};

class EditorApp {
public:
	EditorApp(Workspace workspace, EditorOptions options);
	~EditorApp();

	EditorApp(const EditorApp &) = delete;
	EditorApp &operator=(const EditorApp &) = delete;

	int run();

private:
	// --- loading ---
	void rebuild_renderer(); // atlas + ChunkRenderer from the current catalog
	void open_structure(const std::string &name);
	void reload();
	void refresh_view();
	void frame_camera();

	// --- per frame ---
	void handle_input();
	void draw_world();
	void draw_ui();
	void draw_top_bar();
	void draw_side_panel();
	void draw_open_dialog();
	void draw_errors_panel();
	bool mouse_over_ui() const;

	Workspace workspace_;
	EditorOptions options_;
	std::unique_ptr<render::Window> window_;
	std::unique_ptr<render::ChunkRenderer> renderer_;
	std::unique_ptr<VolumeView> view_;
	OrbitCamera camera_;

	std::optional<StructureDoc> doc_;
	std::size_t variant_ = 0;
	std::string status_;

	bool show_open_ = false;
	bool show_errors_ = false;
	int open_scroll_ = 0;
	int open_active_ = -1;
	int errors_scroll_ = 0;
	int errors_active_ = -1;
	std::vector<Rectangle> ui_rects_; // panels drawn this frame, for hit-testing
};

} // namespace vb::editor
