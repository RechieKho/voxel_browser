# Developer Experience (pack authors & agents) — Design & Phased Plan

> Full detail for this topic; the backlog entry is `REMAINING_TASKS.md`
> Phase 10. Status: **planned (2026-10-06)**, nothing implemented yet.

## 1. Problem

The engine has grown a large Lua surface and a capable `vb` CLI, but a person
(or an agent) who wants to *write a content pack* still has to reverse-engineer
it:

| # | Gap | Today |
| --- | --- | --- |
| 1 | No `npm init` equivalent | A new pack is made by copying `content/base` or `content/examples/kitchen_sink` by hand. `vb structure new` is the only scaffolding command. |
| 2 | No quick-reference for the Lua API | `docs/lua-api.md` (~750 lines) is an accurate but narrative design log: one bullet can run 60 lines and mix several functions, history and phase numbers. There is no "one function, signature, one example" lookup. |
| 3 | No editor support | No type stubs, so editors flag `vb` as an undefined global, offer no completion, and happily complete `io.open`/`os.time`, which the sandbox removes (`strip_sandbox`, `src/script/vm.cpp`). |
| 4 | Not agent-friendly | `vb --help` is a one-line-per-command list; there is no markdown CLI reference, no per-command help with examples, no machine-readable command list, and no headless "does my pack load?" check an agent can loop on. |

## 2. Goals / non-goals

**Goals**

1. `vb pack new my_pack` (and `vb init` in an existing folder) produces a pack
   that loads, runs (`vb pack dev`), has editor completion, and explains itself
   to a human or an agent in its own `README.md`/`AGENTS.md`.
2. **One source of truth for the Lua API** from which the stubs and the quick
   reference are generated, with a test that fails when a binding is added to
   C++ without being described (drift is the main risk of any API doc).
3. A fast, headless, scriptable validation loop: `vb pack check [--json]`
   reports load errors with `file:line`, exit code `0/1`.
4. A complete, generated CLI reference (`docs/cli.md`) and `vb help <command>`,
   both from the command table, plus an `--json` form for tools.
5. Everything works offline from an installed version: stubs and docs ship in
   the release archive.

**Non-goals (for now)**

- A pack registry / `vb pack publish` / dependency resolution (stacked packs
  are already `Deferred`).
- An editor plugin. We target the standard Lua Language Server (LuaLS,
  `sumneko.lua`), which every mainstream editor already runs; stubs follow its
  LuaCATS annotation format.
- Server-side hot reload (also `Deferred`); `vb pack dev` keeps using the
  existing `vb host --watch` restart loop.
- Rewriting `docs/lua-api.md`'s history away. It stays as the narrative /
  design guide; the new reference is the lookup.

## 3. Key decisions

### 3.1 Source of truth: hand-written LuaCATS stubs

Candidates considered:

| Option | Pros | Cons |
| --- | --- | --- |
| **A. Hand-written LuaCATS stubs** (`sdk/lua/library/*.lua`) are the source; a script renders markdown from them | Stubs are what editors consume, so they can never be "the generated thing nobody checked". LuaCATS is a known format, agents already read it. Examples live next to the signature. | Need a small parser in the generator (only our own subset of annotations). |
| B. A schema (TOML/JSON) generating both stubs and docs | Fully structured | A third format to learn; examples and prose are awkward in TOML; two generators. |
| C. Annotate the C++ binding sites and extract | Closest to code | sol2 lambdas are spread over 3 300 lines of `pack_runtime.cpp`; comment-extraction from C++ is fragile; still needs prose somewhere. |

**Decision: A.** Drift is caught by a runtime test (§3.2), not by generating
from C++.

Layout (new top-level `sdk/`, shipped in the release archive):

```
sdk/lua/
  library/              # LuaCATS stubs, ---@meta files, never executed
    vb.lua              # vb.register_*, vb.on/after/every, vb.storage ...
    vb.world.lua        # one file per sub-table (world, worldgen, physics,
    vb.worldgen.lua     #   combat, action, hunger, daynight, render, noise,
    ...                 #   config, db, crypto, auth)
    player.lua          # usertypes: Player, Entity, Inventory, ...
    events.lua          # vb.on overloads: one ---@overload per event name,
                        #   so the handler's argument types are known
    ui.lua              # client UI VM (`ui` table, widget tree types)
    data_script.lua     # what a pure data script may return (block tables)
    sandbox.lua         # documents what is NOT available (io, os, load, ...)
  luarc.template.json   # copied to a pack's .luarc.json by `vb pack new`
```

Annotation conventions (checked by the generator, so the docs stay uniform):

- Every public function has a one-line summary, `---@param`/`---@return` for
  every argument, and **at least one fenced ```` ```lua ```` example** in its
  description. LuaLS shows that markdown on hover, so the example also reaches
  the editor.
- Definition tables (`register_block`'s `def`, `visual`, worldgen pipeline
  stages) are `---@class` with one `---@field` per key, including default and
  units (`---@field max_damage? integer Damage to break; 0 = instant (default 0)`).
- Context tags in a custom, LuaLS-ignored line: `---@vb context load|runtime|ui|data`
  (pack-load-only registration vs. runtime vs. client UI VM vs. data script)
  and `---@vb since <protocol or release>`. The generator turns these into
  badges in the reference.
- Event names are a `---@alias VbEvent "player_join"|...` plus one overload
  per event so `vb.on("block_break", function(e) ... end)` gets a typed `e`.

### 3.2 Drift test (runtime ⇆ stubs)

`tests/unit/lua_api_surface_test.cpp` (behind `VB_WITH_LUA`):

1. Build a `PackRuntime` over a loopback transport (as existing pack tests
   do) and a `UiRuntime`; walk `vb` / `ui` recursively, plus the usertype
   metatables (`Player`, ...), collecting dotted names of every function.
2. Read `sdk/lua/api_index.txt` — a sorted list of the same dotted names that
   the generator writes from the stubs (so the C++ test needs no Lua parser).
3. Fail with the symmetric difference: "bound but undocumented" and
   "documented but not bound". An explicit `sdk/lua/api_ignore.txt` lists
   internal helpers that are deliberately left out.

CI also runs `scripts/gen_lua_docs.py --check` (regenerates into a temp dir,
diffs against the committed output) so a stub edit without regenerated docs
fails the lint job.

### 3.3 Generated quick reference

`scripts/gen_lua_docs.py` (Python 3, stdlib only, like the other release
scripts) reads `sdk/lua/library/*.lua` and writes:

```
docs/lua-reference/
  README.md          # cheat sheet: every function as one line
                     #   `vb.world.get_block(pos) -> BlockId` — summary
                     #   grouped by table, each linked to its section
  vb.md, vb.world.md, ..., ui.md, events.md, types.md
sdk/lua/api_index.txt
```

Per-function section: signature, context badge, summary, parameter table,
returns, example(s), "see also" links. Examples are **syntax-checked** by a
doctest (`luaL_loadbuffer` without running; the generator also extracts them
into `build/doc_examples/*.lua`), so a typo in a doc example breaks CI.

`docs/lua-api.md` gets a header pointing to the reference for lookups and
keeps its narrative/design content; over time, per-function detail that the
reference now covers can be trimmed from it.

### 3.4 Headless pack validation: `voxel_browser_server --check-pack`

New server flag: `--check-pack <dir> [--json]` loads the pack into a
`PackRuntime` exactly like a real start (pack loader order, `require`,
`auth.lua` parsing, `freeze()`, worldgen pipeline construction, `ui/*.lua`
compiled in a `UiRuntime`, `structures/` validated with the existing editor
validator), generates **one** chunk around the origin to exercise the
worldgen pipeline, then exits without opening a socket. Output: diagnostics
`{severity, file, line, message}` (human or JSON), exit `0` clean / `1`
errors. Warnings (unknown fields in `def` tables, block without texture, …)
never fail unless `--strict`.

Living in the server binary (rather than in `vb`) keeps one loader and makes
the check match whatever engine version will host the pack. `vb pack check`
resolves the version (§3.5) and runs it.

### 3.4.1 Per-environment globals (decided 2026-10-06)

A pack's files run in four different environments, but LuaLS applies one set
of globals to the whole workspace:

| Files | Environment | Engine globals |
| --- | --- | --- |
| `init.lua`, `blocks/`, `entities/`, `biomes/`, other root `*.lua` | server pack VM (`PackRuntime`) | `vb` |
| `ui/*.lua` | client UI VM (`UiRuntime`) | `ui`, `client` |
| `data/*.lua` data scripts | bare state (`eval_data_script`) | none (`vb.*` raises) |
| `auth.lua` | auth VM (`load_auth_lua`) | none; returns a table |

So stubs that declare both `vb` and `ui` let `vb.world.get_block` complete
silently in `ui/hud.lua` (nil at runtime — and inside an `on_click` handler
only when clicked), and `ui.define` in `init.lua`. Pack-defined cross-file
globals (`base_ui` in `content/base/ui/_style.lua`, `base_*_id` on the server
side) leak across the boundary the same way.

**Decision:** the editor is best-effort, `vb pack check` is the enforcement.

1. Stubs declare every engine global; each carries `---@vb context` and its
   description starts with the environment ("Server pack VM only." /
   "Client UI VM only."), so hover makes the boundary visible.
2. `--check-pack` runs a **static global-access check**: compile every file
   (full Lua library, outside the sandbox), walk the function prototypes'
   bytecode for `_ENV` reads/writes (what `luac -l` lists as
   `GETTABUP/SETTABUP _ENV "name"`) and compare against the file's
   environment:
   - allowed = Lua builtins left by the sandbox + that environment's engine
     globals + globals *assigned* by any file of the same environment
     (so `base_ui` is fine in `ui/`, an error in `init.lua`);
   - a read of another environment's engine global is an **error**
     (`ui/hud.lua:42: 'vb' is not available in the client UI VM (server-only)`);
   - a read of a sandbox-removed builtin (`os`, `io`, `load`, …) is an error;
   - any other unknown global read is a **warning** (likely typo; `--strict`
     fails on it).
   Line numbers come from the prototype's line info, so diagnostics are
   `file:line` like every other check.
3. Optional, only if it proves reliable in 10.B: a generated VS Code
   multi-root `.code-workspace` with a nested `ui/.luarc.json` that drops the
   server stubs, giving real editor warnings in `ui/`. Not generated for
   editors where it doesn't work; no LuaLS plugin (too LuaLS-specific to
   maintain).
4. The template `AGENTS.md` states the rule in its first screen.

### 3.5 `vb pack` command group

```
vb pack new <dir> [--template minimal|ui|worldgen|base] [--name id] [--force] [--json]
vb init [--template ...]          # = vb pack new . (refuses a non-empty dir
                                  #   that already has pack.toml, unless --force)
vb pack check [dir] [--json] [--strict] [--version v]
vb pack dev [dir] [--version v] [--port n] [--no-client]
                                  # vb host --pack dir --watch + vb launch --connect
vb pack types [dir] [--version v] # (re)write .vb/lua stubs + .luarc.json
vb pack info [dir] [--json]       # name/version/engine req, files, block/entity counts
```

- **Non-interactive by default** (agents and CI): every value has a flag and
  a default; no prompts. `--json` prints what was created.
- **Version pinning:** `pack.toml`'s existing `engine_version_req` (today
  informational) is filled with `">=<installed default>"`. `vb pack
  check/dev/types` pick the newest installed version satisfying it (or
  `--version`), and explain how to `vb install` one if none does. The engine
  itself keeps ignoring the field (see the comment in
  `content/base/pack.toml`).
- **Templates** live in `templates/pack/<name>/` in the repo and are embedded
  into `vb` at build time (a CMake step generating a `.cpp` byte table, no new
  dependency), so `vb pack new` works before any version is installed and
  can't fail on a missing file. `{{name}}`, `{{version}}`, `{{engine_req}}`
  are the only substitutions. `--template base` instead copies
  `content/base` from the selected installed version (a full real pack).
  Every template is loaded by `--check-pack` in CI.

Generated pack (template `minimal`):

```
my_pack/
  pack.toml            # name, version 0.1.0, engine_version_req, entry
  init.lua             # vb.on("player_join") greeting + one chat command
  blocks/example.lua   # one textured block, registration commented
  ui/hello.lua         # one ui.define screen opened from init.lua
  textures/example.png # 16x16 placeholder
  README.md            # how to run/check/edit, links to the reference
  AGENTS.md            # §3.6
  .luarc.json          # LuaLS: Lua 5.4, library ./.vb/lua, sandbox builtins off
  .vb/lua/             # stubs copied from the version (gitignored; `vb pack types`)
  .gitignore           # .vb/, storage.json, db/, world/
```

`.luarc.json` disables the builtins the sandbox removes (`io`, `os`,
`package`, `debug` except `traceback` via a stub override), so the editor
reports `os.time()` as undefined exactly as the engine would. Stubs are
*copied* (not referenced in the version directory) so the pack's editor setup
survives `vb prune` and works when the pack is opened on another machine.

`vb pack types` is also how an existing pack (including `content/base` and
`kitchen_sink` in this repo) opts in.

### 3.6 Agent friendliness

1. **Command table becomes the CLI's documentation source.** `Command` gains
   `details` (paragraphs), `examples` (`{cmd, explanation}` pairs), per-flag
   help and `json` (does it support `--json`). Subcommand groups (`server`,
   `structure`, `pack`) get their own tables instead of one usage string.
   From it:
   - `vb help <command> [<sub>]` and `vb <command> --help`: full help with
     examples.
   - `vb help --markdown` → committed `docs/cli.md`, CI `--check` diff like the
     Lua docs. `vb help --json` → the whole table for tools.
   - Completions keep being generated from the same table (already true).
2. **Consistent machine contract**, written down once in `docs/cli.md`'s
   header: exit codes `0/1/2`, errors on stderr prefixed `error:` with a
   `hint:` line where one exists, `--json` on every command that reports
   state (adding it to `install`, `doctor`, `pack *`), no prompts unless a
   TTY *and* the action is destructive (and `--yes` always skips them).
3. **`AGENTS.md` files.**
   - Repo root `AGENTS.md`: a short pointer with the same content as
     `CLAUDE.md` (the convention other agent tools read), so non-Claude agents
     find `STATE.md`/`REMAINING_TASKS.md`/`CONTRIBUTING.md`.
   - Template `AGENTS.md` for packs (the high-value one): the pack layout and
     load order, the two VMs and which files run where, sandbox limits, the
     edit → `vb pack check --json` → `vb pack dev` loop, where the stubs and
     reference are (`.vb/lua/`, `vb docs`), and common mistakes (registration
     after freeze, block id order is saved in worlds, and — stated up front —
     files in `ui/` can't touch `vb` and server files can't touch `ui`/`client`,
     §3.4.1).
4. **Offline docs: `vb docs`.** The release archive ships `docs/` (at least
   `lua-reference/`, `cli.md`, `lua-api.md`, `structure-editor.md`, `auth.md`).
   `vb docs` lists topics, `vb docs vb.world.raycast` prints that section,
   `vb docs --path` prints the directory. An agent without web access can
   read the exact docs of the version it targets.
5. **`docs/llms.txt`**: a one-screen index of the docs (title + one line +
   path each), following the llms.txt convention, linked from `README.md`.

## 4. Testing strategy

| What | How |
| --- | --- |
| Stubs ⇆ runtime | `lua_api_surface_test.cpp` (§3.2) |
| Docs up to date | `gen_lua_docs.py --check`, `vb help --markdown` diff, in `lint.yml` |
| Doc examples compile | doctest loading every extracted example with `luaL_loadbuffer` |
| Stubs are valid LuaCATS | CI runs `lua-language-server --check sdk/lua content/base` (pinned release download); zero warnings in `sdk/`, warnings in packs reported but not gating until §5 phase F |
| Templates work | integration test: `vb pack new` each template into a temp dir, then `voxel_browser_server --check-pack` it |
| `--check-pack` | unit/integration cases: clean pack, syntax error (file:line), runtime error in `init.lua`, bad `register_block` def, bad `ui/*.lua`, bad structure, `--json` shape |
| Global-access check | `vb` read in `ui/` → error with line; `ui`/`client` in `init.lua` → error; `os.time` → error; `base_ui` shared across `ui/` files → clean, same global read in `init.lua` → error; unknown global → warning, error under `--strict`; `content/base` and `kitchen_sink` → clean |
| `vb pack` / `vb help` | CLI tests in the existing `vb` test style (`VB_HOME` temp root, no network) |

## 5. Phased plan

Order follows dependencies (stubs before scaffolding, which copies them;
`--check-pack` before templates, which CI validates with it). The agent-docs
phase (F) depends only on the command table and can run in parallel with B–D.

### 10.A — API inventory + drift test (S–M)

- [ ] Surface walker test (§3.2) that dumps every bound name; commit the first
      dump as the to-document checklist.
- [ ] `sdk/lua/` skeleton, `api_index.txt`, `api_ignore.txt`; the test runs
      in "report only" mode until 10.B finishes, then gates.
- **Exit:** the list of everything that needs a stub exists and is enforced.

### 10.B — LuaCATS stubs (L)

- [ ] Stubs for the server VM (`vb.*`, sub-tables, usertypes, definition
      classes, `vb.on` overloads per event), the UI VM (`ui`, `client`, widget
      tree, `state`), data scripts and `auth.lua`'s returned table.
- [ ] One example per function; context tags plus an environment line at the
      start of every engine global's description (§3.4.1); `sandbox.lua`.
- [ ] Try the multi-root / nested `ui/.luarc.json` setup in VS Code and
      Neovim; keep it for 10.E only where it gives correct warnings.
- [ ] `luarc.template.json`; `content/base` and `kitchen_sink` get a
      `.luarc.json` so the repo dogfoods it.
- [ ] CI: LuaLS `--check` on `sdk/lua`; drift test gates.
- **Exit:** opening `content/base` in VS Code with LuaLS gives completion and
  hover docs for every `vb.*` call and no false "undefined global" warnings.

### 10.C — Generated quick reference (M)

- [ ] `scripts/gen_lua_docs.py` → `docs/lua-reference/` + cheat sheet + `--check`.
- [ ] Example extraction + syntax-check doctest.
- [ ] `docs/lua-api.md` header → reference; README links the cheat sheet.
- **Exit:** every bound function has a reference entry with an example, and
  CI fails if a stub change isn't regenerated.

### 10.D — Headless pack validation (M)

- [ ] `voxel_browser_server --check-pack <dir> [--json] [--strict]` (§3.4),
      including UI VM compile, structure validation and one-chunk worldgen.
- [ ] Static global-access check per environment (§3.4.1).
- [ ] Diagnostics carry `file:line` (parse Lua error prefixes; registration
      errors report the calling chunk via `debug.traceback` level).
- [ ] `vb pack check` + version resolution from `engine_version_req`.
- **Exit:** a broken pack yields a precise, machine-readable error without
  starting a game.

### 10.E — Scaffolding: `vb pack new` / `vb init` (M–L)

- [ ] `templates/pack/{minimal,ui,worldgen}` + build-time embedding into `vb`.
- [ ] `vb pack new`/`vb init`/`vb pack types`/`vb pack info`; `--template base`.
- [ ] Generated `README.md`, `AGENTS.md`, `.luarc.json`, `.gitignore` (+ the
      `ui/` multi-root setup if 10.B kept it).
- [ ] `vb pack dev` (host `--watch` + client connect; `--no-client`).
- [ ] Integration test: new → check for every template.
- **Exit:** `vb init && vb pack dev` puts a player into a world running the
  new pack in under a minute, with editor completion working.

### 10.F — CLI reference & agent docs (M)

- [ ] Extend `Command` (details, examples, flags, json); split subcommand
      tables; `vb help <cmd>`, `--help` everywhere.
- [ ] `vb help --markdown` → `docs/cli.md` (+ CI check); `vb help --json`.
- [ ] Machine contract audit: `error:`/`hint:` format, `--json` on
      `install`/`doctor`/`pack *`, `--yes` on every prompt.
- [ ] Root `AGENTS.md`; `docs/llms.txt`.
- **Exit:** an agent given only `vb help --json` (or `docs/cli.md`) can
  install a version, scaffold, check and host a pack without guessing flags.

### 10.G — Ship it (S)

- [ ] `package_release.py`: include `sdk/` and `docs/` in the game archive.
- [ ] `vb docs [topic] [--path]`.
- [ ] README: "Make your first pack" section (`vb init` → `vb pack dev`),
      `CONTRIBUTING.md`: how to add a binding (C++ + stub + regenerate).
- **Exit:** everything above works from a fresh `install.sh` with no repo
  checkout and no network after install.

## 6. Open questions

1. ~~**Per-directory globals.**~~ Decided 2026-10-06: labelled stubs +
   static check in `vb pack check`; nested editor config only if it works
   (§3.4.1).
2. **`vb init` vs `vb pack init`.** Plan: both, `vb init` being the short,
   npm-like alias. Revisit if the top-level namespace gets crowded.
3. **Should the engine enforce `engine_version_req`?** Out of scope here
   (informational today); `vb` only uses it to pick a version.
4. **Example execution.** Syntax-checking is cheap; actually running examples
   needs a fixture world per example. Revisit after 10.C if doc examples rot.
