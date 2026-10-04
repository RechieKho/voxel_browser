#include "vb/cli/portcheck.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vb::cli {

bool udp_port_available(std::uint16_t port) {
	if (port == 0) {
		return true;
	}
#if defined(_WIN32)
	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		return true; // can't probe; let the server decide
	}
	const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		WSACleanup();
		return true;
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	const bool ok = bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == 0;
	closesocket(s);
	WSACleanup();
	return ok;
#else
	const int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		return true; // can't probe; let the server decide
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	const bool ok = bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == 0;
	close(s);
	return ok;
#endif
}

} // namespace vb::cli
