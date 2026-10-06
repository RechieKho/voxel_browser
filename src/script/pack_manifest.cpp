#include "vb/script/pack_manifest.hpp"

#include <toml++/toml.hpp>

namespace vb::script {

PackManifest read_pack_manifest(const std::filesystem::path &pack_dir) {
	PackManifest m;
	const std::filesystem::path file = pack_dir / "pack.toml";
	std::error_code ec;
	if (!std::filesystem::is_regular_file(file, ec)) {
		return m;
	}
	try {
		const toml::table tbl = toml::parse_file(file.string());
		m.found = true;
		m.name = tbl["name"].value_or(std::string{});
		m.version = tbl["version"].value_or(std::string{});
		m.entry = tbl["entry"].value_or(std::string{});
		if (const toml::node *node = tbl.get("engine_version_req")) {
			if (auto s = node->value<std::string>()) {
				m.engine_version_req = *s;
			} else {
				m.engine_version_req = std::string("<not a string>");
			}
			m.engine_version_req_line = static_cast<int>(node->source().begin.line);
		}
	} catch (const toml::parse_error &e) {
		m.found = true;
		m.error = "pack.toml:" + std::to_string(e.source().begin.line) + ": " + std::string(e.description());
	}
	return m;
}

core::EngineReqCheck check_pack_engine_req(const PackManifest &manifest, const core::SemVer &engine) {
	if (!manifest.found) {
		return {};
	}
	if (!manifest.error.empty()) {
		core::EngineReqCheck bad;
		bad.ok = false;
		bad.message = manifest.error;
		return bad;
	}
	const std::string name = manifest.name.empty() ? "(unnamed)" : manifest.name;
	return core::check_engine_req(name, manifest.engine_version_req, engine);
}

} // namespace vb::script
