#include <doctest/doctest.h>

#include <ostream>

#include "vb/script/ui_runtime.hpp"

#if !VB_WITH_LUA

TEST_CASE("UiRuntime reports kDisabled when built without VB_WITH_LUA") {
	vb::script::UiRuntime ui;
	const auto r = ui.load_pack_file("return 1");
	CHECK_FALSE(r);
	CHECK(r.error == vb::core::ScriptError::kDisabled);
	CHECK_FALSE(ui.is_open());
	CHECK(ui.render_frame().empty());
}

#else

using vb::script::UiRuntime;
using vb::script::WidgetType;

TEST_CASE("ui.define + open() + render_frame() produces the expected widget list") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		ui.define("greet", function(state)
			return {
				widgets = {
					{ id = "hello", type = "label", x = 1, y = 2, w = 30, h = 10,
					  text = "hi " .. state.name },
					{ id = "go", type = "button", x = 1, y = 20, w = 30, h = 10,
					  text = "Go" },
				}
			}
		end)
	)"));

	ui.open("greet", R"({"name":"Ada"})");
	REQUIRE(ui.is_open());
	CHECK(ui.current_name() == "greet");
	// Nothing is evaluated until the first render_frame() call.
	CHECK(ui.widgets().empty());

	const auto &widgets = ui.render_frame();
	REQUIRE(widgets.size() == 2);
	CHECK(widgets[0].id == "hello");
	CHECK(widgets[0].type == WidgetType::kLabel);
	CHECK(widgets[0].text == "hi Ada");
	CHECK(widgets[1].type == WidgetType::kButton);
	CHECK(&ui.widgets() == &widgets);
}

TEST_CASE("render_frame() re-evaluates every call, reflecting state mutated by a click handler") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		ui.define("counter", function(state)
			state.count = state.count or 0
			return {
				widgets = {
					{ id = "label", type = "label", x=0,y=0,w=1,h=1,
					  text = "count " .. state.count },
					{ id = "btn", type = "button", x=0,y=0,w=1,h=1, text = "+",
					  on_click = function() state.count = state.count + 1 end },
				}
			}
		end)
	)"));

	ui.open("counter", "{}");
	REQUIRE(ui.is_open());

	auto widgets = ui.render_frame();
	REQUIRE(widgets.size() == 2);
	CHECK(widgets[0].text == "count 0");

	// The click handler mutates `state`, which is the same table passed to
	// render_fn every frame -- the next render_frame() call must reflect it
	// without closing/reopening the screen.
	ui.report_click("btn");
	widgets = ui.render_frame();
	REQUIRE(widgets.size() == 2);
	CHECK(widgets[0].text == "count 1");
}

TEST_CASE("open() on an undefined name stays closed; render_frame() is a safe no-op") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file("-- no ui.define calls"));
	ui.open("nope", "{}");
	CHECK_FALSE(ui.is_open());
	CHECK(ui.render_frame().empty());
	CHECK(ui.widgets().empty());
}

TEST_CASE("report_click invokes the widget's on_click callback from the latest frame") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		clicked = false
		ui.define("test", function(state)
			return {
				widgets = {
					{ id = "btn", type = "button", x=0,y=0,w=1,h=1, text = "x",
					  on_click = function() clicked = true end },
				}
			}
		end)
	)"));
	ui.open("test", "{}");
	REQUIRE(ui.is_open());
	ui.render_frame();
	ui.report_click("btn");
	// send_event()/ui.close() on an unattached runtime (no attach_session
	// call) must be a safe no-op -- verified implicitly by not crashing.
	CHECK(ui.load_pack_file("assert(clicked == true)"));
}

TEST_CASE("close() invokes on_close (from the latest frame) exactly once and clears state") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		close_count = 0
		ui.define("test", function(state)
			return {
				on_close = function() close_count = close_count + 1 end,
				widgets = {},
			}
		end)
	)"));
	ui.open("test", "{}");
	REQUIRE(ui.is_open());
	ui.render_frame();
	ui.close();
	CHECK_FALSE(ui.is_open());
	CHECK(ui.widgets().empty());
	CHECK(ui.load_pack_file("assert(close_count == 1)"));

	// Closing again (already closed) must not re-invoke on_close.
	ui.close();
	CHECK(ui.load_pack_file("assert(close_count == 1)"));
}

#endif // VB_WITH_LUA
