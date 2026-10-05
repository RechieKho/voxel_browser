# Content Pack Format — Full Reference

> Full detail for this topic; linked from `ARCHITECTURE_SPEC.md`. Ground truth — do not duplicate here.

## 16. Content Pack Format (`content/base`)

```
pack.toml            name, version, engine_version_req, entry = "init.lua"
init.lua             pack-wide setup; always loaded last (host-driven, see below)
*.lua (pack root)    any other top-level module (e.g. crafting.lua) — loaded
                     sorted, after blocks/entities/biomes, before init.lua
data/blocks.lua      pure-data block tables (returns a list); read by registration code
                     and by the structure editor. Recommended, not required
blocks/*.lua         vb.register_block{...}; content/base's only file here is
                     register.lua, which registers everything in data/blocks.lua
entities/*.lua       player defaults, dropped-item entity, visual = {...} (§11.3)
ui/*.lua             inventory screen, pause menu content
textures/*.png       16×16 block textures; packed into an atlas client-side
textures/entities/*.png  billboard sprite atlases (§11.3), directional × animation frames
```

A block data script (`data/blocks.lua`) is a convention, not a rule: a Lua file
returning a list of `vb.register_block` tables, with no `vb.*` calls, so tools
can read the blocks without loading the pack (`vb::script::eval_data_script`,
`docs/structure-editor.md` §G). Order is registration order, and block ids
follow it.

Real `require` doesn't exist yet (§10.2), so a pack can't pull its own files in
from `init.lua` the way this layout might imply — the host
(`vb::script::load_content_pack`, `src/script/pack_loader.cpp`) walks the
directories and any other root-level `*.lua` file itself, in a fixed order,
loading each into the same Lua state as one shared chunk sequence. The engine
has no idea what any of these files are *for* beyond that fixed load order —
`content/base/crafting.lua` (an ordinary root-level module, not a special
case) is the reference example of a pack building a real gameplay system
(crafting recipes) entirely in content, with no engine-side game logic at
all.

The base pack is the reference implementation of the Lua API and the smoke-test
content for CI.

