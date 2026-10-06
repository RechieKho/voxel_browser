# DREAMING — Compacting the Agent Docs

> A reminder for future agents. "Dreaming" means stepping back from feature work to
> refactor the agent-oriented documents that every session reads, so they stay
> short and current. Inactive material moves into the matching **vault**
> directory. Nothing gets deleted.

---

## Why

Every agent session reads `REMAINING_TASKS.md`, `STATE.md` and
`ARCHITECTURE_SPEC.md`. Agents add to them every session and rarely remove
anything, so they keep growing. Each extra line costs context in every future
session and buries the few lines that still matter. Dreaming moves finished,
superseded and historical material out of those files. The history is kept, but
nobody has to read it to find the current state.

## The rule

| Core file (read often)  | Vault (read on demand)    | Hard cap  |
| ----------------------- | ------------------------- | --------- |
| `REMAINING_TASKS.md`    | `remaining_tasks/*.md`    | 500 lines |
| `STATE.md`              | `state/*.md`              | 500 lines |
| `ARCHITECTURE_SPEC.md`  | `architecture_spec/*.md`  | 500 lines |

Each core file stays **under 500 lines** (check with `wc -l`). Aim for a margin
of 50–100 lines below that, so the next few sessions can add notes without
going over straight away. `STATE.md` has its own softer target of ~300–400
lines, and the 500-line cap does not replace it.

`STATE.md.local` is machine-specific and not shared, so this rule does not
apply to it.

## When to dream

Dream whenever **any** of these is true:

- A core file is at or above 500 lines. **Do this before ending the session.**
  Finish the task you were given, then dream before you commit.
- A core file is above ~450 lines and you are about to add to it.
- A phase, design question or investigation was just closed, so its write-up is
  now history.
- The user asks you to "dream".

Check all three files at once:

```bash
wc -l REMAINING_TASKS.md STATE.md ARCHITECTURE_SPEC.md
```

## What counts as inactive (move it to the vault)

- **`REMAINING_TASKS.md`**
  - Detailed write-ups under `[x]` items: what shipped, why, which files and
    which tests. In the core, reduce each one to a single line with a pointer
    such as ``Full detail: `remaining_tasks/phaseN.md`.``
  - Phases that are fully ✅ done. Keep the heading, a 2–4 line summary and
    the pointer.
  - Long rationale for deferred items. That goes in `remaining_tasks/deferred.md`.
- **`STATE.md`**
  - "Current status" entries that are no longer current, meaning the work
    landed and later sessions have built on it. Move them to the newest
    `state/changelog-*.md`.
  - Bug investigations that are resolved, unless the trap can still catch
    someone. A trap that can still catch someone stays as a one-line warning
    in the core, with the long form in `state/gotchas.md`.
  - Lists of steps, logs and proofs. Keep the conclusion in the core and move
    the evidence to the vault.
- **`ARCHITECTURE_SPEC.md`**
  - Full struct definitions, diagrams, API surfaces, design rationale and
    resolution history. Each `§N` keeps its actionable rules and current
    status, and points to `architecture_spec/<topic>.md`.
  - Open Questions (§18) that are resolved. Keep one line with the decision in
    the core and move the discussion to `architecture_spec/open-questions.md`.

## What stays in the core (active)

- Anything a session needs **before** it starts work: open `[ ]`/`[~]` items,
  live landmines, standing priorities, build and test traps that still apply,
  and current design rules.
- Bugs that are open, or fixed but likely to come back.
- The legend, the headers, and the "Detail files" or "Full detail:" pointers
  that make the vault findable.

If you are unsure, keep a **one-line summary plus a pointer** in the core and
move the body to the vault. A pointer costs one line, and losing a landmine
costs a whole session.

## How to dream

1. **Measure.** Run `wc -l` on the three core files, and on any vault file you
   plan to append to.
2. **Classify.** Go through the core file section by section and mark each
   block *active* or *inactive* using the lists above.
3. **Move, don't rewrite.** Cut inactive blocks into the matching vault file
   with their wording unchanged. Rewording history loses detail for no gain.
   Put a one-line summary and a pointer where each block used to be.
   - Pick the vault file that already holds the topic: `remaining_tasks/phaseN.md`,
     `remaining_tasks/cross_cutting.md`, `state/gotchas.md`,
     `architecture_spec/<topic>.md`, and so on.
   - `state/changelog-*.md` files are newest-first. Insert new history at the
     **top** of the newest one, under a dated heading.
   - Vault files have no hard cap. If one passes ~2000 lines, split it, for
     example `changelog-part4.md`. Then update the file's own header and the
     index in its core file.
4. **Fix the index.** Every vault file must appear in its core file's
   index: STATE.md's "Detail files" list, the "Full detail:" lines in
   REMAINING_TASKS.md, and the per-section links in ARCHITECTURE_SPEC.md.
   Update the descriptions of the vault files you touched so they say what
   now lives there.
5. **Fix cross-references.** Search for links to anything you moved, such as
   `grep -rn "STATE.md §" --include=*.md .`, and to section numbers that
   changed. Point them at the new location.
6. **Verify.** Run `wc -l` again: every core file must be under 500 lines. Diff
   the moved text (`git diff --stat` and a skim of `git diff`) to confirm that
   nothing disappeared. Any line removed from a core file should reappear in a
   vault file, or as a summary plus pointer.
7. **Commit separately.** Dreaming is a docs-only change. Commit it on its own,
   for example `docs: dream — compact STATE.md into state/changelog-part3.md`,
   apart from feature work, so reviewers can check it as a pure move.

## Don'ts

- Don't delete history. Move it. The only exception is text that is
  duplicated word for word and already exists in the vault.
- Don't leave a core file with a broken or missing pointer to its vault.
- Don't summarize an open landmine away. The warning stays in the core, and
  only the long explanation moves.
- Don't mix dreaming with code changes in one commit.
- Don't move content between the three domains. Backlog history goes to
  `remaining_tasks/`, gotchas and changelogs go to `state/`, and design goes
  to `architecture_spec/`.
