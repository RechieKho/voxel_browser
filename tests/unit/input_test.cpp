#include <doctest/doctest.h>

#include <raylib.h>

#include "vb/render/input.hpp"

using namespace vb::render;

TEST_CASE("SyntheticInput holds a key for N frames, press fires once") {
	SyntheticInput in;
	in.hold_key(KEY_W, 3);
	InputFrame f = in.poll();
	CHECK(f.key_down(KEY_W));
	CHECK(f.key_pressed(KEY_W));
	CHECK(f.key_pressed_first == KEY_W);
	f = in.poll();
	CHECK(f.key_down(KEY_W));
	CHECK_FALSE(f.key_pressed(KEY_W));
	f = in.poll();
	CHECK(f.key_down(KEY_W));
	f = in.poll();
	CHECK_FALSE(f.key_down(KEY_W));
}

TEST_CASE("SyntheticInput mouse delta is delivered once") {
	SyntheticInput in;
	in.look_delta(3.0, -2.0);
	in.look_delta(1.0, 0.0);
	InputFrame f = in.poll();
	CHECK(f.mouse_dx == doctest::Approx(4.0));
	CHECK(f.mouse_dy == doctest::Approx(-2.0));
	f = in.poll();
	CHECK(f.mouse_dx == doctest::Approx(0.0));
}

TEST_CASE("sample_input_cmd: movement only while mouse is captured") {
	MovementBindings b;
	InputFrame f;
	f.keys_down.set(KEY_W);
	f.keys_down.set(KEY_D);
	f.keys_down.set(KEY_SPACE);
	f.mouse_down.set(MOUSE_BUTTON_LEFT);

	auto cmd = sample_input_cmd(f, 7, 0.016, 10.0, -5.0, true, b);
	CHECK(cmd.seq == 7);
	CHECK(cmd.move.z == doctest::Approx(1.0));
	CHECK(cmd.move.x == doctest::Approx(1.0));
	CHECK((cmd.buttons & vb::protocol::kInputJump) != 0);
	CHECK((cmd.buttons & vb::protocol::kInputPrimary) != 0);
	CHECK((cmd.buttons & vb::protocol::kInputSecondary) == 0);

	cmd = sample_input_cmd(f, 8, 0.016, 0.0, 0.0, false, b);
	CHECK(cmd.move.z == doctest::Approx(0.0));
	CHECK(cmd.buttons == 0);
}

TEST_CASE("sample_input_cmd: opposing keys cancel; engine keybind bits by name") {
	MovementBindings b;
	InputFrame f;
	f.keys_down.set(KEY_W);
	f.keys_down.set(KEY_S);
	const std::vector<std::string> names = { "jump", "move_forward" };
	const auto cmd = sample_input_cmd(f, 1, 0.016, 0, 0, true, b, names);
	CHECK(cmd.move.z == doctest::Approx(0.0));
	CHECK((cmd.keybinds & 0b10u) != 0); // move_forward held
	CHECK((cmd.keybinds & 0b01u) == 0); // jump not held
}

TEST_CASE("sample_input_cmd: custom keybinds ignore mouse capture") {
	MovementBindings b;
	InputFrame f;
	f.keys_down.set(KEY_E);
	const std::vector<std::string> names = { "base:inventory" };
	const auto cmd = sample_input_cmd(f, 1, 0.016, 0, 0, false, b, names);
	CHECK((cmd.keybinds & 1u) != 0);
}
