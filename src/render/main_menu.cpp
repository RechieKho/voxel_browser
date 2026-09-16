#include "vb/render/main_menu.hpp"

#include <cstdio>
#include <cstring>

#include <raygui.h>
#include <raylib.h>

namespace vb::render {

namespace {

constexpr int kTextBufSize = 128;

// GuiTextBox needs a fixed-capacity, nul-terminated buffer; `s` is resized to
// kTextBufSize before the call and trimmed back to its real length after,
// mirroring UiRenderer::draw's kTextBox case.
bool text_box(Rectangle bounds, std::string &s, bool &edit) {
	s.resize(kTextBufSize, '\0');
	const bool toggled = GuiTextBox(bounds, s.data(), kTextBufSize, edit) != 0;
	if (toggled) {
		edit = !edit;
	}
	s.resize(std::strlen(s.c_str()));
	return toggled;
}

Rectangle centered(float w, float h, float y) {
	const float x = (static_cast<float>(GetScreenWidth()) - w) * 0.5f;
	return Rectangle{ x, y, w, h };
}

} // namespace

MainMenu::MainMenu(const core::ClientConfig &config) :
		address_("127.0.0.1"), port_text_("27015"), player_name_(config.player_name) {
}

void MainMenu::prefill(std::string_view address, int port, std::string_view player_name) {
	address_ = std::string(address);
	port_ = port;
	player_name_ = std::string(player_name);
}

MainMenu::MainResult MainMenu::draw_main(const std::vector<std::string> &recent_servers) {
	MainResult result;

	const float panel_w = 420.0f;
	GuiPanel(centered(panel_w, 420.0f, 80.0f), "voxel_browser");

	float y = 130.0f;
	const float x = (static_cast<float>(GetScreenWidth()) - panel_w) * 0.5f + 20.0f;
	const float field_w = panel_w - 40.0f;

	GuiLabel(Rectangle{ x, y, field_w, 20.0f }, "Player name");
	y += 22.0f;
	text_box(Rectangle{ x, y, field_w, 28.0f }, player_name_, name_edit_);
	y += 38.0f;

	GuiLabel(Rectangle{ x, y, field_w, 20.0f }, "Server address");
	y += 22.0f;
	text_box(Rectangle{ x, y, field_w * 0.65f, 28.0f }, address_, address_edit_);
	if (GuiValueBox(Rectangle{ x + field_w * 0.68f, y, field_w * 0.32f, 28.0f },
				nullptr, &port_, 1, 65535, port_edit_) != 0) {
		port_edit_ = !port_edit_;
	}
	y += 40.0f;

	if (GuiButton(Rectangle{ x, y, field_w, 32.0f }, "Connect")) {
		result.connect = true;
	}
	y += 40.0f;
	if (GuiButton(Rectangle{ x, y, field_w, 32.0f }, "Play Singleplayer")) {
		result.singleplayer = true;
	}
	y += 40.0f;

	if (!recent_servers.empty()) {
		GuiLabel(Rectangle{ x, y, field_w, 20.0f }, "Recent servers");
		y += 22.0f;
		std::vector<const char *> items;
		items.reserve(recent_servers.size());
		for (const auto &s : recent_servers) {
			items.push_back(s.c_str());
		}
		const float list_h = 84.0f;
		const int before = recent_active_;
		GuiListView(Rectangle{ x, y, field_w, list_h }, nullptr, &recent_scroll_,
				&recent_active_);
		if (recent_active_ != before && recent_active_ >= 0 &&
				static_cast<std::size_t>(recent_active_) < recent_servers.size()) {
			const std::string &picked = recent_servers[static_cast<std::size_t>(recent_active_)];
			const auto colon = picked.rfind(':');
			if (colon != std::string::npos) {
				address_ = picked.substr(0, colon);
				try {
					port_ = std::stoi(picked.substr(colon + 1));
				} catch (...) {
				}
			} else {
				address_ = picked;
			}
		}
		y += list_h + 10.0f;
	}

	if (GuiButton(Rectangle{ x, y, field_w * 0.48f, 32.0f }, "Settings")) {
		result.open_settings = true;
	}
	if (GuiButton(Rectangle{ x + field_w * 0.52f, y, field_w * 0.48f, 32.0f }, "Quit")) {
		result.quit = true;
	}

	return result;
}

void MainMenu::open_settings(const core::ClientConfig &config) {
	s_width_ = static_cast<int>(config.window_width);
	s_height_ = static_cast<int>(config.window_height);
	s_vsync_ = config.vsync;
	s_fov_ = static_cast<float>(config.fov);
	s_render_distance_ = static_cast<float>(config.render_distance);
	s_sensitivity_ = static_cast<float>(config.mouse_sensitivity);
	s_cache_mb_ = static_cast<int>(config.asset_cache_mb);
}

MainMenu::SettingsResult MainMenu::draw_settings(core::ClientConfig &config) {
	SettingsResult result;

	const float panel_w = 460.0f;
	GuiPanel(centered(panel_w, 420.0f, 80.0f), "Settings");

	float y = 130.0f;
	const float x = (static_cast<float>(GetScreenWidth()) - panel_w) * 0.5f + 20.0f;
	const float field_w = panel_w - 40.0f;

	GuiLabel(Rectangle{ x, y, field_w * 0.5f, 24.0f }, "Window width (applies on restart)");
	if (GuiValueBox(Rectangle{ x + field_w * 0.55f, y, field_w * 0.2f, 24.0f }, nullptr,
				&s_width_, 640, 3840, s_width_edit_) != 0) {
		s_width_edit_ = !s_width_edit_;
	}
	if (GuiValueBox(Rectangle{ x + field_w * 0.78f, y, field_w * 0.2f, 24.0f }, nullptr,
				&s_height_, 480, 2160, s_height_edit_) != 0) {
		s_height_edit_ = !s_height_edit_;
	}
	y += 32.0f;

	GuiCheckBox(Rectangle{ x, y, 20.0f, 20.0f }, "V-Sync (applies on restart)", &s_vsync_);
	y += 34.0f;

	char fov_label[32];
	std::snprintf(fov_label, sizeof(fov_label), "%.0f", static_cast<double>(s_fov_));
	GuiSlider(Rectangle{ x, y, field_w, 20.0f }, "FOV", fov_label, &s_fov_, 50.0f, 110.0f);
	y += 32.0f;

	char rd_label[32];
	std::snprintf(rd_label, sizeof(rd_label), "%.0f chunks",
			static_cast<double>(s_render_distance_));
	GuiSlider(Rectangle{ x, y, field_w, 20.0f }, "Render distance", rd_label,
			&s_render_distance_, 2.0f, 32.0f);
	y += 32.0f;

	char sens_label[32];
	std::snprintf(sens_label, sizeof(sens_label), "%.2f", static_cast<double>(s_sensitivity_));
	GuiSlider(Rectangle{ x, y, field_w, 20.0f }, "Sensitivity", sens_label, &s_sensitivity_,
			0.02f, 0.6f);
	y += 32.0f;

	GuiLabel(Rectangle{ x, y, field_w * 0.55f, 24.0f }, "Asset cache (MB)");
	if (GuiValueBox(Rectangle{ x + field_w * 0.6f, y, field_w * 0.4f, 24.0f }, nullptr,
				&s_cache_mb_, 64, 8192, s_cache_edit_) != 0) {
		s_cache_edit_ = !s_cache_edit_;
	}
	y += 44.0f;

	if (GuiButton(Rectangle{ x, y, field_w * 0.48f, 32.0f }, "Save")) {
		config.window_width = static_cast<std::uint32_t>(s_width_);
		config.window_height = static_cast<std::uint32_t>(s_height_);
		config.vsync = s_vsync_;
		config.fov = static_cast<double>(s_fov_);
		config.render_distance = static_cast<std::uint32_t>(s_render_distance_);
		config.mouse_sensitivity = static_cast<double>(s_sensitivity_);
		config.asset_cache_mb = static_cast<std::uint32_t>(s_cache_mb_);
		result.save = true;
	}
	if (GuiButton(Rectangle{ x + field_w * 0.52f, y, field_w * 0.48f, 32.0f }, "Back")) {
		result.back = true;
	}

	return result;
}

MainMenu::ConnectingResult MainMenu::draw_connecting(std::string_view status_text) {
	ConnectingResult result;
	const float panel_w = 420.0f;
	GuiPanel(centered(panel_w, 140.0f, 220.0f), "Connecting");
	const float x = (static_cast<float>(GetScreenWidth()) - panel_w) * 0.5f + 20.0f;
	GuiLabel(Rectangle{ x, 270.0f, panel_w - 40.0f, 24.0f },
			std::string(status_text).c_str());
	if (GuiButton(Rectangle{ x, 310.0f, panel_w - 40.0f, 32.0f }, "Cancel")) {
		result.cancel = true;
	}
	return result;
}

MainMenu::ErrorResult MainMenu::draw_error(std::string_view reason) {
	ErrorResult result;
	const float panel_w = 460.0f;
	GuiPanel(centered(panel_w, 160.0f, 220.0f), "Could not connect");
	const float x = (static_cast<float>(GetScreenWidth()) - panel_w) * 0.5f + 20.0f;
	GuiLabel(Rectangle{ x, 270.0f, panel_w - 40.0f, 48.0f }, std::string(reason).c_str());
	if (GuiButton(Rectangle{ x, 330.0f, panel_w - 40.0f, 32.0f }, "Back to menu")) {
		result.back = true;
	}
	return result;
}

} // namespace vb::render
