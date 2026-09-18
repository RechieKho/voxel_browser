#pragma once

#include <filesystem>

#include "vb/script/pack_runtime.hpp"

// Phase 5.1: loads a content pack's Lua files into a PackRuntime.
//
// `vb::script::Vm`'s sandbox nils out `require` (src/script/vm.cpp) --
// REMAINING_TASKS.md 4.1 tracks a real virtual-FS `require` as a deferred
// follow-up. Until that lands, a pack's `init.lua` cannot pull in its own
// `blocks/*.lua`/`entities/*.lua` itself, so the host walks the pack
// directory and loads each file as its own chunk, in a fixed order:
// blocks/*.lua (sorted), then entities/*.lua (sorted), then biomes/*.lua
// (sorted), then any other loose *.lua file directly at the pack root
// (sorted), then init.lua last. Every file shares the same Lua globals
// (`vb.register_block` etc.), so this is behaviourally equivalent to one
// concatenated script.
//
// `ui/*.lua` is deliberately NOT part of this walk: it runs in a separate,
// restricted client-side `UiRuntime` Lua state (no `vb.register_*`/server
// globals at all, just `ui`/`client`) that src/client/main.cpp loads
// directly off disk (or via Asset Sync for real multiplayer) -- see its
// `enter_playing` lambda. Adding "ui" here would make PackRuntime try to run
// e.g. `ui.define_hud(...)` against a Lua state that has no `ui` global at
// all, which is a fatal load error, not a fix (confirmed 2026-09-18 after a
// report that the base pack's break-progress HUD wasn't appearing -- the
// real fix was elsewhere; see STATE.md).

namespace vb::script {

// Loads every pack Lua file into `rt` (before rt.freeze()). Returns false on
// a real syntax/runtime error in a pack file (a broken pack is a fatal
// misconfiguration, logged via VB_ERROR with the offending file name) or
// true otherwise -- including a build without VB_WITH_LUA, where every load
// reports core::ScriptError::kDisabled and is treated as a non-fatal,
// once-logged no-op (same graceful-degrade pattern as VB_WITH_NET off).
// Missing `blocks/`/`entities/` directories or a missing `init.lua` are not
// errors -- a pack may not have all three.
bool load_content_pack(PackRuntime &rt, const std::filesystem::path &content_pack);

} // namespace vb::script
