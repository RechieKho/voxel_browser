#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vb/core/config.hpp"

// Engine-level main menu / connect / settings / error screens (spec §5.3),
// drawn with raygui. Not pack content -- no Lua involved here, unlike
// vb::script::UiRuntime (Phase 4.5) which is what a *pack* uses to draw its
// own screens once joined. src/client/main.cpp owns the actual app-state
// machine (menu -> connecting -> playing / error) and which struct
// (Singleplayer / RemoteConnection) is alive; MainMenu only draws the
// non-gameplay screens and reports back which button fired this frame.

namespace vb::render {

class MainMenu {
public:
	explicit MainMenu(const core::ClientConfig &config);

	// Pre-fills the connect fields (e.g. from CLI overrides) before the first
	// draw_main() call.
	void prefill(std::string_view address, int port, std::string_view player_name);

	// --- main screen ---------------------------------------------------
	struct MainResult {
		bool connect = false;
		bool singleplayer = false;
		bool open_settings = false;
		bool quit = false;
	};
	// `recent_servers` is "host:port" strings, most-recent first; clicking one
	// fills the address/port fields.
	MainResult draw_main(const std::vector<std::string> &recent_servers);

	const std::string &address() const { return address_; }
	int port() const { return port_; }
	const std::string &player_name() const { return player_name_; }

	// --- settings screen -------------------------------------------------
	struct SettingsResult {
		bool save = false;
		bool back = false;
		bool open_keybindings = false;
	};
	// `config` is read on entry (call once when opening the screen) and, on
	// `save`, filled in with the edited values for the caller to persist.
	void open_settings(const core::ClientConfig &config);
	SettingsResult draw_settings(core::ClientConfig &config);

	// --- keybindings screen (5.3) ----------------------------------------
	struct KeybindingsResult {
		bool save = false;
		bool back = false;
	};
	// Reached from Settings. `config` is read on entry the same way
	// open_settings() is; on `save`, the 6 MovementBindings-equivalent
	// key_* fields are filled in with the edited values for the caller to
	// persist. Rebinding is "click the action's key button, then press any
	// physical key" -- Esc cancels a rebind in progress without changing it.
	void open_keybindings(const core::ClientConfig &config);
	KeybindingsResult draw_keybindings(core::ClientConfig &config);

	// --- connecting screen -------------------------------------------------
	struct ConnectingResult {
		bool cancel = false;
	};
	ConnectingResult draw_connecting(std::string_view status_text);

	// --- error screen --------------------------------------------------------
	struct ErrorResult {
		bool back = false;
	};
	ErrorResult draw_error(std::string_view reason);

	// --- loading screen (Phase 7.1) ---------------------------------------
	// The window between "joined" and "first playable frame" -- initial chunk
	// streaming. `fraction` is [0,1] (clamp before calling); `operator_title`
	// is stage 2's optional operator branding (server.toml's `motd`, once the
	// handshake has actually delivered it -- empty until then, drawn under
	// the generic bar rather than blocking it). Deliberately no Lua/pack
	// involvement and no cancel button -- this screen's only job is getting
	// out of the way quickly, not hosting interaction.
	void draw_loading(float fraction, std::string_view operator_title);

private:
	std::string address_;
	std::string port_text_;
	int port_ = 27015;
	std::string player_name_;
	bool address_edit_ = false;
	bool port_edit_ = false;
	bool name_edit_ = false;
	int recent_scroll_ = 0;
	int recent_active_ = -1;

	// Settings screen staging values (raygui widgets bind to these directly).
	int s_width_ = 1280;
	int s_height_ = 720;
	bool s_vsync_ = true;
	float s_fov_ = 70.0f;
	float s_render_distance_ = 8.0f;
	float s_sensitivity_ = 0.12f;
	int s_cache_mb_ = 512;
	bool s_width_edit_ = false;
	bool s_height_edit_ = false;
	bool s_cache_edit_ = false;

	// Keybindings screen staging values, in the same order the screen draws
	// them (forward/back/left/right/jump/sprint).
	int k_keys_[6] = { 87, 83, 65, 68, 32, 340 };
	// Index into k_keys_ currently waiting for a key press, or -1 if none.
	int k_rebinding_ = -1;
};

} // namespace vb::render
