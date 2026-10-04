#include "vb/cli/layout.hpp"

#include "vb/core/paths.hpp"

namespace vb::cli {

Layout Layout::from_environment() {
	return Layout(vb::core::user_data_dir(), vb::core::user_config_dir(),
			vb::core::user_cache_dir());
}

} // namespace vb::cli
