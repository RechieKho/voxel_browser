#include "recorder.hpp"

#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>

#include <raylib.h>
#include <nlohmann/json.hpp>

#include "vb/core/math.hpp"
#include "vb/world/raycast.hpp"

namespace vb::client {

namespace {

constexpr double kReach = 5.0; // the distance the client's own targeting uses
constexpr long long kWalkIdleFrames = 15; // a quarter second without a movement key = stopped
constexpr long long kSameBlockFrames = 90; // clicks on one block this close together are one break
constexpr long long kResultFrames = 900; // how long to wait for an action's result (15 s at 60 fps; a sanitized server needs seconds)

std::string fmt1(double v) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.1f", v);
	return buf;
}

std::string tuple3(const vb::core::IVec3 &p) {
	return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z) + ")";
}

std::optional<std::string> block_name(const vb::net::ClientSession &c, const vb::core::IVec3 &p) {
	const auto &store = c.chunk_store();
	if (!store.has(vb::core::chunk_of(p))) {
		return std::nullopt;
	}
	const vb::core::BlockId id = store.block_at(p);
	return store.registry().contains(id) ? std::optional<std::string>(store.registry().get(id).name)
										 : std::nullopt;
}

} // namespace

std::string Recorder::quote(const std::string &s) {
	// JSON string syntax is valid Python for everything we emit (all non-ASCII is \u-escaped).
	return nlohmann::json(s).dump();
}

std::string Recorder::var() const {
	std::string v;
	for (char c : player_name_) {
		v += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
	}
	if (v.empty() || std::isdigit(static_cast<unsigned char>(v[0]))) {
		v = "p_" + v;
	}
	return v;
}

void Recorder::flush_walk() {
	if (!moving_) {
		return;
	}
	moving_ = false;
	if (!have_start_) {
		return;
	}
	const double dx = feet_.x - walked_from_.x, dz = feet_.z - walked_from_.z;
	if (std::hypot(dx, dz) < 0.5) {
		return; // shuffled in place
	}
	steps_.push_back(var() + ".walk_to((" + fmt1(feet_.x) + ", None, " + fmt1(feet_.z) + "), tolerance=0.7)");
	walked_from_ = feet_;
}

void Recorder::add(std::string step) {
	flush_walk();
	steps_.push_back(std::move(step));
	write();
}

void Recorder::observe_frame(const ClientApp &app, const vb::render::InputFrame &in) {
	app_ = &app;
	++frame_;
	if (player_name_ != app.player_name()) {
		player_name_ = app.player_name();
		write(); // the header names the player: keep the file current even before any step
	}
	const vb::net::ClientSession *s = app.session();
	if (s == nullptr || !s->joined() || app.app_state() != AppState::kPlaying) {
		return;
	}
	feet_ = s->predicted_feet();
	if (!have_start_) {
		walked_from_ = feet_;
		have_start_ = true;
	}
	check_pending(app);

	// A pack screen opening or closing is a result worth asserting (and what a replay must wait
	// for before it clicks anything on it).
	const std::string ui_now = app.ui().is_open() ? app.ui().current_name() : std::string();
	if (ui_now != open_ui_) {
		if (!open_ui_.empty()) {
			add("expect(" + var() + ").not_.to_have_ui_open(" + quote(open_ui_) + ")");
		}
		if (!ui_now.empty()) {
			add("expect(" + var() + ").to_have_ui_open(" + quote(ui_now) + ")");
		}
		open_ui_ = ui_now;
	}

	// Keybinds that open pack screens (kCustomKeybinds in the input layer): not while typing.
	if (!app.chat_box_open()) {
		if (in.key_pressed(KEY_E)) {
			add(var() + ".key_press(\"inventory\")");
		}
		if (in.key_pressed(KEY_ESCAPE)) {
			add(var() + ".key_press(\"pause\")");
		}
	}

	// World interaction only happens with the mouse captured and no screen/chat in the way.
	if (!app.mouse_captured_state() || app.ui().is_open() || app.chat_box_open()) {
		return;
	}
	const auto &b = app.bindings();
	if (in.key_down(b.forward) || in.key_down(b.back) || in.key_down(b.left) || in.key_down(b.right)) {
		moving_ = true;
		last_move_frame_ = frame_;
	} else if (moving_ && frame_ - last_move_frame_ >= kWalkIdleFrames) {
		flush_walk();
		write();
	}
	if (in.key_pressed(b.jump)) {
		add(var() + ".key_press(\"jump\")");
	}
	for (int i = 0; i < 9; ++i) {
		if (in.key_pressed(KEY_ONE + i)) {
			add(var() + ".select_slot(" + std::to_string(i + 1) + ")");
		}
	}

	const bool left = in.mouse_button_pressed(MOUSE_BUTTON_LEFT);
	const bool right = in.mouse_button_pressed(MOUSE_BUTTON_RIGHT);
	if (!left && !right) {
		return;
	}
	const vb::world::VoxelRayHit hit = vb::world::raycast_voxel(s->chunk_store(), app.eye(),
			vb::core::forward_from_yaw_pitch(app.yaw(), app.pitch()), kReach);
	if (!hit.hit) {
		return; // a click at the sky is nothing a test needs to repeat
	}
	if (left) {
		const bool same = have_last_break_ && last_break_pos_ == hit.voxel && frame_ - last_break_frame_ < kSameBlockFrames;
		last_break_frame_ = frame_;
		if (!same) {
			have_last_break_ = true;
			last_break_pos_ = hit.voxel;
			add(var() + ".break_block(" + tuple3(hit.voxel) + ")");
			pending_breaks_.push_back({ hit.voxel, block_name(*s, hit.voxel).value_or(""), frame_ });
		}
	}
	if (right) {
		const vb::core::IVec3 dest{ hit.voxel.x + hit.normal.x, hit.voxel.y + hit.normal.y, hit.voxel.z + hit.normal.z };
		add(var() + ".place_block(" + tuple3(hit.voxel) + ", face=" + tuple3(hit.normal) + ")");
		pending_places_.push_back({ dest, block_name(*s, dest).value_or(""), frame_ });
	}
}

void Recorder::check_pending(const ClientApp &app) {
	const vb::net::ClientSession *s = app.session();
	auto sweep = [&](std::vector<Pending> &list) {
		for (auto it = list.begin(); it != list.end();) {
			const auto now = block_name(*s, it->pos);
			if (now && *now != it->before) {
				add("expect(" + var() + ").to_see_block(" + tuple3(it->pos) + ", " + quote(*now) + ")");
				it = list.erase(it);
			} else if (frame_ - it->since > kResultFrames) {
				it = list.erase(it); // nothing visible happened: record no claim about it
			} else {
				++it;
			}
		}
	};
	sweep(pending_breaks_);
	sweep(pending_places_);
}

void Recorder::on_chat_sent(std::string_view text) {
	const std::string t(text);
	add(var() + ".chat(" + quote(t) + ")");
	add("expect(" + var() + ").to_have_chat(" + quote(player_name_ + ": " + t) + ")");
}

void Recorder::on_ui_click(const std::string &id, bool hud) {
	add(var() + "." + (hud ? "hud" : "ui") + "(" + quote(id) + ").click()");
}

void Recorder::on_ui_change(const std::string &id, const std::string &text, bool hud) {
	add(var() + "." + (hud ? "hud" : "ui") + "(" + quote(id) + ").fill(" + quote(text) + ")");
}

void Recorder::on_ui_list(const std::string &id, int index, bool hud) {
	add(var() + "." + (hud ? "hud" : "ui") + "(" + quote(id) + ").select(" + std::to_string(index) + ")");
}

void Recorder::on_menu_connect(const std::string &host, int port, const std::string &name) {
	player_name_ = name;
	steps_.push_back("# the session started on the main menu: Connect to " + host + ":" + std::to_string(port) + " as " + name);
	write();
}

void Recorder::on_menu_singleplayer(const std::string &name) {
	player_name_ = name;
	steps_.push_back("# the session started on the main menu: Play Singleplayer as " + name + " (replay needs a dedicated `server`)");
	write();
}

bool Recorder::write() const {
	std::ofstream f(path_, std::ios::trunc);
	if (!f) {
		return false;
	}
	std::time_t now = std::time(nullptr);
	char stamp[32];
	std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::gmtime(&now));
	const std::string v = var();
	f << "\"\"\"Recorded with `voxel_browser --automation-record` on " << stamp << " UTC.\n"
	  << "\n"
	  << "A starting point, not a finished test: it replays what you did, with an assertion after the\n"
	  << "steps whose result it could see. Scene setup (set_block, give, teleport) is not recorded:\n"
	  << "add it with `server`. Coordinates are absolute, so replay against the same world (same seed).\n"
	  << "\"\"\"\n"
	  << "from vbtest import expect\n\n\n"
	  << "def test_recorded_session(server, clients):\n"
	  << "    (" << v << ",) = clients(1, names=[" << quote(player_name_) << "])\n"
	  << "    expect(" << v << ").to_be_on_ground()\n";
	for (const std::string &step : steps_) {
		f << "    " << step << "\n";
	}
	return f.good();
}

} // namespace vb::client
