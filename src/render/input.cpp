#include "vb/render/input.hpp"

#include <algorithm>
#include <cstring>

#include <raylib.h>

namespace vb::render {

InputFrame RaylibInput::poll() {
	InputFrame f;
	for (std::size_t k = 0; k < kInputKeyCount; ++k) {
		const int key = static_cast<int>(k);
		if (IsKeyDown(key)) {
			f.keys_down.set(k);
		}
		if (IsKeyPressed(key)) {
			f.keys_pressed.set(k);
		}
	}
	for (std::size_t b = 0; b < kInputMouseButtonCount; ++b) {
		const int button = static_cast<int>(b);
		if (IsMouseButtonDown(button)) {
			f.mouse_down.set(b);
		}
		if (IsMouseButtonPressed(button)) {
			f.mouse_pressed.set(b);
		}
	}
	const Vector2 md = GetMouseDelta();
	f.mouse_dx = static_cast<double>(md.x);
	f.mouse_dy = static_cast<double>(md.y);
	f.key_pressed_first = GetKeyPressed();
	// Drain the rest of raylib's key queue so it can't grow across frames.
	while (GetKeyPressed() != 0) {
	}
	return f;
}

void SyntheticInput::hold_key(int key, int frames) {
	if (frames > 0) {
		keys_.push_back({ key, frames, true });
		if (pending_first_key_ == 0) {
			pending_first_key_ = key;
		}
	}
}

void SyntheticInput::hold_mouse_button(int button, int frames) {
	if (frames > 0) {
		buttons_.push_back({ button, frames, true });
	}
}

void SyntheticInput::look_delta(double dx, double dy) {
	pending_dx_ += dx;
	pending_dy_ += dy;
}

InputFrame SyntheticInput::poll() {
	InputFrame f;
	const auto in_range = [](int v, std::size_t n) {
		return v >= 0 && static_cast<std::size_t>(v) < n;
	};
	for (Hold &h : keys_) {
		if (in_range(h.code, kInputKeyCount)) {
			f.keys_down.set(static_cast<std::size_t>(h.code));
			if (h.fresh) {
				f.keys_pressed.set(static_cast<std::size_t>(h.code));
			}
		}
		h.fresh = false;
		--h.frames_left;
	}
	for (Hold &h : buttons_) {
		if (in_range(h.code, kInputMouseButtonCount)) {
			f.mouse_down.set(static_cast<std::size_t>(h.code));
			if (h.fresh) {
				f.mouse_pressed.set(static_cast<std::size_t>(h.code));
			}
		}
		h.fresh = false;
		--h.frames_left;
	}
	const auto expired = [](const Hold &h) { return h.frames_left <= 0; };
	keys_.erase(std::remove_if(keys_.begin(), keys_.end(), expired), keys_.end());
	buttons_.erase(std::remove_if(buttons_.begin(), buttons_.end(), expired), buttons_.end());
	f.mouse_dx = pending_dx_;
	f.mouse_dy = pending_dy_;
	f.key_pressed_first = pending_first_key_;
	pending_dx_ = pending_dy_ = 0.0;
	pending_first_key_ = 0;
	return f;
}

MovementBindings::MovementBindings() :
		forward(KEY_W), back(KEY_S), left(KEY_A), right(KEY_D), jump(KEY_SPACE),
		sprint(KEY_LEFT_SHIFT) {}

namespace {

// Phase 6.17: physical-key-to-action mapping for the axes/buttons the engine
// itself always understands (InputCmd::move/buttons -- distinct from
// vb.register_keybind's pack-defined slots, Phase 6.3, which cover only
// discrete named actions a pack invents). These used to be raylib key
// literals mixed directly into sample_input_cmd's branching with no seam at
// all; pulling them into one small table is the actual "decouple hardcoded
// movement" -- sample_input_cmd itself no longer hardcodes which physical
// key means what, and a future client settings screen (Phase 5.3, still not
// attempted) has exactly one place to rebind. Note this is a *client-local*
// physical-key mapping, not a network-visible one -- what the resulting
// InputCmd.move/buttons/yaw/pitch actually *do* is already fully
// pack-overridable server-side via vb.on("player_input", ...), independent
// of which key produced them.

// Phase 6.19: the engine pre-registers 8 action names ("move_forward",
// "move_back", "move_left", "move_right", "jump", "sprint", "primary",
// "secondary") into the same Phase 6.3 keybind registry every pack-custom
// vb.register_keybind() name goes into (see PackRuntime::Impl::Impl in
// src/script/pack_runtime.cpp) -- so they're enumerable via
// S2C_KeybindRegistry like any other keybind, and a pack's
// vb.on("player_input", ...) can read e.g. input.keybinds["move_forward"]
// the same way it reads a custom one. This is purely additive:
// InputCmd::move/buttons (and MovementBindings' physical keys) are
// unchanged, so physics/movement code isn't affected. Lookup is by name in
// whatever S2C_KeybindRegistry the server actually sent, never assumed to
// be bits 0-7.
void set_engine_keybind(vb::protocol::InputCmd &cmd, const char *name,
		bool held, const std::vector<std::string> &keybind_names) {
	for (std::size_t i = 0; i < keybind_names.size(); ++i) {
		if (keybind_names[i] == name) {
			if (held) {
				cmd.keybinds |= (1u << i);
			}
			return;
		}
	}
}

// Phase 7.4: closes the gap `content/base/ui/pause.lua`/`ui/inventory.lua`'s
// own header comments flagged ("nothing opens this yet") and
// `content/examples/kitchen_sink/keybinds.lua` hit for its own custom
// screen ("no base-pack/client UI wires these yet") -- Phase 6.3's
// vb.register_keybind gives a pack a *named* bit in InputCmd.keybinds, but
// nothing on the client ever mapped a physical key to a pack-registered
// custom name (only the 8 pre-registered engine names above get one, via
// MovementBindings). This is a minimal hardcoded default table, not a real
// settings-screen UI (Phase 5.3's keybindings screen only covers the 6
// MovementBindings axes) -- a future rebind screen for these is a separate
// step past this one, same as that item's own scope note. Unlike the
// engine-name lookup above, these are read unconditionally (not gated on
// mouse_captured below): opening a pause/inventory screen must work whether
// or not the mouse is currently captured for looking around.
struct CustomKeybind {
	const char *name;
	int key;
};
constexpr CustomKeybind kCustomKeybinds[] = {
	{ "base:pause", KEY_ESCAPE },
	{ "base:inventory", KEY_E },
};

} // namespace

vb::protocol::InputCmd sample_input_cmd(const InputFrame &in, std::uint32_t seq,
		double dt, double yaw, double pitch, bool mouse_captured,
		const MovementBindings &bindings,
		const std::vector<std::string> &keybind_names,
		std::uint8_t selected_slot) {
	vb::protocol::InputCmd cmd;
	cmd.seq = seq;
	cmd.dt = static_cast<float>(dt);
	cmd.yaw = static_cast<float>(yaw);
	cmd.pitch = static_cast<float>(pitch);
	cmd.selected_slot = selected_slot;
	if (mouse_captured) {
		const bool forward = in.key_down(bindings.forward);
		const bool back = in.key_down(bindings.back);
		const bool right = in.key_down(bindings.right);
		const bool left = in.key_down(bindings.left);
		const bool jump = in.key_down(bindings.jump);
		const bool sprint = in.key_down(bindings.sprint);
		// Phase 6.17: block breaking/placing is no longer an engine default
		// (see the removed hold-to-break timer further below in this file) --
		// the client's only job is to report these as raw held-button state,
		// exactly like jump/sprint above. Whether holding "primary" over a
		// voxel does anything at all is entirely up to a content pack's
		// vb.on("player_input", ...) handler (content/base/mechanics.lua).
		const bool primary = in.mouse_button_down(MOUSE_BUTTON_LEFT);
		const bool secondary = in.mouse_button_down(MOUSE_BUTTON_RIGHT);

		if (forward) {
			cmd.move.z += 1.0f;
		}
		if (back) {
			cmd.move.z -= 1.0f;
		}
		if (right) {
			cmd.move.x += 1.0f;
		}
		if (left) {
			cmd.move.x -= 1.0f;
		}
		if (jump) {
			cmd.buttons |= vb::protocol::kInputJump;
		}
		if (sprint) {
			cmd.buttons |= vb::protocol::kInputSprint;
		}
		if (primary) {
			cmd.buttons |= vb::protocol::kInputPrimary;
		}
		if (secondary) {
			cmd.buttons |= vb::protocol::kInputSecondary;
		}

		set_engine_keybind(cmd, "move_forward", forward, keybind_names);
		set_engine_keybind(cmd, "move_back", back, keybind_names);
		set_engine_keybind(cmd, "move_left", left, keybind_names);
		set_engine_keybind(cmd, "move_right", right, keybind_names);
		set_engine_keybind(cmd, "jump", jump, keybind_names);
		set_engine_keybind(cmd, "sprint", sprint, keybind_names);
		set_engine_keybind(cmd, "primary", primary, keybind_names);
		set_engine_keybind(cmd, "secondary", secondary, keybind_names);
	}
	for (const CustomKeybind &kb : kCustomKeybinds) {
		set_engine_keybind(cmd, kb.name, in.key_down(kb.key), keybind_names);
	}
	return cmd;
}

} // namespace vb::render
