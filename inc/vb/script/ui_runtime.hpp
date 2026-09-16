#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vb/net/session.hpp"
#include "vb/script/vm.hpp"

// Client-side UI VM (spec §10.4, Phase 4.5): a second, separate Lua VM from
// the server's PackRuntime. A pack declares screens via `ui.define(name,
// layout_fn)`; the layout function returns a widget list this class exposes
// as plain data (Widget, no sol2) for vb::render::UiRenderer to draw with
// raygui. Widget on_click/on_change/on_close callbacks may call
// ui.send_event(...)/ui.close(), which route straight to the attached
// ClientSession as a C2S_UiEvent -- game-meaningful logic stays
// server-authoritative; purely cosmetic state can stay Lua-local.

namespace vb::script {

enum class WidgetType : std::uint8_t {
	kLabel,
	kPanel,
	kButton,
	kTextBox,
	kList,
};

struct Widget {
	std::string id;
	WidgetType type = WidgetType::kLabel;
	float x = 0, y = 0, w = 0, h = 0;
	std::string text; // label/panel/button text; textbox initial value
	std::vector<std::string> items; // list only
	int list_index = -1; // list only
};

class UiRuntime {
public:
	explicit UiRuntime(VmLimits limits = {});
	~UiRuntime();
	UiRuntime(UiRuntime &&) noexcept;
	UiRuntime &operator=(UiRuntime &&) noexcept;
	UiRuntime(const UiRuntime &) = delete;
	UiRuntime &operator=(const UiRuntime &) = delete;

	// Runs one chunk of UI Lua source (registration only: ui.define calls).
	ScriptResult load_pack_file(std::string_view code,
			std::string_view chunk_name = "ui_pack");

	// Outgoing C2S_UiEvent frames (ui.send_event / ui.close) go through this
	// session once attached; unattached, they're silently dropped.
	void attach_session(net::ClientSession &session);

	// Looks up `name` (registered via ui.define) and evaluates its layout
	// function once against `ctx_json` (parsed JSON -> Lua table). Logs and
	// leaves the runtime closed if `name` was never defined. Widgets are
	// computed once here and do not re-layout afterward -- a callback that
	// wants a different screen should close() and have the server
	// open_ui() again.
	void open(std::string_view name, std::string_view ctx_json);

	// Sends exactly one "close" C2S_UiEvent (spec: on_close always reaches
	// the server), then runs the pack's own on_close callback if present
	// (local cosmetic cleanup only), then clears state. No-op if not open.
	void close();

	bool is_open() const;
	const std::string &current_name() const;
	const std::vector<Widget> &widgets() const;

	// Called by vb::render::UiRenderer once per frame per widget that
	// reported an interaction this frame. Invokes that widget's Lua
	// on_click/on_change callback if present; a callback error is caught
	// and logged, never propagated.
	void report_click(const std::string &widget_id);
	void report_change(const std::string &widget_id, std::string_view text_value);
	void report_list_change(const std::string &widget_id, int new_index);

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace vb::script
