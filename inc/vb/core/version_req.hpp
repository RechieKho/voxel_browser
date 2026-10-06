#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Engine version requirement of a content pack (`pack.toml`'s
// `engine_version_req`; architecture_spec/dev-experience.md §3.7). A small
// Cargo-style subset shared by the engine and the `vb` CLI:
//
//   *                     any version
//   >=0.5.0 >0.5.0 <=0.7 <0.7 =0.5.2   comparators (missing minor/patch = 0)
//   >=0.5.0, <0.7.0       comma = AND
//   ^0.5.1  ~0.5.1        >=0.5.1, <0.6.0  (^1.2.3 = >=1.2.3, <2.0.0)
//
// No OR, no pre-release identifiers. An unparseable value is an error (fail
// closed), never treated as `*`.

namespace vb::core {

struct SemVer {
	int major = 0;
	int minor = 0;
	int patch = 0;

	friend bool operator==(const SemVer &, const SemVer &) = default;
	friend auto operator<=>(const SemVer &, const SemVer &) = default;
};

// "0.5.2", "v0.5", "1" (missing parts = 0). Rejects anything else, including a
// "-3-gabc1234" git-describe suffix (the caller strips it; see engine_version()).
std::optional<SemVer> parse_semver(std::string_view text);

std::string to_string(const SemVer &v);

class VersionReq {
public:
	// Parses `text`. On failure returns nullopt and, if given, sets `*error`.
	static std::optional<VersionReq> parse(std::string_view text, std::string *error = nullptr);

	// `*`
	static VersionReq any();

	bool matches(const SemVer &v) const;
	bool is_any() const { return comparators_.empty(); }

	// Normalised text ("*" or ">=0.5.0, <0.6.0").
	std::string to_string() const;

	// The smallest version the requirement can admit (the lower bound of its
	// first `>=`/`=` comparator), or nullopt for an open lower bound. Used for
	// "this pack needs Voxel Browser >=X" messages.
	std::optional<SemVer> lower_bound() const;

	enum class Op { kGe, kGt, kLe, kLt, kEq };
	struct Comparator {
		Op op = Op::kGe;
		SemVer version;
	};

private:
	std::vector<Comparator> comparators_;
};

// The version this binary identifies as: `kVersionNumeric` (newest reachable
// tag). A build outside git counts as 0.0.0.
SemVer engine_version();

// Result of checking a pack's declared requirement against an engine version.
struct EngineReqCheck {
	bool ok = true;
	bool warning = false; // missing field: treated as `*` with a warning
	std::string message; // empty when ok and no warning
};

// `req_text` is the raw `engine_version_req` string (nullopt = field missing).
// An unparseable value is an error (ok = false).
EngineReqCheck check_engine_req(const std::string &pack_name, const std::optional<std::string> &req_text,
		const SemVer &engine);

} // namespace vb::core
