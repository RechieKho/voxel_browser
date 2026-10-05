#include "vb/auth/http.hpp"

#include <string_view>

#if defined(VB_WITH_AUTH)
#include <curl/curl.h>
#endif

namespace vb::auth {

bool url_allowed(const std::string &url) {
	constexpr std::string_view https = "https://";
	if (url.size() > https.size() && url.compare(0, https.size(), https) == 0) {
		return true;
	}
	for (const std::string_view prefix :
			{ "http://127.0.0.1", "http://localhost" }) {
		if (url.size() >= prefix.size() && url.compare(0, prefix.size(), prefix) == 0) {
			const char next = url.size() > prefix.size() ? url[prefix.size()] : '/';
			if (next == '/' || next == ':' || next == '?') {
				return true;
			}
		}
	}
	return false;
}

#if defined(VB_WITH_AUTH)

namespace {

constexpr std::size_t kMaxResponseBytes = 1024 * 1024;

std::size_t write_cb(char *ptr, std::size_t size, std::size_t nmemb, void *user) {
	auto *body = static_cast<std::string *>(user);
	const std::size_t n = size * nmemb;
	if (body->size() + n > kMaxResponseBytes) {
		return 0; // abort the transfer: oversize response
	}
	body->append(ptr, n);
	return n;
}

class CurlFetcher final : public HttpFetcher {
public:
	CurlFetcher() { curl_global_init(CURL_GLOBAL_DEFAULT); }
	HttpResult get(const std::string &url) override { return perform(url, nullptr, {}); }
	HttpResult post(const std::string &url, const std::string &content_type,
			const std::string &body) override {
		return perform(url, &content_type, body);
	}

private:
	HttpResult perform(const std::string &url, const std::string *content_type,
			const std::string &body) {
		HttpResult out;
		if (!url_allowed(url)) {
			out.error = "url not allowed (https required)";
			return out;
		}
		CURL *c = curl_easy_init();
		if (c == nullptr) {
			out.error = "curl init failed";
			return out;
		}
		curl_slist *headers = nullptr;
		if (content_type != nullptr) {
			headers = curl_slist_append(headers, ("Content-Type: " + *content_type).c_str());
			curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(c, CURLOPT_POST, 1L);
			curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
			curl_easy_setopt(c, CURLOPT_COPYPOSTFIELDS, body.c_str());
		}
		curl_easy_setopt(c, CURLOPT_URL, url.c_str());
		curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, content_type == nullptr ? 1L : 0L);
		curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
		curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https,http");
		curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
		curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
		curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
		curl_easy_setopt(c, CURLOPT_WRITEDATA, &out.body);
		curl_easy_setopt(c, CURLOPT_USERAGENT, "voxel_browser-auth/1");
		curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
		const CURLcode rc = curl_easy_perform(c);
		if (rc != CURLE_OK) {
			out.error = curl_easy_strerror(rc);
			out.body.clear();
		} else {
			long code = 0;
			curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
			out.status = static_cast<int>(code);
		}
		curl_slist_free_all(headers);
		curl_easy_cleanup(c);
		return out;
	}
};

} // namespace

std::unique_ptr<HttpFetcher> make_curl_fetcher() {
	return std::make_unique<CurlFetcher>();
}

#else

std::unique_ptr<HttpFetcher> make_curl_fetcher() { return nullptr; }

#endif

} // namespace vb::auth
