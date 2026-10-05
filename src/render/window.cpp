#include "vb/render/window.hpp"

#include <raylib.h>

#include "vb/core/config.hpp"

namespace vb::render {

Window::Window(const WindowConfig &config) : headless_(config.headless),
											 headless_frame_limit_(config.headless_frame_limit) {
	if (headless_) {
		return;
	}

	unsigned int flags = FLAG_MSAA_4X_HINT;
	if (config.vsync) {
		flags |= FLAG_VSYNC_HINT;
	}
	if (config.resizable) {
		flags |= FLAG_WINDOW_RESIZABLE;
	}
	SetConfigFlags(flags);
	SetTraceLogLevel(LOG_WARNING);
	InitWindow(config.width, config.height, config.title.c_str());
	owns_window_ = true;
	if (config.resizable) {
		// Matches the launch-time floor in vb::core::apply_cli_overrides --
		// without this a resizable window could still be dragged below it.
		SetWindowMinSize(static_cast<int>(vb::core::kMinWindowWidth),
				static_cast<int>(vb::core::kMinWindowHeight));
	}
	if (!config.vsync) {
		SetTargetFPS(0);
	}
}

Window::~Window() {
	if (owns_window_) {
		CloseWindow();
	}
}

bool Window::should_close() const {
	if (headless_) {
		return headless_frame_limit_ != 0 && frames_ >= headless_frame_limit_;
	}
	return WindowShouldClose();
}

void Window::begin_frame() {
	if (!headless_) {
		BeginDrawing();
		ClearBackground(Color{ 18, 18, 22, 255 });
	}
}

void Window::end_frame() {
	if (!headless_) {
		EndDrawing();
	}
	++frames_;
}

} // namespace vb::render
