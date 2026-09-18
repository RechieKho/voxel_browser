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
	ui.set_break_progress(0.5f);
	ui.set_screen_size(1280, 720);
	CHECK(ui.render_hud().empty());
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

TEST_CASE("ui.define_hud + render_hud() composes a progress bar entirely in "
		"Lua from the raw kRect primitive + client.break_progress()/"
		"screen_size() (Phase 6.16)") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		ui.define_hud(function(state)
			local widgets = {}
			local progress = client.break_progress()
			if progress then
				local screen = client.screen_size()
				local bar_w = 120
				local x = (screen.width - bar_w) / 2
				local y = screen.height / 2 + 24
				table.insert(widgets, {
					id = "bg", type = "rect", x = x, y = y, w = bar_w, h = 10,
					color = { 30, 30, 34, 200 }, border = { 90, 90, 100, 230 },
				})
				table.insert(widgets, {
					id = "fill", type = "rect", x = x, y = y,
					w = bar_w * progress, h = 10,
					color = { 220, 220, 220, 230 },
				})
			end
			return { widgets = widgets }
		end)
	)"));

	// Nothing breaking yet: the hud renders no widgets at all.
	ui.set_screen_size(800, 600);
	CHECK(ui.render_hud().empty());

	ui.set_break_progress(0.4f);
	const auto &widgets = ui.render_hud();
	REQUIRE(widgets.size() == 2);
	CHECK(widgets[0].id == "bg");
	CHECK(widgets[0].type == WidgetType::kRect);
	CHECK(widgets[0].x == doctest::Approx((800 - 120) / 2.0f));
	CHECK(widgets[0].y == doctest::Approx(600 / 2.0f + 24));
	CHECK(widgets[0].fill_r == 30);
	CHECK(widgets[0].border_a == 230);
	CHECK(widgets[1].id == "fill");
	CHECK(widgets[1].w == doctest::Approx(120 * 0.4f));
	CHECK(widgets[1].border_a == 0); // no `border` table given -> no outline

	// Clearing progress (nullopt) hides it again, without re-registering.
	ui.set_break_progress(std::nullopt);
	CHECK(ui.render_hud().empty());
}

TEST_CASE("render_hud() is a safe no-op when no HUD was ever registered "
		"(Phase 6.16)") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file("-- no ui.define_hud call"));
	ui.set_break_progress(0.9f);
	CHECK(ui.render_hud().empty());
}

TEST_CASE("the HUD's state table persists across render_hud() calls, "
		"independent of any modal ui.define screen (Phase 6.16)") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		ui.define_hud(function(state)
			state.frames = (state.frames or 0) + 1
			return { widgets = { { id = "n", type = "label", x=0,y=0,w=1,h=1,
				text = tostring(state.frames) } } }
		end)
		ui.define("modal", function(state)
			return { widgets = {} }
		end)
	)"));

	CHECK(ui.render_hud()[0].text == "1");
	CHECK(ui.render_hud()[0].text == "2");

	// A modal screen opening/closing alongside it doesn't reset hud state.
	ui.open("modal", "{}");
	ui.render_frame();
	ui.close();
	CHECK(ui.render_hud()[0].text == "3");
}

#endif // VB_WITH_LUA
