-- content/base's entry point (pack.toml's `entry` field). Loaded last by
-- src/script/pack_loader.cpp, after every blocks/*.lua, entities/*.lua, and
-- biomes/*.lua file -- so every `vb.register_*` id above already exists.
--
-- Real `require` doesn't exist yet (see pack.toml's comment), so this can't
-- pull the other files in itself the way the spec's §16 layout implies --
-- the host (src/server/main.cpp via load_content_pack) does that instead, in
-- a fixed order. This file is for pack-wide setup that isn't a single
-- block/entity/biome registration.

-- vb.storage (Phase 4.2, src/script/pack_runtime.cpp) is a real, working,
-- JSON-file-backed table -- this demonstrates it persists across restarts
-- rather than declaring a feature nothing exercises.
vb.storage.boot_count = (vb.storage.boot_count or 0) + 1
-- vb.storage round-trips through JSON (src/script/pack_runtime.cpp's
-- json_to_lua/lua_to_json), which always hands back Lua float numbers, even
-- for integer values -- format explicitly so this doesn't print "1.0".
print(string.format("[base] content pack loaded (boot #%d)", vb.storage.boot_count))
