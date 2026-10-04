#pragma once

#include <filesystem>
#include <string>

// `vb server service print`: a service definition the user installs themselves
// (still no elevation: a systemd *user* unit, a launchd *agent*, a per-user
// scheduled task). The service just runs `vb server start <name> --foreground`,
// so status / stop / logs keep working exactly as for a hand-started server.

namespace vb::cli {

enum class ServiceKind { Systemd,
	Launchd,
	TaskScheduler };

struct ServiceSpec {
	std::string name; // instance name (validated: [A-Za-z0-9._-])
	std::filesystem::path vb_exe; // absolute path of vb
	std::filesystem::path instance_dir;
	std::filesystem::path log_file;
	std::string vb_home; // VB_HOME to pass on; empty = not set
};

// The service kind for the OS vb is running on.
ServiceKind host_service_kind();

// "systemd" | "launchd" | "task" -> kind; false for anything else.
bool parse_service_kind(const std::string &text, ServiceKind &out);

std::string render_service(ServiceKind kind, const ServiceSpec &spec);

// Conventional file name for the definition ("voxel-browser-<name>.service", ...).
std::string service_file_name(ServiceKind kind, const std::string &name);

// Human instructions for installing / removing it (printed to stderr, so the
// definition on stdout stays a clean file).
std::string service_install_hint(ServiceKind kind, const ServiceSpec &spec);

} // namespace vb::cli
