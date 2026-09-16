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
}

#else

using vb::script::UiRuntime;
using vb::script::WidgetType;

TEST_CASE("ui.define + open() produces the expected widget list from a ctx table") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		ui.define("greet", function(ctx)
			return {
				widgets = {
					{ id = "hello", type = "label", x = 1, y = 2, w = 30, h = 10,
					  text = "hi " .. ctx.name },
					{ id = "go", type = "button", x = 1, y = 20, w = 30, h = 10,
					  text = "Go" },
				}
			}
		end)
	)"));

	ui.open("greet", R"({"name":"Ada"})");
	REQUIRE(ui.is_open());
	CHECK(ui.current_name() == "greet");
	REQUIRE(ui.widgets().size() == 2);
	CHECK(ui.widgets()[0].id == "hello");
	CHECK(ui.widgets()[0].type == WidgetType::kLabel);
	CHECK(ui.widgets()[0].text == "hi Ada");
	CHECK(ui.widgets()[1].type == WidgetType::kButton);
}

TEST_CASE("open() on an undefined name logs and stays closed") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file("-- no ui.define calls"));
	ui.open("nope", "{}");
	CHECK_FALSE(ui.is_open());
	CHECK(ui.widgets().empty());
}

TEST_CASE("report_click invokes the widget's on_click callback") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		clicked = false
		ui.define("test", function(ctx)
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
	ui.report_click("btn");
	// send_event()/ui.close() on an unattached runtime (no attach_session
	// call) must be a safe no-op -- verified implicitly by not crashing.
	CHECK(ui.load_pack_file("assert(clicked == true)"));
}

TEST_CASE("close() invokes on_close exactly once and clears state") {
	UiRuntime ui;
	REQUIRE(ui.load_pack_file(R"(
		close_count = 0
		ui.define("test", function(ctx)
			return {
				on_close = function() close_count = close_count + 1 end,
				widgets = {},
			}
		end)
	)"));
	ui.open("test", "{}");
	REQUIRE(ui.is_open());
	ui.close();
	CHECK_FALSE(ui.is_open());
	CHECK(ui.widgets().empty());
	CHECK(ui.load_pack_file("assert(close_count == 1)"));

	// Closing again (already closed) must not re-invoke on_close.
	ui.close();
	CHECK(ui.load_pack_file("assert(close_count == 1)"));
}

#endif // VB_WITH_LUA
