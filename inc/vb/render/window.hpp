#pragma once

#include <cstdint>
#include <string>

// Thin wrapper over raylib's window/context. In headless mode (CI, integration
// tests, dedicated-server-with-client-libs) no GL context is created and every
// draw call is a no-op, so the same client code path runs without a GPU.

namespace vb::render {

struct WindowConfig {
	int width = 1280;
	int height = 720;
	std::string title = "voxel_browser";
	bool vsync = true;
	bool resizable = true;
	bool headless = false;
	// In headless mode the loop auto-stops after this many frames (0 = never).
	std::uint64_t headless_frame_limit = 0;
};

class Window {
public:
	explicit Window(const WindowConfig &config);
	~Window();

	Window(const Window &) = delete;
	Window &operator=(const Window &) = delete;

	bool headless() const { return headless_; }
	std::uint64_t frame_count() const { return frames_; }

	// True when the user closed the window, or (headless) the frame limit hit.
	bool should_close() const;

	// Frame bracket. Between these, raygui / raylib draw calls are valid
	// (or silently ignored when headless).
	void begin_frame();
	void end_frame();

private:
	bool headless_;
	bool owns_window_ = false;
	std::uint64_t frames_ = 0;
	std::uint64_t headless_frame_limit_;
};

} // namespace vb::render
