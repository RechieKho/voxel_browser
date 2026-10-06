# Agent notes

Start with `STATE.md` (gotchas and current status), `REMAINING_TASKS.md`
(backlog) and `ARCHITECTURE_SPEC.md` (target design); `CONTRIBUTING.md` covers
building, testing and code style.

Writing a **content pack** (Lua)? You don't need the engine sources: run
`vb pack init`, read the generated `AGENTS.md`, and use `vb pack check --json`.
The CLI is documented in `docs/cli.md` (`vb help --json` for tools), the Lua
API in `docs/lua-reference/README.md`; `docs/llms.txt` indexes all docs.

**Dream before you finish.** Each of those three files has a hard cap of 500
lines. Run `wc -l REMAINING_TASKS.md STATE.md ARCHITECTURE_SPEC.md`; if any is
at or over 500 (or you just closed a phase / investigation), move inactive
material into `remaining_tasks/`, `state/` or `architecture_spec/` following
`DREAMING.md`, in a separate docs-only commit.

**"Implement in autopilot mode"** means: implement the given task list in
order without check-ins, update the docs (including `README.md`), open a PR,
then fix CI until it is all green. Read `AUTOPILOT.md` for the full procedure.
