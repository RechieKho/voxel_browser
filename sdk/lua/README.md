# sdk/lua — Lua API stubs

Hand-written [LuaCATS](https://luals.github.io/wiki/annotations/) stubs for the Lua Language Server
(LuaLS, `sumneko.lua`). They are the **single source of truth** for the pack Lua API:

- `library/*.lua` — the stubs (`---@meta`, never executed). Editors read them for completion and hover.
- `api_index.txt` — sorted list of every documented member, *generated* by `scripts/gen_lua_docs.py`.
- `api_ignore.txt` — bound names intentionally not documented.
- `luarc.template.json` — copied to a pack as `.luarc.json` by `vb pack init` / `vb pack types`.

Quick reference (generated): [`docs/lua-reference/README.md`](../../docs/lua-reference/README.md).

Adding or changing a binding: edit the C++, add/adjust the stub (summary, `---@param`/`---@return`,
a ```` ```lua ```` example, `---@vb context ...`), run `python3 scripts/gen_lua_docs.py`, commit the
regenerated files. `ctest` fails (`lua_api_surface_test`) when the live API and the stubs disagree.

Files in a pack's `ui/` folder run in the client UI VM (`ui`, `client`); everything else runs in the
server VM (`vb`). The stubs declare both so one editor setup works, so rely on `vb pack check` for the
per-environment rule.
