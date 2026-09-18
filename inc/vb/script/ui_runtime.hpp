#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vb/net/session.hpp"
#include "vb/script/vm.hpp"

// Client-side UI VM (spec §10.4, Phase 6.2): a second, separate Lua VM from
// the server's PackRuntime. A pack declares screens via `ui.define(name,
// render_fn)`; `render_fn(state)` is called once per UI frame (raygui is
// itself immediate-mode, so no virtual-DOM diffing is needed) and its
// returned widget list is exposed as plain data (Widget, no sol2) for
// vb::render::UiRenderer to draw with raygui. `state` is the same Lua table
// across every frame a screen stays open (seeded once from open()'s
// ctx_json), so a widget callback mutating `state` is naturally visible on
// the next render_frame() call -- that's the entire reactivity mechanism.
// Widget on_click/on_change/on_close callbacks may call
// ui.send_event(...)/ui.close(), which route straight to the attached
// ClientSession as a C2S_UiEvent -- game-meaningful logic stays
// server-authoritative; purely cosmetic state can stay Lua-local on `state`.

namespace vb::script {

enum class WidgetType : std::uint8_t {
	kLabel,
	kPanel,
	kButton,
	kTextBox,
	kList,
	// A raw filled rectangle, optionally outlined -- deliberately the only
	// *non*-interactive primitive here with no baked-in meaning (no "this is
	// a progress bar/health bar/divider" concept engine-side). Composing a
	// progress bar, a health bar, a divider line, or anything else purely
	// visual out of one or more of these is entirely a Lua/content concern
	// (Phase 6.16) -- the engine only ever draws what it's told: a filled
	// box at (x, y, w, h) in `fill_*`, plus a 1px outline in `border_*` when
	// `border_a > 0`.
	kRect,
};

struct Widget {
	std::string id;
	WidgetType type = WidgetType::kLabel;
	float x = 0, y = 0, w = 0, h = 0;
	std::string text; // label/panel/button text; textbox initial value
	std::vector<std::string> items; // list only
	int list_index = -1; // list only
	std::uint8_t fill_r = 255, fill_g = 255, fill_b = 255, fill_a = 255; // kRect only
	std::uint8_t border_r = 0, border_g = 0, border_b = 0, border_a = 0; // kRect only; alpha 0 = no border
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

	// Looks up `name` (registered via ui.define) and initializes `state`
	// from `ctx_json` (parsed JSON -> Lua table); that same table is passed
	// to `render_fn` on every subsequent render_frame() call for as long as
	// this screen stays open. Logs and leaves the runtime closed if `name`
	// was never defined. Does not evaluate `render_fn` itself -- call
	// render_frame() to produce the first frame's widgets.
	void open(std::string_view name, std::string_view ctx_json);

	// Sends exactly one "close" C2S_UiEvent (spec: on_close always reaches
	// the server), then runs the pack's own on_close callback if present
	// (local cosmetic cleanup only), then clears state. No-op if not open.
	void close();

	bool is_open() const;
	const std::string &current_name() const;

	// Evaluates render_fn(state) for the current frame (no-op, returning
	// the last -- likely empty -- list if no screen is open) and returns
	// the fresh widget list. Call once per UI frame, right before handing
	// the result to vb::render::UiRenderer::draw.
	const std::vector<Widget> &render_frame();

	// The widget list from the most recently completed render_frame() call.
	const std::vector<Widget> &widgets() const;

	// Called by vb::render::UiRenderer once per frame per widget that
	// reported an interaction this frame. Invokes that widget's Lua
	// on_click/on_change callback if present; a callback error is caught
	// and logged, never propagated.
	void report_click(const std::string &widget_id);
	void report_change(const std::string &widget_id, std::string_view text_value);
	void report_list_change(const std::string &widget_id, int new_index);

	// --- HUD: an always-on overlay independent of open()/close() ---------
	//
	// "Engine provides raw state, Lua handles presentation": the engine
	// computes things like block-break progress (input timing, reach,
	// target tracking) since that's gameplay logic, but never draws a pixel
	// of it -- a pack's `ui.define_hud(render_fn)` (registered like
	// `ui.define`, but always active, not tied to any open()/close() name)
	// decides whether/how to show it. `render_fn(state)` is called every UI
	// frame regardless of whether a modal screen (open()/close() above) is
	// also open, with its own persistent `state` table (no ctx_json --
	// there's no "opening" a HUD). Read the raw values a HUD might want via
	// e.g. `client.break_progress()`/`client.screen_size()` below. Display-
	// only for now: hud widgets aren't wired to report_click/report_change
	// (no interactive HUD element exists yet).

	// Sets the raw local state a HUD's render_fn can read back via
	// `client.break_progress()` -- nullopt when not currently breaking
	// anything. Call once per frame from the input-handling code that
	// already computes this (src/client/main.cpp), before render_hud().
	void set_break_progress(std::optional<float> fraction);

	// Sets the window size a HUD's render_fn can read back via
	// `client.screen_size()` (`{width=.., height=..}`) -- widgets take
	// absolute pixel positions (same as modal screens), so a HUD wanting to
	// center something needs this raw value rather than a hardcoded guess.
	void set_screen_size(int width, int height);

	// Evaluates the registered HUD render_fn (no-op, returning the last --
	// likely empty -- list if none was ever registered via
	// ui.define_hud) and returns the fresh widget list. Call once per UI
	// frame, unconditionally (unlike render_frame(), not gated on is_open()).
	const std::vector<Widget> &render_hud();

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace vb::script
