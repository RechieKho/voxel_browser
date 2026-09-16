#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vb/script/ui_runtime.hpp" // script::Widget

// Draws a vb::script::UiRuntime's widget list with raygui (spec §10.4,
// Phase 4.5). No sol2 here -- Widget is plain data; UiRuntime (vb_core) and
// UiRenderer (vb_render) are the two halves of the client UI VM split
// across the core/render boundary, same as ClientChunkStore/ChunkRenderer.

namespace vb::render {

struct UiFrameResult {
	std::vector<std::string> clicked;
	std::vector<std::pair<std::string, std::string>> changed_text;
	std::vector<std::pair<std::string, int>> changed_list;
};

class UiRenderer {
public:
	// Draws `widgets` via raygui. Call between Window::begin_frame()/
	// end_frame(), outside any BeginMode3D/EndMode3D block (raygui is a 2D
	// immediate-mode overlay). Resets per-widget text/list edit state when
	// `ui_name` differs from the previous call (a different or reopened UI).
	UiFrameResult draw(std::string_view ui_name,
			const std::vector<script::Widget> &widgets);

private:
	std::string last_ui_name_;
	std::unordered_map<std::string, std::string> text_buffers_;
	std::unordered_map<std::string, bool> edit_mode_;
	std::unordered_map<std::string, int> list_active_;
	std::unordered_map<std::string, int> list_scroll_;
};

} // namespace vb::render
