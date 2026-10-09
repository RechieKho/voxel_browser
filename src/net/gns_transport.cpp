#include "vb/net/gns_transport.hpp"

#if !VB_WITH_NET

// Stub build: no GameNetworkingSockets linked. Every entry point reports
// kBackendUnavailable so callers built without VB_WITH_NET still link.

namespace vb::net {

struct GnsTransport::Impl {};

GnsTransport::GnsTransport() : impl_(nullptr) {}
GnsTransport::~GnsTransport() = default;

core::Status<core::NetError> GnsTransport::listen(std::uint16_t) {
	return core::Err{ core::NetError::kBackendUnavailable };
}
core::Result<ConnId, core::NetError> GnsTransport::connect(std::string_view,
		std::uint16_t) {
	return core::Err{ core::NetError::kBackendUnavailable };
}
void GnsTransport::send(ConnId, protocol::Lane, std::span<const std::byte>) {}
void GnsTransport::close(ConnId, std::string_view) {}
void GnsTransport::poll(std::vector<TransportEvent> &) {}
bool GnsTransport::is_server() const { return false; }
std::size_t GnsTransport::connection_count() const { return 0; }
std::uint16_t GnsTransport::bound_port() const { return 0; }
std::optional<std::string> GnsTransport::remote_address(ConnId) const {
	return std::nullopt;
}
std::optional<double> GnsTransport::round_trip_time_seconds(ConnId) const {
	return std::nullopt;
}

#if defined(VB_WITH_AUTOMATION)
bool GnsTransport::set_net_sim(const NetSimParams &) {
	return false;
}
#endif

} // namespace vb::net

#else

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include "vb/core/log.hpp"

namespace vb::net {

struct GnsTransport::Impl {
	bool is_server = false;
	HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
	std::uint16_t bound_port = 0;
	std::vector<HSteamNetConnection> owned_conns;
	std::vector<TransportEvent> pending;
};

namespace {

// See kGnsLaneCount: lane 1 (kFeedback) is sent ahead of lane 0. Lanes are
// configured by each sending side for its own outbound traffic, so both the
// accepting server and the connecting client call this.
void configure_lanes(ISteamNetworkingSockets *sockets, HSteamNetConnection conn) {
	const int priorities[kGnsLaneCount] = { 1, 0 }; // lower = sent first
	const EResult result =
			sockets->ConfigureConnectionLanes(conn, kGnsLaneCount, priorities, nullptr);
	if (result != k_EResultOK) {
		VB_WARN("net", "ConfigureConnectionLanes failed (result=", static_cast<int>(result),
				"): block-damage updates will queue behind chunk data");
	}
}

} // namespace

namespace {

// GameNetworkingSockets exposes ONE process-wide interface and ONE global
// connection-status callback (SetGlobalCallback_SteamNetConnectionStatusChanged)
// — there's no per-instance hook. This runtime owns process-wide init/shutdown
// (refcounted: a server + several client GnsTransports, e.g. in one test
// process, share it) and routes each status-change event to the owning
// GnsTransport::Impl via small handle -> owner registries.
class GnsRuntime {
public:
	static GnsRuntime &instance() {
		static GnsRuntime rt;
		return rt;
	}

	void acquire() {
		if (refcount_++ == 0) {
			SteamNetworkingErrMsg err;
			if (!GameNetworkingSockets_Init(nullptr, err)) {
				VB_ERROR("net", "GameNetworkingSockets_Init failed: ", err);
			}
			SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(
					&GnsRuntime::on_status_changed);

			// GNS's per-connection k_ESteamNetworkingConfig_SendBufferSize
			// defaults to 512 KiB (524288 bytes) -- past that,
			// SendMessageToConnection() returns k_EResultLimitExceeded instead
			// of queuing the message, reliable or not. Measured: encoding this
			// engine's *default* production view box (view_distance=8,
			// vertical_view=3 -> 2023 chunks, server.toml.example's defaults)
			// with real WorldGenerator terrain comes to ~545 KB of chunk
			// payload alone -- already over the default limit before the
			// per-message protocol framing overhead or any concurrent entity
			// snapshot traffic on the same connection is even counted. Since
			// the whole box streams in one synchronous
			// WorldReplicator::tick()/broadcast_world() burst on join (nothing
			// paces it across ticks), the default buffer overflows almost
			// immediately, and GnsTransport::send() used to silently discard
			// SendMessageToConnection's result -- this was THE root cause of
			// the "invisible-but-walkable, unbreakable, big rectangular
			// gap" bug (see STATE.md §8): whichever chunks landed after the
			// buffer filled were never actually queued, so they never reached
			// the client, yet WorldReplicator's `last_sent_` already marked
			// them sent and never retried. Raised generously (32 MiB) so a
			// full default-config view box -- and meaningfully larger ones --
			// fits with headroom; send() below now also surfaces a failure
			// loudly if this limit is ever hit again instead of dropping it
			// silently.
			constexpr int32_t kSendBufferBytes = 32 * 1024 * 1024;
			SteamNetworkingUtils()->SetGlobalConfigValueInt32(
					k_ESteamNetworkingConfig_SendBufferSize, kSendBufferBytes);
#if defined(VB_WITH_AUTOMATION)
			apply_net_sim();
#endif
		}
	}
#if defined(VB_WITH_AUTOMATION)
	// GNS's fake-network settings are process-wide and reset when the runtime shuts down,
	// so remember them and re-apply on every init.
	void set_net_sim(const NetSimParams &p) {
		net_sim_ = p;
		if (refcount_ > 0) {
			apply_net_sim();
		}
	}
#endif
	void release() {
		if (--refcount_ == 0) {
			GameNetworkingSockets_Kill();
		}
	}

	void register_listener(HSteamListenSocket sock, GnsTransport::Impl *owner) {
		listen_owners_[sock] = owner;
	}
	void unregister_listener(HSteamListenSocket sock) {
		listen_owners_.erase(sock);
	}
	void register_connection(HSteamNetConnection conn, GnsTransport::Impl *owner) {
		conn_owners_[conn] = owner;
	}
	void unregister_connection(HSteamNetConnection conn) {
		conn_owners_.erase(conn);
	}

private:
	GnsRuntime() = default;

	static void on_status_changed(SteamNetConnectionStatusChangedCallback_t *info) {
		GnsRuntime &rt = instance();
		ISteamNetworkingSockets *sockets = SteamNetworkingSockets();

		// A brand new inbound connection on one of our listen sockets: accept
		// it immediately (no pending-review step in this engine) and start
		// tracking ownership. We don't surface it to the app as kConnected
		// yet -- wait for the real Connected state below, same as the client
		// side, so both ends agree the transport handshake actually finished.
		if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting &&
				info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
			const auto it = rt.listen_owners_.find(info->m_info.m_hListenSocket);
			if (it == rt.listen_owners_.end()) {
				sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
				return;
			}
			GnsTransport::Impl *owner = it->second;
			if (sockets->AcceptConnection(info->m_hConn) != k_EResultOK) {
				sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
				return;
			}
			configure_lanes(sockets, info->m_hConn);
			owner->owned_conns.push_back(info->m_hConn);
			rt.register_connection(info->m_hConn, owner);
			return;
		}

		const auto it = rt.conn_owners_.find(info->m_hConn);
		if (it == rt.conn_owners_.end()) {
			return; // not a connection we (still) own
		}
		GnsTransport::Impl *owner = it->second;

		switch (info->m_info.m_eState) {
			case k_ESteamNetworkingConnectionState_Connected: {
				TransportEvent ev;
				ev.kind = TransportEvent::Kind::kConnected;
				ev.conn = static_cast<ConnId>(info->m_hConn);
				owner->pending.push_back(std::move(ev));
				break;
			}
			case k_ESteamNetworkingConnectionState_ClosedByPeer:
			case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
				TransportEvent ev;
				ev.kind = TransportEvent::Kind::kDisconnected;
				ev.conn = static_cast<ConnId>(info->m_hConn);
				ev.reason = info->m_info.m_szEndDebug;
				owner->pending.push_back(std::move(ev));

				sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
				rt.unregister_connection(info->m_hConn);
				auto &vec = owner->owned_conns;
				vec.erase(std::remove(vec.begin(), vec.end(), info->m_hConn),
						vec.end());
				break;
			}
			default:
				break; // Connecting / FindingRoute / FinWait / Linger / Dead
		}
	}

	int refcount_ = 0;
#if defined(VB_WITH_AUTOMATION)
	NetSimParams net_sim_;

	void apply_net_sim() {
		ISteamNetworkingUtils *u = SteamNetworkingUtils();
		const NetSimParams &p = net_sim_;
		u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, p.lag_ms);
		u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, static_cast<float>(p.loss_pct));
		// Jitter: odds 100%, average jitter_ms, never more than twice that.
		u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct, p.jitter_ms > 0 ? 100.0f : 0.0f);
		u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg, static_cast<float>(p.jitter_ms));
		u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Max, static_cast<float>(2 * p.jitter_ms));
		u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketReorder_Send, static_cast<float>(p.reorder_pct));
		u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketReorder_Time, 20);
		u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketDup_Send, static_cast<float>(p.dup_pct));
		u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketDup_TimeMax, 20);
		VB_INFO("net", "net-sim active: lag=", p.lag_ms, "ms jitter=", p.jitter_ms, "ms loss=", p.loss_pct,
				"% reorder=", p.reorder_pct, "% dup=", p.dup_pct, "%");
	}
#endif
	std::unordered_map<HSteamListenSocket, GnsTransport::Impl *> listen_owners_;
	std::unordered_map<HSteamNetConnection, GnsTransport::Impl *> conn_owners_;
};

// Little-endian peek at the message-type prefix, matching ByteReader/Writer's
// manual byte order (never host-endianness-dependent) — used only to fill in
// TransportEvent::lane for received messages, which nothing downstream
// currently reads but LoopbackTransport sets faithfully too.
protocol::Lane lane_of_frame(const void *data, int size) {
	if (size < 2) {
		return protocol::Lane::kControl;
	}
	const auto *b = static_cast<const unsigned char *>(data);
	const auto type = static_cast<protocol::MessageType>(
			static_cast<std::uint16_t>(b[0]) | (static_cast<std::uint16_t>(b[1]) << 8));
	return protocol::lane_for(type);
}

// Hostname resolution (REMAINING_TASKS.md 1.3 polish):
// SteamNetworkingIPAddr::ParseString() only ever accepts numeric IP literals
// (e.g. "127.0.0.1", "::1") -- it never resolves DNS, so "localhost" or a
// real hostname always failed GnsTransport::connect() before this. getaddrinfo()
// is the portable BSD-sockets resolver (present on Windows/Linux/macOS
// alike); prefers the first IPv4 result, falling back to IPv6, matching
// this project's near-exclusive use of IPv4 literals elsewhere (127.0.0.1
// in every test/example). Returns false (leaves `out` untouched) if
// resolution fails or yields nothing usable.
bool resolve_hostname(const std::string &host, SteamNetworkingIPAddr &out) {
#ifdef _WIN32
	WSADATA wsa_data;
	const bool wsa_ready = WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#endif
	addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	addrinfo *result = nullptr;
	const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
	bool ok = false;
	if (rc == 0 && result != nullptr) {
		const addrinfo *ipv4 = nullptr;
		const addrinfo *ipv6 = nullptr;
		for (const addrinfo *p = result; p != nullptr; p = p->ai_next) {
			if (!ipv4 && p->ai_family == AF_INET) {
				ipv4 = p;
			} else if (!ipv6 && p->ai_family == AF_INET6) {
				ipv6 = p;
			}
		}
		if (const addrinfo *chosen = ipv4 ? ipv4 : ipv6) {
			if (chosen->ai_family == AF_INET) {
				const auto *sin =
						reinterpret_cast<const sockaddr_in *>(chosen->ai_addr);
				out.SetIPv4(ntohl(sin->sin_addr.s_addr), 0);
			} else {
				const auto *sin6 =
						reinterpret_cast<const sockaddr_in6 *>(chosen->ai_addr);
				out.SetIPv6(
						reinterpret_cast<const uint8 *>(&sin6->sin6_addr), 0);
			}
			ok = true;
		}
		freeaddrinfo(result);
	}
#ifdef _WIN32
	if (wsa_ready) {
		WSACleanup();
	}
#endif
	return ok;
}

} // namespace

GnsTransport::GnsTransport() : impl_(std::make_unique<Impl>()) {
	GnsRuntime::instance().acquire();
}

GnsTransport::~GnsTransport() {
	ISteamNetworkingSockets *sockets = SteamNetworkingSockets();
	for (HSteamNetConnection c : impl_->owned_conns) {
		sockets->CloseConnection(c, 0, nullptr, false);
		GnsRuntime::instance().unregister_connection(c);
	}
	if (impl_->listen_socket != k_HSteamListenSocket_Invalid) {
		sockets->CloseListenSocket(impl_->listen_socket);
		GnsRuntime::instance().unregister_listener(impl_->listen_socket);
	}
	GnsRuntime::instance().release();
}

core::Status<core::NetError> GnsTransport::listen(std::uint16_t port) {
	if (impl_->listen_socket != k_HSteamListenSocket_Invalid) {
		return core::Err{ core::NetError::kAlreadyListening };
	}
	impl_->is_server = true;

	SteamNetworkingIPAddr addr;
	addr.Clear();
	addr.m_port = port;

	ISteamNetworkingSockets *sockets = SteamNetworkingSockets();
	impl_->listen_socket = sockets->CreateListenSocketIP(addr, 0, nullptr);
	if (impl_->listen_socket == k_HSteamListenSocket_Invalid) {
		return core::Err{ core::NetError::kBindFailed };
	}
	GnsRuntime::instance().register_listener(impl_->listen_socket, impl_.get());

	SteamNetworkingIPAddr bound;
	if (sockets->GetListenSocketAddress(impl_->listen_socket, &bound)) {
		impl_->bound_port = bound.m_port;
	}
	return {};
}

core::Result<ConnId, core::NetError> GnsTransport::connect(
		std::string_view host, std::uint16_t port) {
	SteamNetworkingIPAddr addr;
	addr.Clear();
	const std::string host_str(host);
	if (!addr.ParseString(host_str.c_str())) {
		// Not a numeric IP literal -- SteamNetworkingIPAddr::ParseString()
		// never resolves DNS on its own (e.g. "localhost", a real
		// hostname), so fall back to a real resolver (Phase 1.3 polish).
		if (!resolve_hostname(host_str, addr)) {
			return core::Err{ core::NetError::kConnectFailed };
		}
	}
	addr.m_port = port;

	ISteamNetworkingSockets *sockets = SteamNetworkingSockets();
	const HSteamNetConnection conn = sockets->ConnectByIPAddress(addr, 0, nullptr);
	if (conn == k_HSteamNetConnection_Invalid) {
		return core::Err{ core::NetError::kConnectFailed };
	}
	configure_lanes(sockets, conn);
	impl_->owned_conns.push_back(conn);
	GnsRuntime::instance().register_connection(conn, impl_.get());
	return static_cast<ConnId>(conn);
}

void GnsTransport::send(ConnId conn, protocol::Lane lane,
		std::span<const std::byte> frame) {
	int flags = send_mode_for_lane(lane) == SendMode::kReliableOrdered
			? k_nSteamNetworkingSend_Reliable
			: k_nSteamNetworkingSend_UnreliableNoNagle;
	if (lane_skips_nagle(lane)) {
		flags |= k_nSteamNetworkingSend_NoNagle;
	}
	// The result used to be discarded entirely. If the send buffer is ever
	// full (k_EResultLimitExceeded) or the connection is otherwise unusable,
	// GNS does not queue the message at all -- reliable does not mean
	// retried if it was never accepted in the first place. That used to be a
	// silent, permanent message loss (root cause of the "invisible but
	// walkable" chunk-streaming bug, see the SendBufferSize comment in
	// GnsRuntime::acquire() and STATE.md §8) with zero trace on either end.
	// Raising the buffer fixes the common case; log loudly if it still ever
	// happens so a recurrence isn't silent again.
	EResult result = k_EResultOK;
	if (const int gns_lane = gns_lane_index(lane); gns_lane == 0) {
		result = SteamNetworkingSockets()->SendMessageToConnection(
				static_cast<HSteamNetConnection>(conn), frame.data(),
				static_cast<std::uint32_t>(frame.size()), flags, nullptr);
	} else {
		// Only SendMessages() takes a lane index.
		SteamNetworkingMessage_t *msg =
				SteamNetworkingUtils()->AllocateMessage(static_cast<int>(frame.size()));
		std::memcpy(msg->m_pData, frame.data(), frame.size());
		msg->m_conn = static_cast<HSteamNetConnection>(conn);
		msg->m_nFlags = flags;
		msg->m_idxLane = static_cast<std::uint16_t>(gns_lane);
		int64 number_or_result = 0; // GNS's int64 (long long), not std::int64_t
		SteamNetworkingSockets()->SendMessages(1, &msg, &number_or_result, /*bDeleteFailedMessages*/ true);
		if (number_or_result < 0) {
			result = static_cast<EResult>(-number_or_result);
		}
	}
	if (result != k_EResultOK) {
		VB_ERROR("net", "SendMessageToConnection failed (result=",
				static_cast<int>(result), ", ", frame.size(),
				" bytes, lane=", static_cast<int>(lane), "): message dropped");
	}
}

void GnsTransport::close(ConnId conn, std::string_view reason) {
	const auto hconn = static_cast<HSteamNetConnection>(conn);
	auto &vec = impl_->owned_conns;
	const auto it = std::find(vec.begin(), vec.end(), hconn);
	if (it == vec.end()) {
		return;
	}
	const std::string reason_str(reason);
	SteamNetworkingSockets()->CloseConnection(hconn, 0,
			reason_str.empty() ? nullptr : reason_str.c_str(), false);
	GnsRuntime::instance().unregister_connection(hconn);
	vec.erase(it);

	// CloseConnection() invalidates the handle immediately on our side and
	// does not fire our own status-changed callback for our own close -- only
	// the *other* end learns about it that way. Synthesize the local
	// disconnect event ourselves (LoopbackTransport does the same on close()).
	TransportEvent ev;
	ev.kind = TransportEvent::Kind::kDisconnected;
	ev.conn = conn;
	ev.reason = reason_str;
	impl_->pending.push_back(std::move(ev));
}

void GnsTransport::poll(std::vector<TransportEvent> &out) {
	ISteamNetworkingSockets *sockets = SteamNetworkingSockets();
	sockets->RunCallbacks(); // synchronously dispatches into impl_->pending

	for (auto &ev : impl_->pending) {
		out.push_back(std::move(ev));
	}
	impl_->pending.clear();

	SteamNetworkingMessage_t *msgs[32];
	for (HSteamNetConnection c : impl_->owned_conns) {
		for (;;) {
			const int n = sockets->ReceiveMessagesOnConnection(c, msgs, 32);
			if (n <= 0) {
				break;
			}
			for (int i = 0; i < n; ++i) {
				TransportEvent ev;
				ev.kind = TransportEvent::Kind::kMessage;
				ev.conn = static_cast<ConnId>(c);
				ev.lane = lane_of_frame(msgs[i]->m_pData, msgs[i]->m_cbSize);
				const auto *bytes =
						static_cast<const std::byte *>(msgs[i]->m_pData);
				ev.frame.assign(bytes, bytes + msgs[i]->m_cbSize);
				msgs[i]->Release();
				out.push_back(std::move(ev));
			}
			if (n < 32) {
				break;
			}
		}
	}
}

bool GnsTransport::is_server() const { return impl_->is_server; }

std::size_t GnsTransport::connection_count() const {
	return impl_->owned_conns.size();
}

std::uint16_t GnsTransport::bound_port() const { return impl_->bound_port; }

std::optional<std::string> GnsTransport::remote_address(ConnId conn) const {
	SteamNetConnectionInfo_t info;
	if (!SteamNetworkingSockets()->GetConnectionInfo(
				static_cast<HSteamNetConnection>(conn), &info)) {
		return std::nullopt;
	}
	char buf[SteamNetworkingIPAddr::k_cchMaxString];
	info.m_addrRemote.ToString(buf, sizeof(buf), /*bWithPort=*/false);
	return std::string(buf);
}

// REMAINING_TASKS.md Phase 3's "wall-clock server_time_est + smoothing"
// gap: real one-way network latency (half of this) is what makes that
// estimate meaningfully different from just "elapsed local time since the
// last packet" -- LoopbackTransport has nothing to report here (its
// default kept at 0). `m_nPing` is a running average GNS itself already
// maintains from real packet round trips, in milliseconds; negative
// (unmeasured yet, or an invalid/closed connection) reports as unknown
// rather than a nonsensical negative duration.
std::optional<double> GnsTransport::round_trip_time_seconds(ConnId conn) const {
	SteamNetConnectionRealTimeStatus_t status;
	const EResult result = SteamNetworkingSockets()->GetConnectionRealTimeStatus(
			static_cast<HSteamNetConnection>(conn), &status, 0, nullptr);
	if (result != k_EResultOK || status.m_nPing < 0) {
		return std::nullopt;
	}
	return static_cast<double>(status.m_nPing) / 1000.0;
}

#if defined(VB_WITH_AUTOMATION)
bool GnsTransport::set_net_sim(const NetSimParams &params) {
	GnsRuntime::instance().set_net_sim(params);
	return true;
}
#endif

} // namespace vb::net

#endif // VB_WITH_NET
