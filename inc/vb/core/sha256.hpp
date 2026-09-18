#pragma once

#include <string>
#include <string_view>

// Minimal SHA-256 (FIPS 180-4). Dependency-free -- backs vb.crypto.hash (spec
// §10.6/§10.7, Phase 6.4) so a pack implementing its own login doesn't have
// to roll credential hashing in pure Lua (the sandbox strips os/io -- see
// ARCHITECTURE_SPEC.md §10.2), and vb::script::ScriptDb's key-to-filename
// hashing (vb/script/db.hpp).

namespace vb::core {

// Lowercase hex digest, 64 chars.
std::string sha256_hex(std::string_view data);

} // namespace vb::core
