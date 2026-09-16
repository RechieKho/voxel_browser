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
const std::vector<Widget> &UiRuntime::widgets() const {
	static const std::vector<Widget> kEmpty;
	return kEmpty;
}
void UiRuntime::report_click(const std::string &) {}
void UiRuntime::report_change(const std::string &, std::string_view) {}
void UiRuntime::report_list_change(const std::string &, int) {}

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
	if (s != "label") {
		VB_WARN("script", "ui: unknown widget type '", s, "', treating as label");
	}
	return WidgetType::kLabel;
}

} // namespace

struct UiRuntime::Impl {
	Vm vm;
	net::ClientSession *session = nullptr;
	std::unordered_map<std::string, sol::protected_function> layout_fns;

	std::string current_name;
	sol::table current_layout; // {widgets = {...}, on_close = fn?}
	std::unordered_map<std::string, sol::table> widget_by_id;
	std::vector<Widget> widgets_vec;
	std::string current_widget_id; // scratch, valid during a callback

	explicit Impl(VmLimits limits);

	sol::state &lua_state() { return vm.native_impl().lua; }
	void install_bindings();
	void send_event(const std::string &kind, const sol::object &value);
	void run_callback(const std::string &widget_id, const char *field,
			const sol::object &arg, bool has_arg);
	void do_close();
};

UiRuntime::Impl::Impl(VmLimits limits) : vm(limits) { install_bindings(); }

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

	ui["send_event"] = [this](const std::string &kind, sol::object value) {
		send_event(kind, value);
	};

	ui["close"] = [this] { do_close(); };
}

void UiRuntime::Impl::do_close() {
	if (current_name.empty()) {
		return;
	}
	send_event("close", sol::lua_nil);
	if (current_layout.valid()) {
		sol::object on_close = current_layout["on_close"];
		if (on_close.is<sol::protected_function>()) {
			vm.begin_call_budget();
			sol::protected_function_result r =
					on_close.as<sol::protected_function>()();
			if (!r.valid()) {
				const sol::error e = r;
				VB_WARN("script", "ui on_close handler error: ", e.what());
			}
		}
	}
	current_name.clear();
	current_layout = sol::lua_nil;
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
	sol::object ctx = json_to_lua(impl_->lua_state(), parsed);

	impl_->vm.begin_call_budget();
	sol::protected_function_result r = it->second(ctx);
	if (!r.valid()) {
		const sol::error e = r;
		VB_WARN("script", "ui '", name, "' layout function error: ", e.what());
		return;
	}
	sol::object result = r;
	if (result.get_type() != sol::type::table) {
		VB_WARN("script", "ui '", name, "' layout function did not return a table");
		return;
	}
	sol::table layout = result.as<sol::table>();
	sol::object widgets_obj = layout["widgets"];
	if (widgets_obj.get_type() != sol::type::table) {
		VB_WARN("script", "ui '", name, "' layout has no 'widgets' array");
		return;
	}
	sol::table widget_tables = widgets_obj.as<sol::table>();

	impl_->current_name = std::string(name);
	impl_->current_layout = layout;
	impl_->widget_by_id.clear();
	impl_->widgets_vec.clear();

	for (const auto &kv : widget_tables) {
		sol::object entry = kv.second;
		if (entry.get_type() != sol::type::table) {
			continue;
		}
		sol::table wt = entry.as<sol::table>();
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
		if (!w.id.empty()) {
			impl_->widget_by_id[w.id] = wt;
		}
		impl_->widgets_vec.push_back(std::move(w));
	}
}

void UiRuntime::close() { impl_->do_close(); }

bool UiRuntime::is_open() const { return !impl_->current_name.empty(); }
const std::string &UiRuntime::current_name() const { return impl_->current_name; }
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

} // namespace vb::script

#endif // VB_WITH_LUA
