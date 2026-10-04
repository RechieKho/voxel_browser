#include "vb/cli/source.hpp"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

#include <algorithm>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "vb/cli/version.hpp"

namespace vb::cli {

namespace fs = std::filesystem;

namespace {

constexpr const char *kDefaultSource = "RechieKho/voxel_browser";
constexpr std::size_t kMaxManifestBytes = 1u << 20;

// ---------------------------------------------------------------- DirSource

class DirSource final : public Source {
public:
	explicit DirSource(fs::path dir) : dir_(std::move(dir)) {}

	Status fetch_manifest(std::string_view version, std::string &out) override {
		std::ifstream in(dir_ / "release.toml", std::ios::binary);
		if (!in) {
			return { "no release.toml in " + dir_.string() };
		}
		std::stringstream ss;
		ss << in.rdbuf();
		out = ss.str();
		if (version != "latest") {
			// A directory holds exactly one release: make sure it is the asked one.
			toml::table t;
			try {
				t = toml::parse(out);
			} catch (const toml::parse_error &) {
				return {}; // the caller's parse_manifest reports it properly
			}
			const auto have = t["version"].value_or(std::string());
			if (have != version) {
				return { "version " + std::string(version) + " is not available from " +
						describe() + " (it has " + have + ")" };
			}
		}
		return {};
	}

	Status fetch_file(std::string_view, const std::string &file, const fs::path &dest,
			const ProgressFn &progress) override {
		const fs::path src = dir_ / file;
		std::error_code ec;
		const std::uint64_t size = fs::is_regular_file(src, ec) ? fs::file_size(src, ec) : 0;
		std::ifstream in(src, std::ios::binary);
		if (!in) {
			return { "missing " + src.string() };
		}
		fs::create_directories(dest.parent_path(), ec);
		std::ofstream out(dest, std::ios::binary | std::ios::trunc);
		char buf[1 << 16];
		std::uint64_t done = 0;
		while (in) {
			in.read(buf, sizeof(buf));
			const auto n = in.gcount();
			out.write(buf, n);
			done += static_cast<std::uint64_t>(n);
			if (progress) {
				progress(done, size);
			}
		}
		out.flush();
		if (!out) {
			return { "cannot write " + dest.string() };
		}
		return {};
	}

	Status fetch_signature(std::string_view, std::string &out) override {
		out.clear();
		std::ifstream in(dir_ / "release.toml.sig", std::ios::binary);
		if (!in) {
			return {};
		}
		std::stringstream ss;
		ss << in.rdbuf();
		out = ss.str();
		return {};
	}

	Status list_versions(std::vector<std::string> &out) override {
		std::string text;
		if (const Status s = fetch_manifest("latest", text); !s) {
			return s;
		}
		try {
			const toml::table t = toml::parse(text);
			if (const auto v = t["version"].value<std::string>()) {
				out.push_back(*v);
			}
		} catch (const toml::parse_error &) {
			return { "release.toml in " + dir_.string() + " is not valid TOML" };
		}
		return {};
	}

	std::string describe() const override { return "dir:" + dir_.string(); }

private:
	fs::path dir_;
};

// --------------------------------------------------------------- HttpSource

void curl_init_once() {
	static std::once_flag flag;
	std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct CurlHandle {
	CURL *h;
	CurlHandle() : h(curl_easy_init()) {}
	~CurlHandle() {
		if (h != nullptr) {
			curl_easy_cleanup(h);
		}
	}
};

struct MemSink {
	std::string data;
	bool overflow = false;
	std::size_t limit = kMaxManifestBytes;
};

std::size_t mem_write(char *p, std::size_t sz, std::size_t n, void *ud) {
	auto *s = static_cast<MemSink *>(ud);
	if (s->data.size() + sz * n > s->limit) {
		s->overflow = true;
		return 0;
	}
	s->data.append(p, sz * n);
	return sz * n;
}

struct FileSink {
	CURL *curl = nullptr;
	fs::path path;
	std::uint64_t resume_from = 0;
	std::ofstream out;
	bool opened = false;
};

std::size_t file_write(char *p, std::size_t sz, std::size_t n, void *ud) {
	auto *s = static_cast<FileSink *>(ud);
	if (!s->opened) {
		long code = 0;
		curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &code);
		// Server honoured Range (206) -> append; ignored it (200) -> start over.
		const bool append = s->resume_from > 0 && code == 206;
		s->out.open(s->path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
		s->opened = true;
	}
	s->out.write(p, static_cast<std::streamsize>(sz * n));
	return s->out ? sz * n : 0;
}

struct ProgressCtx {
	const ProgressFn *fn;
	std::uint64_t offset;
};

int xfer_progress(void *ud, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
	const auto *c = static_cast<const ProgressCtx *>(ud);
	if (c->fn != nullptr && *c->fn) {
		(*c->fn)(c->offset + static_cast<std::uint64_t>(now),
				total > 0 ? c->offset + static_cast<std::uint64_t>(total) : 0);
	}
	return 0;
}

void common_options(CURL *h, bool allow_http) {
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(h, CURLOPT_USERAGENT, "vb-cli");
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, allow_http ? "http,https" : "https");
	curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	(void)allow_http;
#endif
	// Proxy settings (HTTPS_PROXY etc.) are honoured by libcurl itself.
}

class HttpSource final : public Source {
public:
	HttpSource(std::string repo, std::string base) : repo_(std::move(repo)), base_(std::move(base)) {
		while (!base_.empty() && base_.back() == '/') {
			base_.pop_back();
		}
		curl_init_once();
	}

	Status fetch_manifest(std::string_view version, std::string &out) override {
		CurlHandle c;
		if (c.h == nullptr) {
			return { "curl init failed" };
		}
		MemSink sink;
		const std::string url = release_url(version, "release.toml");
		common_options(c.h, allow_http());
		curl_easy_setopt(c.h, CURLOPT_URL, url.c_str());
		curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, mem_write);
		curl_easy_setopt(c.h, CURLOPT_WRITEDATA, &sink);
		char err[CURL_ERROR_SIZE] = {};
		curl_easy_setopt(c.h, CURLOPT_ERRORBUFFER, err);
		const CURLcode rc = curl_easy_perform(c.h);
		if (rc != CURLE_OK) {
			long code = 0;
			curl_easy_getinfo(c.h, CURLINFO_RESPONSE_CODE, &code);
			if (code == 404) {
				return { "release " + std::string(version) + " not found at " + url };
			}
			return { "cannot fetch " + url + ": " + (sink.overflow ? "manifest too large" : (err[0] != 0 ? err : curl_easy_strerror(rc))) };
		}
		out = std::move(sink.data);
		return {};
	}

	Status fetch_file(std::string_view version, const std::string &file, const fs::path &dest,
			const ProgressFn &progress) override {
		CurlHandle c;
		if (c.h == nullptr) {
			return { "curl init failed" };
		}
		std::error_code ec;
		fs::create_directories(dest.parent_path(), ec);
		FileSink sink;
		sink.curl = c.h;
		sink.path = dest;
		sink.resume_from = fs::is_regular_file(dest, ec) ? fs::file_size(dest, ec) : 0;
		ProgressCtx pctx{ &progress, sink.resume_from };

		const std::string url = release_url(version, file);
		common_options(c.h, allow_http());
		curl_easy_setopt(c.h, CURLOPT_URL, url.c_str());
		curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, file_write);
		curl_easy_setopt(c.h, CURLOPT_WRITEDATA, &sink);
		curl_easy_setopt(c.h, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(c.h, CURLOPT_XFERINFOFUNCTION, xfer_progress);
		curl_easy_setopt(c.h, CURLOPT_XFERINFODATA, &pctx);
		if (sink.resume_from > 0) {
			curl_easy_setopt(c.h, CURLOPT_RESUME_FROM_LARGE,
					static_cast<curl_off_t>(sink.resume_from));
		}
		char err[CURL_ERROR_SIZE] = {};
		curl_easy_setopt(c.h, CURLOPT_ERRORBUFFER, err);
		const CURLcode rc = curl_easy_perform(c.h);
		sink.out.close();
		if (rc == CURLE_HTTP_RETURNED_ERROR) {
			long code = 0;
			curl_easy_getinfo(c.h, CURLINFO_RESPONSE_CODE, &code);
			if (code == 416) {
				return {}; // .part already complete; the caller verifies the hash
			}
		}
		if (rc != CURLE_OK) {
			return { "download of " + url + " failed: " +
					(err[0] != 0 ? err : curl_easy_strerror(rc)) +
					" (re-run to resume)" };
		}
		return {};
	}

	Status fetch_signature(std::string_view version, std::string &out) override {
		out.clear();
		CurlHandle c;
		if (c.h == nullptr) {
			return { "curl init failed" };
		}
		MemSink sink;
		sink.limit = 4096; // a base64 Ed25519 signature is 88 bytes
		const std::string url = release_url(version, "release.toml.sig");
		common_options(c.h, allow_http());
		curl_easy_setopt(c.h, CURLOPT_URL, url.c_str());
		curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, mem_write);
		curl_easy_setopt(c.h, CURLOPT_WRITEDATA, &sink);
		char err[CURL_ERROR_SIZE] = {};
		curl_easy_setopt(c.h, CURLOPT_ERRORBUFFER, err);
		const CURLcode rc = curl_easy_perform(c.h);
		if (rc != CURLE_OK) {
			long code = 0;
			curl_easy_getinfo(c.h, CURLINFO_RESPONSE_CODE, &code);
			if (code == 404) {
				return {}; // an unsigned release
			}
			return { "cannot fetch " + url + ": " +
				(sink.overflow ? "signature too large" : (err[0] != 0 ? err : curl_easy_strerror(rc))) };
		}
		out = std::move(sink.data);
		return {};
	}

	Status list_versions(std::vector<std::string> &out) override {
		CurlHandle c;
		if (c.h == nullptr) {
			return { "curl init failed" };
		}
		// github.com serves its API from a different host; any other base is
		// taken to be GitHub Enterprise-shaped (<base>/api/v3).
		const std::string api = base_ == "https://github.com" ? "https://api.github.com"
															  : base_ + "/api/v3";
		const std::string url = api + "/repos/" + repo_ + "/releases?per_page=100";
		MemSink sink;
		sink.limit = 8u << 20; // a page of 100 releases with notes is a few MB at most
		common_options(c.h, allow_http());
		curl_slist *headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
		curl_easy_setopt(c.h, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(c.h, CURLOPT_URL, url.c_str());
		curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, mem_write);
		curl_easy_setopt(c.h, CURLOPT_WRITEDATA, &sink);
		char err[CURL_ERROR_SIZE] = {};
		curl_easy_setopt(c.h, CURLOPT_ERRORBUFFER, err);
		const CURLcode rc = curl_easy_perform(c.h);
		curl_slist_free_all(headers);
		if (rc != CURLE_OK) {
			long code = 0;
			curl_easy_getinfo(c.h, CURLINFO_RESPONSE_CODE, &code);
			std::string why = code == 403 || code == 429
					? "rate limited by the API (try again later)"
					: (sink.overflow ? "response too large"
									 : (err[0] != 0 ? err : curl_easy_strerror(rc)));
			return { "cannot list releases from " + url + ": " + why };
		}
		return parse_release_list(sink.data, out);
	}

	std::string describe() const override { return base_ + "/" + repo_; }

private:
	bool allow_http() const { return base_.rfind("http://", 0) == 0; }

	std::string release_url(std::string_view version, std::string_view file) const {
		std::string u = base_ + "/" + repo_ + "/releases/";
		if (version == "latest") {
			u += "latest/download/";
		} else {
			u += "download/" + std::string(version) + "/";
		}
		return u + std::string(file);
	}

	std::string repo_;
	std::string base_;
};

bool valid_repo(std::string_view s) {
	const std::size_t slash = s.find('/');
	if (slash == std::string_view::npos || slash == 0 || slash + 1 >= s.size() ||
			s.find('/', slash + 1) != std::string_view::npos) {
		return false;
	}
	for (const char c : s) {
		const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
				c == '.' || c == '/';
		if (!ok) {
			return false;
		}
	}
	return true;
}

} // namespace

Status parse_release_list(std::string_view json, std::vector<std::string> &out) {
	nlohmann::json doc = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
	if (doc.is_discarded() || !doc.is_array()) {
		return { "unexpected response from the release API (not a JSON list)" };
	}
	std::vector<Version> found;
	for (const nlohmann::json &rel : doc) {
		if (!rel.is_object() || rel.value("draft", false) || rel.value("prerelease", false)) {
			continue;
		}
		const std::string tag = rel.value("tag_name", std::string());
		if (const auto v = parse_version(tag); v && std::find(found.begin(), found.end(), *v) == found.end()) {
			found.push_back(*v);
		}
	}
	std::sort(found.begin(), found.end(), [](const Version &a, const Version &b) { return b < a; });
	for (const Version &v : found) {
		out.push_back(to_tag(v));
	}
	return {};
}

std::unique_ptr<Source> make_dir_source(fs::path dir) {
	return std::make_unique<DirSource>(std::move(dir));
}

std::unique_ptr<Source> make_http_source(std::string repo, std::string base_url) {
	return std::make_unique<HttpSource>(std::move(repo), std::move(base_url));
}

std::unique_ptr<Source> make_source(std::string_view spec, std::string *error) {
	if (spec.rfind("dir:", 0) == 0) {
		if (spec.size() == 4) {
			if (error != nullptr) {
				*error = "empty directory in source 'dir:'";
			}
			return nullptr;
		}
		return make_dir_source(fs::path(std::string(spec.substr(4))));
	}
	// "owner/repo" or "owner/repo@<base-url>" (a GitHub-compatible mirror).
	const std::size_t at = spec.find('@');
	const std::string_view repo = spec.substr(0, at);
	if (valid_repo(repo)) {
		if (at == std::string_view::npos) {
			return make_http_source(std::string(repo));
		}
		const std::string_view base = spec.substr(at + 1);
		if (base.rfind("https://", 0) == 0 || base.rfind("http://", 0) == 0) {
			return make_http_source(std::string(repo), std::string(base));
		}
	}
	if (error != nullptr) {
		*error = "bad source '" + std::string(spec) +
				"' (use owner/repo[@https://mirror], or dir:/path)";
	}
	return nullptr;
}

std::string configured_source_spec(const Layout &layout) {
	if (const char *env = std::getenv("VB_SOURCE"); env != nullptr && *env != '\0') {
		return env;
	}
	try {
		const toml::table tbl = toml::parse_file(layout.cli_toml().string());
		if (auto v = tbl["source"].value<std::string>(); v && !v->empty()) {
			return *v;
		}
	} catch (const toml::parse_error &) {
	}
	return kDefaultSource;
}

} // namespace vb::cli
