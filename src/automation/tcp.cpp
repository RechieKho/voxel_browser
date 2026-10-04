// TCP transport for the automation Host (docs/e2e-automation.md §3.1): the same JSON-lines
// protocol as stdio, over a loopback-only, token-authenticated socket. Development builds
// only (VB_WITH_AUTOMATION).
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <streambuf>
#include <string>

#if defined(_WIN32)
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <csignal>
#endif

#include "vb/automation/host.hpp"

namespace vb::automation {

using nlohmann::json;

namespace {

#if defined(_WIN32)
using Sock = SOCKET;
constexpr Sock kBadSock = INVALID_SOCKET;
void close_sock(Sock s) { closesocket(s); }
using SockLen = int;
#else
using Sock = int;
constexpr Sock kBadSock = -1;
void close_sock(Sock s) { ::close(s); }
using SockLen = socklen_t;
#endif

void set_recv_timeout(Sock s, int seconds) {
#if defined(_WIN32)
	DWORD ms = static_cast<DWORD>(seconds) * 1000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&ms), sizeof ms);
#else
	timeval tv{};
	tv.tv_sec = seconds;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
}

std::string random_token() {
	std::random_device rd;
	static const char *hex = "0123456789abcdef";
	std::string t;
	for (int i = 0; i < 32; ++i) {
		t += hex[rd() % 16];
	}
	return t;
}

// Compares in time independent of where the strings first differ.
bool token_equal(const std::string &a, const std::string &b) {
	unsigned diff = static_cast<unsigned>(a.size() ^ b.size());
	for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
		diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
	}
	return diff == 0;
}

// The listener plus the single current connection. Shared by the input and output
// streambufs below; lives for the whole process (a dev tool has one of these).
class TcpChannel {
public:
	bool listen_loopback(int port, std::string &error) {
#if defined(_WIN32)
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			error = "WSAStartup failed";
			return false;
		}
#else
		std::signal(SIGPIPE, SIG_IGN); // a vanished peer must not kill the game
#endif
		listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
		if (listen_ == kBadSock) {
			error = "socket() failed";
			return false;
		}
		const int one = 1;
		setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof one);
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only, by construction
		addr.sin_port = htons(static_cast<unsigned short>(port));
		if (::bind(listen_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 || ::listen(listen_, 4) != 0) {
			error = "could not listen on 127.0.0.1:" + std::to_string(port);
			close_sock(listen_);
			listen_ = kBadSock;
			return false;
		}
		SockLen len = sizeof addr;
		getsockname(listen_, reinterpret_cast<sockaddr *>(&addr), &len);
		port_ = ntohs(addr.sin_port);
		return true;
	}

	void set_token(std::string t) { token_ = std::move(t); }
	int port() const { return port_; }
	const std::string &token() const { return token_; }

	// Blocks until some payload bytes (after authentication) are available. Never reports
	// end-of-input just because a client left: it goes back to accepting. 0 = fatal.
	long read_some(char *buf, std::size_t cap) {
		for (;;) {
			if (!leftover_.empty()) {
				const std::size_t n = std::min(cap, leftover_.size());
				std::memcpy(buf, leftover_.data(), n);
				leftover_.erase(0, n);
				return static_cast<long>(n);
			}
			Sock c = current();
			if (c == kBadSock) {
				if (accept_and_authenticate() < 0) {
					return 0; // the listening socket itself failed: give up
				}
				continue;
			}
			const auto n = ::recv(c, buf, static_cast<int>(cap), 0);
			if (n <= 0) {
				drop_current(); // the client went away: wait for the next one
				continue;
			}
			return static_cast<long>(n);
		}
	}

	// Output goes to the current client, if any; with none attached it is dropped.
	void write_all(const char *s, std::size_t n) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (conn_ == kBadSock) {
			return;
		}
		std::size_t done = 0;
		while (done < n) {
			const auto w = ::send(conn_, s + done, static_cast<int>(n - done), 0);
			if (w <= 0) {
				// Wake the reader (blocked in recv on this fd) so it drops the connection.
#if defined(_WIN32)
				::shutdown(conn_, SD_BOTH);
#else
				::shutdown(conn_, SHUT_RDWR);
#endif
				return;
			}
			done += static_cast<std::size_t>(w);
		}
	}

private:
	Sock current() {
		std::lock_guard<std::mutex> lock(mutex_);
		return conn_;
	}
	void drop_current() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (conn_ != kBadSock) {
			close_sock(conn_);
			conn_ = kBadSock;
		}
	}
	void send_line(Sock c, const json &j) {
		const std::string s = j.dump() + "\n";
		::send(c, s.data(), static_cast<int>(s.size()), 0);
	}

	// 1 = a client authenticated, 0 = a connection was turned away (try again), -1 = fatal.
	int accept_and_authenticate() {
		const Sock c = ::accept(listen_, nullptr, nullptr);
		if (c == kBadSock) {
#if defined(_WIN32)
			return WSAGetLastError() == WSAECONNRESET ? 0 : -1;
#else
			return (errno == EINTR || errno == ECONNABORTED) ? 0 : -1;
#endif
		}
		// A connection that never authenticates must not wedge the (single) accept loop.
		set_recv_timeout(c, 5);
		std::string pending;
		char chunk[1024];
		while (pending.find('\n') == std::string::npos && pending.size() < 4096) {
			const auto n = ::recv(c, chunk, sizeof chunk, 0);
			if (n <= 0) {
				close_sock(c);
				return 0;
			}
			pending.append(chunk, static_cast<std::size_t>(n));
		}
		const std::size_t nl = pending.find('\n');
		bool ok = false;
		if (nl != std::string::npos) {
			const json first = json::parse(pending.substr(0, nl), nullptr, false);
			ok = first.is_object() && first.value("cmd", std::string()) == "auth" && first.contains("args") &&
					first["args"].is_object() && first["args"].value("token", json()).is_string() &&
					token_equal(first["args"]["token"].get<std::string>(), token_);
		}
		if (!ok) {
			send_line(c, json{ { "ok", false }, { "error", { { "code", "unauthorized" }, { "message", "bad or missing token" } } } });
			close_sock(c);
			return 0;
		}
		set_recv_timeout(c, 0); // 0 = no timeout again
		send_line(c, json{ { "ok", true }, { "result", { { "authenticated", true } } } });
		leftover_ = pending.substr(nl + 1);
		std::lock_guard<std::mutex> lock(mutex_);
		conn_ = c;
		return 1;
	}

	Sock listen_ = kBadSock;
	Sock conn_ = kBadSock;
	std::mutex mutex_;
	std::string token_;
	std::string leftover_; // bytes that arrived in the same packet as the auth line
	int port_ = 0;
};

class TcpInBuf final : public std::streambuf {
public:
	explicit TcpInBuf(TcpChannel &ch) :
			ch_(ch) {}

protected:
	int_type underflow() override {
		if (gptr() < egptr()) {
			return traits_type::to_int_type(*gptr());
		}
		const long n = ch_.read_some(buf_, sizeof buf_);
		if (n <= 0) {
			return traits_type::eof();
		}
		setg(buf_, buf_, buf_ + n);
		return traits_type::to_int_type(*gptr());
	}

private:
	TcpChannel &ch_;
	char buf_[4096];
};

class TcpOutBuf final : public std::streambuf {
public:
	explicit TcpOutBuf(TcpChannel &ch) :
			ch_(ch) {}

protected:
	std::streamsize xsputn(const char *s, std::streamsize n) override {
		ch_.write_all(s, static_cast<std::size_t>(n));
		return n;
	}
	int_type overflow(int_type c) override {
		if (!traits_type::eq_int_type(c, traits_type::eof())) {
			const char ch = traits_type::to_char_type(c);
			ch_.write_all(&ch, 1);
		}
		return traits_type::not_eof(c);
	}

private:
	TcpChannel &ch_;
};

struct TcpHolder {
	TcpChannel channel;
	TcpInBuf in_buf{ channel };
	TcpOutBuf out_buf{ channel };
	std::istream in{ &in_buf };
	std::ostream out{ &out_buf };
};

} // namespace

std::unique_ptr<Host> Host::open_tcp(const TcpOptions &options, std::string &error) {
	if (options.port < 0 || options.port > 65535) {
		error = "port must be 0..65535";
		return nullptr;
	}
	// Intentionally leaked, like the stdin holder: the reader thread may still be blocked in
	// accept()/recv() when the process exits.
	auto *holder = new TcpHolder();
	if (!holder->channel.listen_loopback(options.port, error)) {
		delete holder;
		return nullptr;
	}
	holder->channel.set_token(options.token.empty() ? random_token() : options.token);

	const json info{ { "host", "127.0.0.1" }, { "port", holder->channel.port() },
		{ "token", holder->channel.token() }, { "pid", static_cast<long>(
#if defined(_WIN32)
															   _getpid()
#else
															   ::getpid()
#endif
																	   ) } };
	if (!options.info_path.empty()) {
		const std::filesystem::path p(options.info_path);
		{
			std::ofstream f(p, std::ios::trunc);
			if (!f) {
				error = "cannot write --automation-info file '" + options.info_path + "'";
				return nullptr;
			}
		}
		// The token is a credential: owner-only before the secret is written into it.
		std::error_code ec;
		std::filesystem::permissions(p, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
				std::filesystem::perm_options::replace, ec);
		std::ofstream f(p, std::ios::trunc);
		f << info.dump() << '\n';
	} else {
		std::cerr << "automation: listening on 127.0.0.1:" << holder->channel.port()
				  << " token=" << holder->channel.token() << '\n';
	}
	return std::make_unique<Host>(holder->in, holder->out);
}

std::unique_ptr<Host> Host::open_spec(const std::string &spec, const std::string &token,
		const std::string &info_path, std::string &error) {
	if (spec == "stdio") {
		auto h = open_stdio();
		if (!h) {
			error = "could not set up the automation channel";
		}
		return h;
	}
	if (spec == "tcp" || spec.rfind("tcp:", 0) == 0) {
		TcpOptions o;
		o.token = token;
		o.info_path = info_path;
		if (spec == "tcp:") {
			error = "missing port after 'tcp:' in --automation (use 'tcp' for any free port)";
			return nullptr;
		}
		if (spec.size() > 4) {
			char *end = nullptr;
			const long p = std::strtol(spec.c_str() + 4, &end, 10);
			if (end == spec.c_str() + 4 || *end != '\0') {
				error = "bad port in --automation " + spec;
				return nullptr;
			}
			o.port = static_cast<int>(p);
		}
		return open_tcp(o, error);
	}
	error = "--automation must be 'stdio' or 'tcp[:PORT]'";
	return nullptr;
}

} // namespace vb::automation
