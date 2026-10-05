#pragma once

#include <memory>
#include <string>

// Injectable HTTPS GET used for OIDC discovery and JWKS fetches (auth.md §5.4)
// so unit tests never touch the network.

namespace vb::auth {

struct HttpResult {
	int status = 0; // 0 = transport failure
	std::string body;
	std::string error; // transport-level description when status == 0
};

class HttpFetcher {
public:
	virtual ~HttpFetcher() = default;
	// Blocking GET. Must bound its own time and response size.
	virtual HttpResult get(const std::string &url) = 0;
};

// libcurl-backed fetcher (system TLS stack). https only, plus http to
// loopback hosts for development; redirects limited; 1 MiB response cap;
// 10 s total timeout. Null when the build has no VB_WITH_AUTH.
std::unique_ptr<HttpFetcher> make_curl_fetcher();

// The URL policy shared by config validation and fetching: https, or http to
// 127.0.0.1/localhost.
bool url_allowed(const std::string &url);

} // namespace vb::auth
