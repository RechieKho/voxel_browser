#include "vb/render/ui_renderer.hpp"

#include <cstring>

#include <raygui.h>

namespace vb::render {

namespace {

constexpr int kTextBoxBufferSize = 256;

} // namespace

UiFrameResult UiRenderer::draw(std::string_view ui_name,
		const std::vector<script::Widget> &widgets) {
	if (ui_name != last_ui_name_) {
		text_buffers_.clear();
		edit_mode_.clear();
		list_active_.clear();
		list_scroll_.clear();
		last_ui_name_ = std::string(ui_name);
	}

	UiFrameResult result;

	for (const auto &w : widgets) {
		const Rectangle bounds{ w.x, w.y, w.w, w.h };
		switch (w.type) {
			case script::WidgetType::kLabel:
				GuiLabel(bounds, w.text.c_str());
				break;
			case script::WidgetType::kPanel:
				GuiPanel(bounds, w.text.c_str());
				break;
			case script::WidgetType::kButton:
				if (GuiButton(bounds, w.text.c_str())) {
					result.clicked.push_back(w.id);
				}
				break;
			case script::WidgetType::kTextBox: {
				auto buf_it = text_buffers_.find(w.id);
				if (buf_it == text_buffers_.end()) {
					buf_it = text_buffers_.emplace(w.id, w.text).first;
				}
				std::string &buf = buf_it->second;
				buf.resize(kTextBoxBufferSize, '\0');
				bool &edit = edit_mode_[w.id];
				const bool was_editing = edit;
				if (GuiTextBox(bounds, buf.data(), kTextBoxBufferSize, edit)) {
					edit = !edit;
				}
				buf.resize(std::strlen(buf.c_str()));
				if (was_editing && !edit) {
					result.changed_text.emplace_back(w.id, buf);
				}
				break;
			}
			case script::WidgetType::kList: {
				std::vector<const char *> items;
				items.reserve(w.items.size());
				for (const auto &item : w.items) {
					items.push_back(item.c_str());
				}
				auto active_it = list_active_.find(w.id);
				if (active_it == list_active_.end()) {
					active_it = list_active_.emplace(w.id, w.list_index).first;
				}
				int &scroll = list_scroll_[w.id];
				int focus = -1;
				const int before = active_it->second;
				GuiListViewEx(bounds, items.data(), static_cast<int>(items.size()),
						&scroll, &active_it->second, &focus);
				if (active_it->second != before) {
					result.changed_list.emplace_back(w.id, active_it->second);
				}
				break;
			}
		}
	}

	return result;
}

} // namespace vb::render
