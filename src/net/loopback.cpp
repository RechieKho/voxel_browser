#include "vb/net/loopback.hpp"

#include <algorithm>
#include <utility>

namespace vb::net {

struct LoopbackNetwork::Hub {
	struct Link {
		ConnId id = ConnId::kInvalid;
		std::deque<TransportEvent> to_server;
		std::deque<TransportEvent> to_client;
		bool closed = false;
	};

	bool listening = false;
	std::uint64_t next_id = 1;
	std::vector<std::unique_ptr<Link>> links;

	Link *find(ConnId id) {
		for (auto &l : links) {
			if (l->id == id) {
				return l.get();
			}
		}
		return nullptr;
	}
};

namespace {

using Hub = LoopbackNetwork::Hub;

TransportEvent make_event(TransportEvent::Kind kind, ConnId conn) {
	TransportEvent ev;
	ev.kind = kind;
	ev.conn = conn;
	return ev;
}

class LoopbackTransport final : public Transport {
public:
	LoopbackTransport(std::shared_ptr<Hub> hub, bool server) : hub_(std::move(hub)), server_(server) {}

	core::Status<core::NetError> listen(std::uint16_t) override {
		if (!server_) {
			return core::Err{ core::NetError::kConnectFailed };
		}
		if (hub_->listening) {
			return core::Err{ core::NetError::kAlreadyListening };
		}
		hub_->listening = true;
		return {};
	}

	core::Result<ConnId, core::NetError> connect(std::string_view,
			std::uint16_t) override {
		if (server_) {
			return core::Err{ core::NetError::kConnectFailed };
		}
		if (!hub_->listening) {
			return core::Err{ core::NetError::kConnectFailed };
		}
		auto link = std::make_unique<Hub::Link>();
		link->id = static_cast<ConnId>(hub_->next_id++);
		const ConnId id = link->id;
		link->to_client.push_back(
				make_event(TransportEvent::Kind::kConnected, id));
		link->to_server.push_back(
				make_event(TransportEvent::Kind::kConnected, id));
		hub_->links.push_back(std::move(link));
		owned_.push_back(id);
		return id;
	}

	void send(ConnId conn, protocol::Lane lane,
			std::span<const std::byte> frame) override {
		Hub::Link *link = hub_->find(conn);
		if (link == nullptr || link->closed) {
			return;
		}
		TransportEvent ev = make_event(TransportEvent::Kind::kMessage, conn);
		ev.lane = lane;
		ev.frame.assign(frame.begin(), frame.end());
		(server_ ? link->to_client : link->to_server).push_back(std::move(ev));
	}

	void close(ConnId conn, std::string_view reason) override {
		Hub::Link *link = hub_->find(conn);
		if (link == nullptr || link->closed) {
			return;
		}
		link->closed = true;
		TransportEvent a = make_event(TransportEvent::Kind::kDisconnected, conn);
		a.reason = std::string(reason);
		TransportEvent b = a;
		link->to_server.push_back(std::move(a));
		link->to_client.push_back(std::move(b));
	}

	void poll(std::vector<TransportEvent> &out) override {
		for (auto &link : hub_->links) {
			if (!server_ && std::find(owned_.begin(), owned_.end(), link->id) == owned_.end()) {
				continue;
			}
			std::deque<TransportEvent> &q = server_ ? link->to_server
													: link->to_client;
			while (!q.empty()) {
				out.push_back(std::move(q.front()));
				q.pop_front();
			}
		}
	}

	bool is_server() const override { return server_; }

	std::size_t connection_count() const override {
		std::size_t n = 0;
		for (const auto &link : hub_->links) {
			if (link->closed) {
				continue;
			}
			if (server_ || std::find(owned_.begin(), owned_.end(), link->id) != owned_.end()) {
				++n;
			}
		}
		return n;
	}

private:
	std::shared_ptr<Hub> hub_;
	bool server_;
	std::vector<ConnId> owned_;
};

} // namespace

LoopbackNetwork::LoopbackNetwork() : hub_(std::make_shared<Hub>()),
									 server_(std::make_unique<LoopbackTransport>(hub_, true)) {}

Transport &LoopbackNetwork::server() { return *server_; }

Transport &LoopbackNetwork::create_client() {
	clients_.push_back(std::make_unique<LoopbackTransport>(hub_, false));
	return *clients_.back();
}

} // namespace vb::net
