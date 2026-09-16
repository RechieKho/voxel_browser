#pragma once

#include <span>
#include <string>
#include <vector>

#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"

// Chat / UI RPC messages (spec §10.3 player:send_message / player:open_ui).
// Phase 4.2 gave S2CChat/S2COpenUi/C2SUiEvent real codecs so the Lua runtime
// could send them; Phase 5.4 adds C2SChat (client -> server) and wires both
// directions into a HUD chat box.

namespace vb::protocol {

// Sent by a playing client when the player submits a chat line (spec §5.4).
// The server runs `vb.on("chat")` (veto) then, if allowed, broadcasts
// S2CChat{"<name>: <text>"} to every playing connection.
struct C2SChat {
	static constexpr MessageType kType = MessageType::kC2SChat;

	std::string text;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SChat> decode(std::span<const std::byte> in);
};

struct S2CChat {
	static constexpr MessageType kType = MessageType::kS2CChat;

	std::string text;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CChat> decode(std::span<const std::byte> in);
};

// `ctx_json` is the pre-serialized JSON of the Lua ctx table passed to
// player:open_ui(name, ctx); the protocol layer stays data-model agnostic,
// same convention as the opaque chunk payload in S2CChunkAdd.
struct S2COpenUi {
	static constexpr MessageType kType = MessageType::kS2COpenUi;

	std::string ui_name;
	std::string ctx_json;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2COpenUi> decode(std::span<const std::byte> in);
};

// Sent by the client UI VM (Phase 4.5) when a widget's on_click/on_change/
// on_close callback calls ui.send_event(...)/ui.close(). `value_json` is
// "null" for click/close events.
struct C2SUiEvent {
	static constexpr MessageType kType = MessageType::kC2SUiEvent;

	std::string ui_name;
	std::string widget_id;
	std::string event_kind; // "click" | "change" | "close"
	std::string value_json;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SUiEvent> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
