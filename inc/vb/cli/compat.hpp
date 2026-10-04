#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "vb/cli/layout.hpp"
#include "vb/cli/store.hpp"

// `vb launch --connect host:port`: warn when the client about to be launched
// speaks a different wire protocol than a server vb can see (dev-cli.md 8.6).
// A server on another machine is invisible to vb -- its own handshake still
// reports a mismatch -- so only servers vb manages locally are checked.

namespace vb::cli {

struct ConnectTarget {
	std::string host;
	std::uint16_t port = 27015;
};

// "host", "host:port", "[::1]:port". Port 1-65535.
Status parse_connect_target(std::string_view text, ConnectTarget &out);

// localhost / 127.0.0.0/8 / ::1 / 0.0.0.0
bool is_local_host(std::string_view host);

// The "N" of "(protocol N, ..." in a binary's `--version` output.
std::optional<int> parse_protocol(std::string_view version_output);

// The protocol a managed entry's client or server speaks: from the install
// receipt when it has one, otherwise by running `<binary> --version`.
std::optional<int> entry_protocol(const struct Entry &entry, bool server);

// Empty = compatible or unknown. Otherwise a one-paragraph warning naming the
// server, both versions and what to do.
std::string protocol_warning(const Layout &layout, const struct Entry &client, const ConnectTarget &target);

} // namespace vb::cli
