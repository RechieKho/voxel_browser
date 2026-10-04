// Records a play session as a vbtest script (docs/e2e-automation.md §8, phase E6):
//   voxel_browser --automation-record session.py
// It watches the same input and the same game state the player produces and writes what
// they *meant*, not raw input: a left click while the crosshair is on a block is
// `break_block((x, y, z))`, walking becomes a `walk_to` to where the player stopped, a chat
// line is `chat("...")`, a pack-UI button is `ui("id").click()`. After actions whose outcome it
// can see (a block broke, a block appeared, chat arrived) it adds the matching `expect(...)`.
// Development builds only (VB_WITH_AUTOMATION).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "client_app.hpp"
#include "vb/render/input.hpp"

namespace vb::client {

class Recorder final : public ClientApp::Observer {
public:
	explicit Recorder(std::string path) :
			path_(std::move(path)) {}

	// Call once per frame, after ClientApp::frame(), with the InputFrame it was given.
	void observe_frame(const ClientApp &app, const vb::render::InputFrame &input);

	// Writes the script (also done after every recorded step, so a crash loses nothing).
	bool write() const;
	const std::vector<std::string> &steps() const { return steps_; }

	// ClientApp::Observer
	void on_chat_sent(std::string_view text) override;
	void on_ui_click(const std::string &id, bool hud) override;
	void on_ui_change(const std::string &id, const std::string &text, bool hud) override;
	void on_ui_list(const std::string &id, int index, bool hud) override;
	void on_menu_connect(const std::string &host, int port, const std::string &name) override;
	void on_menu_singleplayer(const std::string &name) override;

private:
	struct Pending { // an action whose result we are waiting to see
		vb::core::IVec3 pos;
		std::string before;
		long long since;
	};

	std::string var() const; // the Python variable the script names the player
	void add(std::string step); // flushes a pending walk first, keeps order
	void flush_walk();
	void check_pending(const ClientApp &app);
	static std::string quote(const std::string &s);

	std::string path_;
	std::string player_name_ = "Player";
	std::vector<std::string> steps_;
	long long frame_ = 0;

	// walking
	bool moving_ = false;
	long long last_move_frame_ = 0;
	bool have_start_ = false;
	vb::core::Vec3d walked_from_{};
	vb::core::Vec3d feet_{};

	// breaking: one multi-hit block is a series of clicks, recorded once
	bool have_last_break_ = false;
	vb::core::IVec3 last_break_pos_{};
	long long last_break_frame_ = 0;

	// the modal screen currently open (a pack UI), so opening/closing it can be asserted
	std::string open_ui_;

	std::vector<Pending> pending_breaks_;
	std::vector<Pending> pending_places_;
	const ClientApp *app_ = nullptr;
};

} // namespace vb::client
