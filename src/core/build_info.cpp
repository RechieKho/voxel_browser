#include "vb/core/build_info.hpp"

namespace vb::core {

std::string describe_build() {
	std::string out;
	out.reserve(96);
	out += kProjectName;
	out += ' ';
	out += kVersionString;
	out += " (protocol ";
	out += std::to_string(static_cast<unsigned>(kEngineProtocolVersion));
	out += ", built ";
	out += kBuildTimestamp;
	out += ')';
	return out;
}

} // namespace vb::core
