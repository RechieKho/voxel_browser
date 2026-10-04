#include "vb/cli/compat.hpp"

#include <cctype>

#include <toml++/toml.hpp>

#include "vb/cli/instance.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/store.hpp"

namespace vb::cli {

namespace fs = std::filesystem;

Status parse_connect_target(std::string_view text, ConnectTarget &out) {
	ConnectTarget t;
	std::string_view host = text;
	std::string_view port_text;
	if (!text.empty() && text.front() == '[') { // [v6]:port
		const std::size_t close = text.find(']');
		if (close == std::string_view::npos) {
			return { "bad address '" + std::string(text) + "'" };
		}
		host = text.substr(1, close - 1);
		const std::string_view rest = text.substr(close + 1);
		if (!rest.empty()) {
			if (rest.front() != ':') {
				return { "bad address '" + std::string(text) + "'" };
			}
			port_text = rest.substr(1);
		}
	} else {
		const std::size_t colon = text.rfind(':');
		if (colon != std::string_view::npos && text.find(':') == colon) { // exactly one ':'
			host = text.substr(0, colon);
			port_text = text.substr(colon + 1);
		}
	}
	if (host.empty()) {
		return { "bad address '" + std::string(text) + "' (expected host or host:port)" };
	}
	if (!port_text.empty()) {
		if (port_text.size() > 5 || port_text.find_first_not_of("0123456789") != std::string_view::npos) {
			return { "bad port in '" + std::string(text) + "'" };
		}
		const int p = std::stoi(std::string(port_text));
		if (p < 1 || p > 65535) {
			return { "bad port in '" + std::string(text) + "' (1-65535)" };
		}
		t.port = static_cast<std::uint16_t>(p);
	}
	t.host = std::string(host);
	out = std::move(t);
	return {};
}

bool is_local_host(std::string_view host) {
	std::string h(host);
	for (char &c : h) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return h == "localhost" || h == "::1" || h == "0.0.0.0" || h.rfind("127.", 0) == 0;
}

std::optional<int> parse_protocol(std::string_view output) {
	const std::string_view key = "protocol ";
	const std::size_t at = output.find(key);
	if (at == std::string_view::npos) {
		return std::nullopt;
	}
	std::size_t i = at + key.size();
	int value = 0;
	int digits = 0;
	while (i < output.size() && std::isdigit(static_cast<unsigned char>(output[i])) && digits < 6) {
		value = value * 10 + (output[i] - '0');
		++i;
		++digits;
	}
	return digits > 0 ? std::optional<int>(value) : std::nullopt;
}

std::optional<int> entry_protocol(const Entry &entry, bool server) {
	// A release's receipt records the protocol its manifest declared.
	try {
		const toml::table t = toml::parse_file((entry.root / ".install.toml").string());
		if (const auto v = t["engine_protocol_version"].value<std::int64_t>(); v && *v > 0) {
			return static_cast<int>(*v);
		}
	} catch (const toml::parse_error &) {
	}
	const auto bin = find_binary(entry, server ? Binary::Server : Binary::Client);
	if (!bin) {
		return std::nullopt;
	}
	const CaptureResult r = run_capture(*bin, { "--version" }, entry.root);
	if (!r.error.empty() || r.exit_code != 0) {
		return std::nullopt;
	}
	return parse_protocol(r.output);
}

std::string protocol_warning(const Layout &layout, const Entry &client, const ConnectTarget &target) {
	if (!is_local_host(target.host)) {
		return {};
	}
	for (const Instance &inst : list_instances(layout)) {
		const auto rec = running_record(inst);
		if (!rec || rec->port != target.port) {
			continue;
		}
		const auto server_entry = resolve_entry(layout, rec->version);
		if (!server_entry) {
			return {};
		}
		const auto server_protocol = entry_protocol(*server_entry, true);
		const auto client_protocol = entry_protocol(client, false);
		if (!server_protocol || !client_protocol || *server_protocol == *client_protocol) {
			return {};
		}
		return "server '" + inst.name + "' on port " + std::to_string(target.port) + " runs " +
				server_entry->name + " (network protocol " + std::to_string(*server_protocol) +
				") but the client you are launching is " + client.name + " (protocol " +
				std::to_string(*client_protocol) + "); they will not be able to connect. "
												   "Launch the matching client with `vb launch --version " +
				server_entry->name +
				" --connect ...`, or restart the server on " + client.name + ".";
	}
	return {};
}

} // namespace vb::cli
