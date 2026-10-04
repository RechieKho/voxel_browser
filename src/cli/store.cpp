#include "vb/cli/store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include <toml++/toml.hpp>

#include "vb/cli/version.hpp"

namespace vb::cli {

namespace fs = std::filesystem;

namespace {

// Quotes `s` as a TOML basic string.
std::string toml_quote(const std::string &s) {
	std::string out = "\"";
	for (const char c : s) {
		switch (c) {
		case '\\': out += "\\\\"; break;
		case '"': out += "\\\""; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default: out += c; break;
		}
	}
	return out + "\"";
}

Status write_text(const fs::path &path, const std::string &text) {
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	if (ec) {
		return { "cannot create " + path.parent_path().string() + ": " + ec.message() };
	}
	// Write-then-rename so a crash never leaves a truncated cli.toml/link file.
	fs::path tmp = path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << text;
		if (!out) {
			return { "cannot write " + tmp.string() };
		}
	}
	fs::rename(tmp, path, ec);
	if (ec) {
		return { "cannot replace " + path.string() + ": " + ec.message() };
	}
	return {};
}

std::optional<fs::path> read_link_target(const fs::path &file) {
	try {
		const toml::table tbl = toml::parse_file(file.string());
		if (const auto p = tbl["path"].value<std::string>()) {
			return fs::path(*p);
		}
	} catch (const toml::parse_error &) {
	}
	return std::nullopt;
}

} // namespace

std::string binary_file_name(Binary which) {
	const char *base = which == Binary::Client ? "voxel_browser" : "voxel_browser_server";
#if defined(_WIN32)
	return std::string(base) + ".exe";
#else
	return base;
#endif
}

std::optional<std::string> read_default_version(const Layout &layout) {
	try {
		const toml::table tbl = toml::parse_file(layout.cli_toml().string());
		if (auto v = tbl["default_version"].value<std::string>(); v && !v->empty()) {
			return *v;
		}
	} catch (const toml::parse_error &) {
	}
	return std::nullopt;
}

Status write_default_version(const Layout &layout, const std::string &name) {
	// Preserve other keys a future cli.toml may carry.
	toml::table tbl;
	try {
		tbl = toml::parse_file(layout.cli_toml().string());
	} catch (const toml::parse_error &) {
	}
	tbl.insert_or_assign("default_version", name);
	std::ostringstream os;
	os << tbl << "\n";
	return write_text(layout.cli_toml(), os.str());
}

std::vector<Entry> list_entries(const Layout &layout) {
	const auto def = read_default_version(layout);
	std::vector<Entry> out;
	std::error_code ec;

	std::vector<std::pair<Version, Entry>> releases;
	for (fs::directory_iterator it(layout.versions_dir(), ec), end; !ec && it != end;
			it.increment(ec)) {
		const std::string name = it->path().filename().string();
		const auto v = parse_version(name);
		if (!v || !it->is_directory(ec) || to_tag(*v) != name) {
			continue; // .staging-*, stray files
		}
		Entry e;
		e.name = name;
		e.kind = EntryKind::Release;
		e.root = it->path();
		e.is_default = def && *def == name;
		releases.emplace_back(*v, std::move(e));
	}
	std::sort(releases.begin(), releases.end(),
			[](const auto &a, const auto &b) { return a.first > b.first; });
	for (auto &r : releases) {
		out.push_back(std::move(r.second));
	}

	std::vector<Entry> links;
	ec.clear();
	for (fs::directory_iterator it(layout.links_dir(), ec), end; !ec && it != end;
			it.increment(ec)) {
		const fs::path &p = it->path();
		if (p.extension() != ".toml" || !is_valid_link_name(p.stem().string())) {
			continue;
		}
		const auto target = read_link_target(p);
		if (!target) {
			continue;
		}
		Entry e;
		e.name = p.stem().string();
		e.kind = EntryKind::Link;
		e.root = *target;
		e.is_default = def && *def == e.name;
		std::error_code dec;
		e.root_exists = fs::is_directory(e.root, dec);
		links.push_back(std::move(e));
	}
	std::sort(links.begin(), links.end(),
			[](const Entry &a, const Entry &b) { return a.name < b.name; });
	for (auto &l : links) {
		out.push_back(std::move(l));
	}
	return out;
}

std::optional<Entry> resolve_entry(const Layout &layout, std::string_view name,
		std::string *why) {
	const auto fail = [&](std::string msg) -> std::optional<Entry> {
		if (why != nullptr) {
			*why = std::move(msg);
		}
		return std::nullopt;
	};
	const auto entries = list_entries(layout);
	std::string wanted(name);
	if (wanted.empty() || wanted == "default") {
		const auto def = read_default_version(layout);
		if (!def) {
			return fail(entries.empty()
							? "nothing installed (try `vb link <name> <build-dir>`)"
							: "no default version set (try `vb use <version>`)");
		}
		wanted = *def;
	}
	if (const auto v = parse_version(wanted)) {
		wanted = to_tag(*v); // accept "0.2.0" for "v0.2.0"
	}
	for (const Entry &e : entries) {
		if (e.name == wanted) {
			if (!e.root_exists) {
				return fail("'" + wanted + "' links to " + e.root.string() +
						", which no longer exists");
			}
			return e;
		}
	}
	return fail("'" + wanted + "' is not installed (see `vb list`)");
}

std::optional<fs::path> find_binary(const Entry &entry, Binary which) {
	std::error_code ec;
	fs::path p = entry.root / binary_file_name(which);
	if (fs::is_regular_file(p, ec)) {
		return p;
	}
	return std::nullopt;
}

Status add_link(const Layout &layout, std::string_view name, const fs::path &build_dir) {
	if (!is_valid_link_name(name)) {
		return { "invalid link name '" + std::string(name) +
				"' (use lowercase letters, digits, '_' or '-'; it must start with a letter "
				"and must not look like a version)" };
	}
	std::error_code ec;
	const fs::path abs = fs::weakly_canonical(fs::absolute(build_dir, ec), ec);
	if (ec || !fs::is_directory(abs, ec)) {
		return { build_dir.string() + " is not a directory" };
	}
	return write_text(layout.link_file(name), "path = " + toml_quote(abs.generic_string()) + "\n");
}

Status remove_link(const Layout &layout, std::string_view name) {
	std::error_code ec;
	if (!is_valid_link_name(name) || !fs::remove(layout.link_file(name), ec)) {
		return { "no link named '" + std::string(name) + "'" };
	}
	const auto def = read_default_version(layout);
	if (def && *def == name) {
		fs::remove(layout.cli_toml(), ec); // only key today; rewritten on next `vb use`
	}
	return {};
}

Status uninstall(const Layout &layout, std::string_view name) {
	std::string why;
	std::string wanted(name);
	if (const auto v = parse_version(wanted)) {
		wanted = to_tag(*v);
	}
	const auto entries = list_entries(layout);
	const auto it = std::find_if(entries.begin(), entries.end(),
			[&](const Entry &e) { return e.name == wanted; });
	if (it == entries.end()) {
		return { "'" + std::string(name) + "' is not installed" };
	}
	if (it->kind == EntryKind::Link) {
		return remove_link(layout, it->name);
	}
	std::error_code ec;
	fs::remove_all(it->root, ec);
	if (ec) {
		return { "cannot remove " + it->root.string() + ": " + ec.message() };
	}
	if (it->is_default) {
		fs::remove(layout.cli_toml(), ec);
	}
	return {};
}

} // namespace vb::cli
