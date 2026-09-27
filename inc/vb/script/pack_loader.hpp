#pragma once

#include <filesystem>

#include "vb/script/pack_runtime.hpp"

// Phase 5.1: loads a content pack's Lua files into a PackRuntime.
//
// Every `.lua` file under `content_pack` (except `ui/*.lua`, see below) is
// also installed as a requirable module (Phase 4.1, `PackRuntime::
// set_pack_modules` -> `vb::script::Vm::install_require`), keyed by its
// path relative to the pack root -- so `require("lib.util")` inside any pack
// file resolves `lib/util.lua` against that same in-memory map, never the
// real filesystem. That's for genuinely optional shared helper modules a
// pack author wants to pull in explicitly; it does NOT replace the fixed
// load order below, which remains the only way `vb.register_block` et al.
// actually run -- `require`-ing e.g. `blocks/stone.lua` a second time just
// returns its (already-run) cached result, it doesn't run it again.
//
// `init.lua` still cannot reach into `blocks/*.lua`/`entities/*.lua` on its
// own via a plain directory-walk-replacing `require` -- the host walks the
// pack directory and loads each of those as its own top-level chunk, in a
// fixed order:
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
