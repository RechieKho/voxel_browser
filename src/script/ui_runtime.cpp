#include "vb/script/ui_runtime.hpp"

#if !VB_WITH_LUA

// Stub build: scripting compiled out. Every entry point is a no-op,
// mirroring vm.cpp / pack_runtime.cpp's disabled-build pattern.

namespace vb::script {

struct UiRuntime::Impl {};

UiRuntime::UiRuntime(VmLimits) : impl_(nullptr) {}
UiRuntime::~UiRuntime() = default;
UiRuntime::UiRuntime(UiRuntime &&) noexcept = default;
UiRuntime &UiRuntime::operator=(UiRuntime &&) noexcept = default;

ScriptResult UiRuntime::load_pack_file(std::string_view, std::string_view) {
	return { false, core::ScriptError::kDisabled,
		"scripting disabled (built without VB_WITH_LUA)" };
}
void UiRuntime::attach_session(net::ClientSession &) {}
void UiRuntime::open(std::string_view, std::string_view) {}
void UiRuntime::close() {}
bool UiRuntime::is_open() const { return false; }
const std::string &UiRuntime::current_name() const {
	static const std::string kEmpty;
	return kEmpty;
}
const std::vector<Widget> &UiRuntime::render_frame() {
	static const std::vector<Widget> kEmpty;
	return kEmpty;
}
const std::vector<Widget> &UiRuntime::widgets() const {
	static const std::vector<Widget> kEmpty;
	return kEmpty;
}
void UiRuntime::report_click(const std::string &) {}
void UiRuntime::report_change(const std::string &, std::string_view) {}
void UiRuntime::report_list_change(const std::string &, int) {}
void UiRuntime::set_break_progress(std::optional<float>) {}
void UiRuntime::set_screen_size(int, int) {}
const std::vector<Widget> &UiRuntime::render_hud() {
	static const std::vector<Widget> kEmpty;
	return kEmpty;
}

} // namespace vb::script

#else

#include <unordered_map>

#include <nlohmann/json.hpp>

#include "vb/core/log.hpp"
#include "vb/script/vm_internal.hpp"

namespace vb::script {

namespace {

sol::object json_to_lua(sol::state_view lua, const nlohmann::json &j) {
	switch (j.type()) {
		case nlohmann::json::value_t::null:
			return sol::make_object(lua, sol::lua_nil);
		case nlohmann::json::value_t::boolean:
			return sol::make_object(lua, j.get<bool>());
		case nlohmann::json::value_t::number_integer:
		case nlohmann::json::value_t::number_unsigned:
		case nlohmann::json::value_t::number_float:
			return sol::make_object(lua, j.get<double>());
		case nlohmann::json::value_t::string:
			return sol::make_object(lua, j.get<std::string>());
		case nlohmann::json::value_t::array: {
			sol::table t = lua.create_table();
			int i = 1;
			for (const auto &e : j) {
				t[i++] = json_to_lua(lua, e);
			}
			return t;
		}
		case nlohmann::json::value_t::object: {
			sol::table t = lua.create_table();
			for (const auto &[k, v] : j.items()) {
				t[k] = json_to_lua(lua, v);
			}
			return t;
		}
		default:
			return sol::make_object(lua, sol::lua_nil);
	}
}

nlohmann::json lua_to_json(const sol::object &obj) {
	switch (obj.get_type()) {
		case sol::type::lua_nil:
		case sol::type::none:
			return nullptr;
		case sol::type::boolean:
			return obj.as<bool>();
		case sol::type::number:
			return obj.as<double>();
		case sol::type::string:
			return obj.as<std::string>();
		case sol::type::table: {
			sol::table t = obj.as<sol::table>();
			std::size_t count = 0;
			for (const auto &kv : t) {
				(void)kv;
				++count;
			}
			bool is_array = count > 0;
			for (std::size_t i = 1; i <= count && is_array; ++i) {
				if (!t[i].valid()) {
					is_array = false;
				}
			}
			if (is_array) {
				nlohmann::json arr = nlohmann::json::array();
				for (std::size_t i = 1; i <= count; ++i) {
					arr.push_back(lua_to_json(t[i]));
				}
				return arr;
			}
			nlohmann::json j = nlohmann::json::object();
			for (const auto &kv : t) {
				if (kv.first.is<std::string>()) {
					j[kv.first.as<std::string>()] = lua_to_json(kv.second);
				}
			}
			return j;
		}
		default:
			return nullptr;
	}
}

WidgetType widget_type_from(const std::string &s) {
	if (s == "panel") {
		return WidgetType::kPanel;
	}
	if (s == "button") {
		return WidgetType::kButton;
	}
	if (s == "textbox") {
		return WidgetType::kTextBox;
	}
	if (s == "list") {
		return WidgetType::kList;
	}
	if (s == "rect") {
		return WidgetType::kRect;
	}
	if (s != "label") {
		VB_WARN("script", "ui: unknown widget type '", s, "', treating as label");
	}
	return WidgetType::kLabel;
}

// Shared by evaluate_frame() (modal screens) and evaluate_hud_frame() (the
// always-on HUD) -- both parse a `{widgets = {...}}` render result the same
// way, just into two separate widget lists/widget_by_id maps.
Widget widget_from_table(const sol::table &wt) {
	Widget w;
	w.id = wt.get_or("id", std::string{});
	w.type = widget_type_from(wt.get_or("type", std::string("label")));
	w.x = wt.get_or("x", 0.0f);
	w.y = wt.get_or("y", 0.0f);
	w.w = wt.get_or("w", 0.0f);
	w.h = wt.get_or("h", 0.0f);
	w.text = wt.get_or("text", std::string{});
	w.list_index = wt.get_or("list_index", -1);
	sol::object items_obj = wt["items"];
	if (items_obj.get_type() == sol::type::table) {
		for (const auto &ikv : items_obj.as<sol::table>()) {
			if (ikv.second.is<std::string>()) {
				w.items.push_back(ikv.second.as<std::string>());
			}
		}
	}
	// kRect only: `color = {r,g,b,a?}` (fill, default opaque white) and an
	// optional `border = {r,g,b,a?}` (default fully transparent -- no
	// border drawn). 1-indexed like every other Lua color array in this
	// codebase (vb.daynight.set_curve's `color = {r,g,b}`).
	sol::object color_obj = wt["color"];
	if (color_obj.get_type() == sol::type::table) {
		sol::table c = color_obj.as<sol::table>();
		w.fill_r = c.get_or(1, std::uint8_t{ 255 });
		w.fill_g = c.get_or(2, std::uint8_t{ 255 });
		w.fill_b = c.get_or(3, std::uint8_t{ 255 });
		w.fill_a = c.get_or(4, std::uint8_t{ 255 });
	}
	sol::object border_obj = wt["border"];
	if (border_obj.get_type() == sol::type::table) {
		sol::table c = border_obj.as<sol::table>();
		w.border_r = c.get_or(1, std::uint8_t{ 0 });
		w.border_g = c.get_or(2, std::uint8_t{ 0 });
		w.border_b = c.get_or(3, std::uint8_t{ 0 });
		w.border_a = c.get_or(4, std::uint8_t{ 255 });
	}
	return w;
}

} // namespace

struct UiRuntime::Impl {
	Vm vm;
	net::ClientSession *session = nullptr;
	std::unordered_map<std::string, sol::protected_function> layout_fns;

	std::string current_name;
	sol::protected_function current_render_fn;
	sol::table current_state; // persists across every frame this screen is open
	sol::protected_function current_on_close; // refreshed by each evaluate_frame()
	std::unordered_map<std::string, sol::table> widget_by_id;
	std::vector<Widget> widgets_vec;
	std::string current_widget_id; // scratch, valid during a callback

	// HUD (always-on overlay, independent of open()/close() above): a
	// single registered render_fn with its own persistent state table --
	// there's only ever one HUD, unlike the named-screen registry
	// (layout_fns) modal screens use.
	sol::protected_function hud_render_fn;
	sol::table hud_state;
	std::vector<Widget> hud_widgets_vec;
	// Raw engine state a HUD's render_fn can read via client.break_progress()
	// -- nullopt when the player isn't currently breaking anything. Set once
	// per frame by src/client/main.cpp's own input-handling code, which
	// already computes this; the engine never draws it itself.
	std::optional<float> break_progress;
	// Window size a HUD's render_fn can read via client.screen_size() --
	// widgets take absolute pixel positions, so centering anything needs
	// this. Zero until the first set_screen_size() call.
	int screen_w = 0;
	int screen_h = 0;

	explicit Impl(VmLimits limits);

	sol::state &lua_state() { return vm.native_impl().lua; }
	void install_bindings();
	void send_event(const std::string &kind, const sol::object &value);
	void run_callback(const std::string &widget_id, const char *field,
			const sol::object &arg, bool has_arg);
	void evaluate_frame();
	void evaluate_hud_frame();
	void do_close();
};

UiRuntime::Impl::Impl(VmLimits limits) : vm(limits) {
	install_bindings();
	hud_state = lua_state().create_table();
}

void UiRuntime::Impl::send_event(const std::string &kind, const sol::object &value) {
	if (session == nullptr) {
		return;
	}
	protocol::C2SUiEvent e;
	e.ui_name = current_name;
	e.widget_id = current_widget_id;
	e.event_kind = kind;
	e.value_json = lua_to_json(value).dump();
	session->send_ui_event(e);
}

void UiRuntime::Impl::install_bindings() {
	sol::state &lua = lua_state();
	sol::table ui = lua.create_named_table("ui");

	ui["define"] = [this](const std::string &name, sol::protected_function fn) {
		layout_fns[name] = std::move(fn);
	};

	// Registers the single always-on HUD render function (see the header's
	// "HUD" section) -- distinct from ui.define's named-screen registry
	// above since a HUD isn't opened/closed, just always active.
	ui["define_hud"] = [this](sol::protected_function fn) {
		hud_render_fn = std::move(fn);
	};

	ui["send_event"] = [this](const std::string &kind, sol::object value) {
		send_event(kind, value);
	};

	ui["close"] = [this] { do_close(); };

	// Raw, engine-computed local client state a HUD (or any Lua UI) can
	// query for presentation -- "engine provides raw state, Lua decides how
	// to show it": nothing here draws a pixel, it's read-only data.
	sol::table client_tbl = lua.create_named_table("client");
	client_tbl["break_progress"] = [this]() -> sol::object {
		if (!break_progress) {
			return sol::make_object(lua_state(), sol::lua_nil);
		}
		return sol::make_object(lua_state(), *break_progress);
	};
	client_tbl["screen_size"] = [this]() -> sol::table {
		sol::table t = lua_state().create_table();
		t["width"] = screen_w;
		t["height"] = screen_h;
		return t;
	};
}

void UiRuntime::Impl::evaluate_frame() {
	if (current_name.empty() || !current_render_fn.valid()) {
		return;
	}
	vm.begin_call_budget();
	sol::protected_function_result r = current_render_fn(current_state);
	if (!r.valid()) {
		const sol::error e = r;
		VB_WARN("script", "ui '", current_name, "' render function error: ", e.what());
		return; // leave the previous frame's widgets in place, don't flicker
	}
	sol::object result = r;
	if (result.get_type() != sol::type::table) {
		VB_WARN("script", "ui '", current_name,
				"' render function did not return a table");
		return;
	}
	sol::table layout = result.as<sol::table>();
	sol::object widgets_obj = layout["widgets"];
	if (widgets_obj.get_type() != sol::type::table) {
		VB_WARN("script", "ui '", current_name, "' render result has no 'widgets' array");
		return;
	}
	sol::table widget_tables = widgets_obj.as<sol::table>();

	sol::object on_close = layout["on_close"];
	current_on_close = on_close.is<sol::protected_function>()
			? on_close.as<sol::protected_function>()
			: sol::protected_function();

	widget_by_id.clear();
	widgets_vec.clear();
	for (const auto &kv : widget_tables) {
		sol::object entry = kv.second;
		if (entry.get_type() != sol::type::table) {
			continue;
		}
		sol::table wt = entry.as<sol::table>();
		Widget w = widget_from_table(wt);
		if (!w.id.empty()) {
			widget_by_id[w.id] = wt;
		}
		widgets_vec.push_back(std::move(w));
	}
}

void UiRuntime::Impl::evaluate_hud_frame() {
	if (!hud_render_fn.valid()) {
		return;
	}
	vm.begin_call_budget();
	sol::protected_function_result r = hud_render_fn(hud_state);
	if (!r.valid()) {
		const sol::error e = r;
		VB_WARN("script", "ui hud render function error: ", e.what());
		return; // leave the previous frame's widgets in place, don't flicker
	}
	sol::object result = r;
	if (result.get_type() != sol::type::table) {
		VB_WARN("script", "ui hud render function did not return a table");
		return;
	}
	sol::table layout = result.as<sol::table>();
	sol::object widgets_obj = layout["widgets"];
	if (widgets_obj.get_type() != sol::type::table) {
		VB_WARN("script", "ui hud render result has no 'widgets' array");
		return;
	}
	sol::table widget_tables = widgets_obj.as<sol::table>();

	hud_widgets_vec.clear();
	for (const auto &kv : widget_tables) {
		sol::object entry = kv.second;
		if (entry.get_type() != sol::type::table) {
			continue;
		}
		hud_widgets_vec.push_back(widget_from_table(entry.as<sol::table>()));
	}
}

void UiRuntime::Impl::do_close() {
	if (current_name.empty()) {
		return;
	}
	send_event("close", sol::lua_nil);
	if (current_on_close.valid()) {
		vm.begin_call_budget();
		sol::protected_function_result r = current_on_close();
		if (!r.valid()) {
			const sol::error e = r;
			VB_WARN("script", "ui on_close handler error: ", e.what());
		}
	}
	current_name.clear();
	current_render_fn = sol::protected_function();
	current_state = sol::lua_nil;
	current_on_close = sol::protected_function();
	widget_by_id.clear();
	widgets_vec.clear();
}

void UiRuntime::Impl::run_callback(const std::string &widget_id, const char *field,
		const sol::object &arg, bool has_arg) {
	auto it = widget_by_id.find(widget_id);
	if (it == widget_by_id.end()) {
		return;
	}
	sol::object cb_obj = it->second[field];
	if (!cb_obj.is<sol::protected_function>()) {
		return;
	}
	sol::protected_function cb = cb_obj.as<sol::protected_function>();
	current_widget_id = widget_id;
	vm.begin_call_budget();
	sol::protected_function_result r = has_arg ? cb(arg) : cb();
	if (!r.valid()) {
		const sol::error e = r;
		VB_WARN("script", "ui widget '", widget_id, "' ", field,
				" handler error: ", e.what());
	}
}

UiRuntime::UiRuntime(VmLimits limits) : impl_(std::make_unique<Impl>(limits)) {}
UiRuntime::~UiRuntime() = default;
UiRuntime::UiRuntime(UiRuntime &&) noexcept = default;
UiRuntime &UiRuntime::operator=(UiRuntime &&) noexcept = default;

ScriptResult UiRuntime::load_pack_file(std::string_view code,
		std::string_view chunk_name) {
	return impl_->vm.do_string(code, chunk_name);
}

void UiRuntime::attach_session(net::ClientSession &session) {
	impl_->session = &session;
}

void UiRuntime::open(std::string_view name, std::string_view ctx_json) {
	auto it = impl_->layout_fns.find(std::string(name));
	if (it == impl_->layout_fns.end()) {
		VB_WARN("script", "ui.open: '", name, "' was never defined via ui.define");
		return;
	}

	nlohmann::json parsed;
	try {
		parsed = ctx_json.empty() ? nlohmann::json::object()
								  : nlohmann::json::parse(ctx_json);
	} catch (const nlohmann::json::parse_error &) {
		parsed = nlohmann::json::object();
	}

	sol::object ctx_obj = json_to_lua(impl_->lua_state(), parsed);
	sol::table state = ctx_obj.get_type() == sol::type::table
			? ctx_obj.as<sol::table>()
			: impl_->lua_state().create_table();

	impl_->current_name = std::string(name);
	impl_->current_render_fn = it->second;
	impl_->current_state = state;
	impl_->current_on_close = sol::protected_function();
	impl_->widget_by_id.clear();
	impl_->widgets_vec.clear();
}

void UiRuntime::close() { impl_->do_close(); }

bool UiRuntime::is_open() const { return !impl_->current_name.empty(); }
const std::string &UiRuntime::current_name() const { return impl_->current_name; }

const std::vector<Widget> &UiRuntime::render_frame() {
	impl_->evaluate_frame();
	return impl_->widgets_vec;
}

const std::vector<Widget> &UiRuntime::widgets() const { return impl_->widgets_vec; }

void UiRuntime::report_click(const std::string &widget_id) {
	impl_->run_callback(widget_id, "on_click", sol::lua_nil, false);
}

void UiRuntime::report_change(const std::string &widget_id,
		std::string_view text_value) {
	sol::object v = sol::make_object(impl_->lua_state(), std::string(text_value));
	impl_->run_callback(widget_id, "on_change", v, true);
}

void UiRuntime::report_list_change(const std::string &widget_id, int new_index) {
	sol::object v = sol::make_object(impl_->lua_state(), new_index);
	impl_->run_callback(widget_id, "on_change", v, true);
}

void UiRuntime::set_break_progress(std::optional<float> fraction) {
	impl_->break_progress = fraction;
}

void UiRuntime::set_screen_size(int width, int height) {
	impl_->screen_w = width;
	impl_->screen_h = height;
}

const std::vector<Widget> &UiRuntime::render_hud() {
	impl_->evaluate_hud_frame();
	return impl_->hud_widgets_vec;
}

} // namespace vb::script

#endif // VB_WITH_LUA
