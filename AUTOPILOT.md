# AUTOPILOT — "Implement in autopilot mode"

> For future agents. When the user says **"implement in autopilot mode"**, they
> have handed you a list of tasks (or a plan) that is already specified and
> ready to execute. They want you to take it from the first task to a green PR
> **without stopping to check in**. They will review the whole result in one
> pass at the end. This file explains what that means in this repo.

---

## Preconditions

- **A cloud session.** Autopilot is meant for Claude Code's remote sessions,
  where the user is usually away. Don't wait for an answer, because none is
  coming soon.
- **A concrete, executable list.** This can be a task list in the prompt, a
  plan file, or a named block of `REMAINING_TASKS.md` items. Every item should
  be something you can do without asking a design question. If an item is
  missing a decision, see "Ambiguity" below.
- **A designated branch.** Develop and push only on the branch the session
  gives you. Never push to `main`.

## The four stages

Do these **in order**. Don't start a stage until the one before it is done.

### 1. Implement every task, in sequence

- Work through the list **from top to bottom, in the order given**. Later
  tasks often build on earlier ones. Track progress with the task tools
  (TaskCreate / TaskUpdate) so the order and status are visible.
- **Don't pause between tasks for approval.** The user reviews everything at
  once at the end, so asking "should I continue?" only stalls the session.
- Finish each task fully before starting the next, including any tests the
  task implies and its local verification (see `CONTRIBUTING.md`: build, the
  relevant `ctest`/doctest filter, `clang-format` on the files you touched,
  and the e2e harness for anything a player would see, per `STATE.md`'s
  standing priority).
- **Commit after each task** with a clear message, so each task is its own
  reviewable step. Push regularly, because the container is temporary.
- Stay within the list. If you notice unrelated problems, write them down for
  the final summary or `STATE.md` instead of fixing them on the side.

### 2. Update the documentation

After all tasks are implemented, bring the docs up to date with what changed:

- **`README.md`**: features, flags, build options and anything else
  user-facing that changed.
- **`REMAINING_TASKS.md`**: tick the finished items `[x]` (the convention is
  "landed and verified") and add any new follow-ups.
- **`STATE.md`**: any gotchas or landmines you ran into, and current status.
- **`ARCHITECTURE_SPEC.md`**: anything that changed the design or resolved an
  open question.
- **Subsystem docs** under `docs/`, such as `docs/lua-api.md` for new
  bindings, `docs/protocol.md` for wire changes (along with the
  `kEngineProtocolVersion` bump) and `docs/e2e-automation.md` §11 for
  automation work.
- If any of the three core agent docs ends up at 500 lines or more, **dream**
  as described in `DREAMING.md`, in its own docs-only commit.

### 3. Open a pull request

- Push the branch, then create the PR (with the GitHub MCP tools in cloud
  sessions) against the default branch. Autopilot counts as the user's
  explicit request for a PR.
- If the repo has a PR template, follow it. If not, the body should include:
  the task list with each item ticked off, a short summary of each change,
  how you verified it (the exact tests and commands), and anything you did
  differently from the plan or left open.
- Subscribe to the PR's activity (`subscribe_pr_activity`) so CI results and
  review comments wake the session.

### 4. Drive CI/CD to green

- The `Builds` workflow (`.github/workflows/runner.yml`) runs on every PR:
  lint (`clang-format`), then Linux, macOS and Windows builds, then the
  bundle step. `Auth against real Keycloak` also runs on PRs that touch auth.
  **Every check must pass.**
- When a check fails, read the job logs, reproduce the failure locally where
  you can, fix the root cause, run the same local checks again, and push.
  Repeat until everything is green. "Flaky" doesn't count as a diagnosis.
- Never skip, disable or quarantine a test to get to green. Never push an
  empty commit, and never close and reopen the PR to restart CI.
- If a failure also happens on the base branch, or can't be fixed from this
  PR (for example a runner outage or a missing secret), leave one comment on
  the PR. Name the failing check, say why the failure isn't from this PR, and
  include a proposed fix if you have one. Then tell the user.
- You're done when **CI is green on the latest commit, the PR can be merged
  without conflicts, and no review threads are waiting on you**.

## Ambiguity and blockers

Autopilot means you rarely ask. It doesn't mean you never ask.

- **Small ambiguity** (naming, file placement, a reasonable default): pick the
  option that matches the existing code, write the choice in the PR body, and
  keep going.
- **Real blocker** (the plan contradicts itself or the code, an item needs a
  product or design decision, or a step can't be done in this environment):
  finish every task that doesn't depend on it, then report the blocker
  clearly in the PR body and in your final message. Don't guess at a
  decision that is only the user's to make, and don't drop the task silently.

## Final report

End with a short message to the user that contains the PR link, the task list
with its status, anything deferred or blocked and why, and the CI status.
That message and the PR are what the user reads first in their review.
