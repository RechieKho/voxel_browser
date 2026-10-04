#pragma once

#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

#include "vb/protocol/input.hpp"

// Input seam between raylib and the client's game logic (e2e design,
// docs/e2e-automation.md §4.1). The client polls one InputSource per frame
// into an InputFrame and everything downstream (sample_input_cmd, chat/
// hotbar/mouse-capture logic, the keybindings screen) reads that snapshot
// instead of calling raylib directly. RaylibInput is today's behavior;
// SyntheticInput is script-fed and needs no window, so the same code is
// unit-testable and, later, automation-drivable.

namespace vb::render {

// Key / button values are raylib's KeyboardKey / MouseButton enums.
inline constexpr std::size_t kInputKeyCount = 512;
inline constexpr std::size_t kInputMouseButtonCount = 8;

struct InputFrame {
	std::bitset<kInputKeyCount> keys_down;
	std::bitset<kInputKeyCount> keys_pressed;
	std::bitset<kInputMouseButtonCount> mouse_down;
	std::bitset<kInputMouseButtonCount> mouse_pressed;
	double mouse_dx = 0.0, mouse_dy = 0.0;
	// First key pressed this frame (raylib GetKeyPressed()), 0 if none.
	// Used by the keybindings screen's "press any key" capture.
	int key_pressed_first = 0;

	bool key_down(int key) const {
		return key >= 0 && static_cast<std::size_t>(key) < kInputKeyCount &&
				keys_down[static_cast<std::size_t>(key)];
	}
	bool key_pressed(int key) const {
		return key >= 0 && static_cast<std::size_t>(key) < kInputKeyCount &&
				keys_pressed[static_cast<std::size_t>(key)];
	}
	bool mouse_button_down(int button) const {
		return button >= 0 && static_cast<std::size_t>(button) < kInputMouseButtonCount &&
				mouse_down[static_cast<std::size_t>(button)];
	}
	bool mouse_button_pressed(int button) const {
		return button >= 0 && static_cast<std::size_t>(button) < kInputMouseButtonCount &&
				mouse_pressed[static_cast<std::size_t>(button)];
	}
};

class InputSource {
public:
	virtual ~InputSource() = default;
	// Called once per frame.
	virtual InputFrame poll() = 0;
};

// Reads raylib's live input state. Requires an open window.
class RaylibInput final : public InputSource {
public:
	InputFrame poll() override;
};

// Script-fed input: no raylib, no window. Held keys/buttons persist for a
// number of polls; presses and mouse deltas are delivered exactly once.
class SyntheticInput final : public InputSource {
public:
	// Hold `key` down for `frames` polls (first of which also reports a press).
	void hold_key(int key, int frames);
	// Single-frame press (down + pressed for one poll).
	void press_key(int key) { hold_key(key, 1); }
	void hold_mouse_button(int button, int frames);
	void press_mouse_button(int button) { hold_mouse_button(button, 1); }
	// Accumulated into the next poll's mouse delta.
	void look_delta(double dx, double dy);

	InputFrame poll() override;

private:
	struct Hold {
		int code;
		int frames_left;
		bool fresh;
	};
	std::vector<Hold> keys_;
	std::vector<Hold> buttons_;
	double pending_dx_ = 0.0, pending_dy_ = 0.0;
	int pending_first_key_ = 0;
};

// Phase 6.17: physical-key-to-action mapping for the axes the engine always
// understands (InputCmd::move/buttons). Client-local; not network-visible.
struct MovementBindings {
	int forward;
	int back;
	int left;
	int right;
	int jump;
	int sprint;

	MovementBindings(); // WASD / Space / Left Shift
	MovementBindings(int fwd, int bck, int lft, int rgt, int jmp, int spr) :
			forward(fwd), back(bck), left(lft), right(rgt), jump(jmp), sprint(spr) {}
};

// Builds this frame's InputCmd from an InputFrame. See the long comments on
// the engine keybind names / kCustomKeybinds in input.cpp.
vb::protocol::InputCmd sample_input_cmd(const InputFrame &in, std::uint32_t seq,
		double dt, double yaw, double pitch, bool mouse_captured,
		const MovementBindings &bindings,
		const std::vector<std::string> &keybind_names = {},
		std::uint8_t selected_slot = 0);

} // namespace vb::render
