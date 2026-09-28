# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here. Keep this file lean
> (~300-400 lines) — new verbose writeups belong in `state/*.md`, linked from
> here, not pasted inline. See "Detail files" at the bottom for the index.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.
>
> **If what you learned is tied to a specific physical machine** (a tool
> path, an agent-shell/Bash-vs-PowerShell quirk, which pre-built `build-*`
> dirs exist, a local toolchain oddity) **write it to `STATE.md.local`
> instead of here** — see that file's own header for why. This file (and
> `state/*.md`) is for gotchas that hold regardless of which machine an
> agent is running on.

---

## Standing priority: prefer engine (C++) work over content (Lua)

When picking what to work on next and multiple open items are roughly equally
ready (e.g. choosing among REMAINING_TASKS' several `[ ]` items with no other
constraint forcing one), **prioritize a task that changes the engine (C++
code under `src/`/`inc/`) over one that's purely `content/*.lua` policy/
content work.** Most remaining Lua-only gaps (PvP/mob damage policy, hunger,
more base-pack art/reskins, etc.) are content authors' problems the engine
already has the primitives for; the engine surface itself is where real
mechanism gaps still exist and where a wrong call is much more expensive to
unwind later. This is a standing preference for ambiguous "what next"
choices, not a rule to force an engine change where a task is inherently
content-only (e.g. the user explicitly asks for a content-pack feature).

---

## Current status (2026-09-28)

**Closed the Cross-Cutting "Perf budget checks" item (the mesh-time/
snapshot-size half of it): `tests/unit/perf_budget_test.cpp` adds two
budget-gate `TEST_CASE`s to the normal `vb_tests` run.** Picked per this
file's own standing priority (engine work over content) from
`REMAINING_TASKS.md`'s remaining `[ ]` items right after the soak test
below — this was the last concrete, unblocked, non-continuous engine gap
left in the whole backlog (everything else remaining is blocked on
infrastructure this agent environment doesn't have, inherently continuous,
or content policy).
**Chunk mesh time:** generates a real chunk via `WorldGenerator` at
`gen.surface_height(16, 16) / kChunkDim` -- **not** a fixed guessed chunk-y
(an earlier draft hardcoded y=2 assuming "base_height=64 puts real terrain
inside [64,96)" and it happened to land on a chunk that's *entirely open
sky* for this seed at column (0,0); `mesh_chunk()` legitimately returned an
empty mesh, and this test's own `REQUIRE_FALSE(mesh.empty())` sanity check
caught it immediately rather than silently passing for the wrong reason).
Meshes it 3x and keeps the fastest run (discards scheduling noise the same
way a real micro-benchmark would, without pulling in a benchmarking
library for a 3-sample gate), asserts `< 100ms`. Measured ~21ms in this
environment's unoptimized Debug build -- real headroom, this isn't a
hair's-width pass that a slightly slower CI runner would flip.
**Snapshot size:** encodes a 50-entity `S2CEntitySnapshot` (deliberately
busier than any single player's `InterestGrid` cell radius would
realistically ever surface at once -- a worst-case shape, not a typical
one) and checks the encoded byte size against a per-entity budget.
Measured ~52 bytes/entity; the check uses 90 bytes/entity + a fixed
256-byte allowance, leaving room for real future growth (more
`EntityRecord` fields) while still catching an actual regression -- e.g. a
change that starts sending `visual_override` on every record instead of
only `entered` ones, breaking `snapshot.hpp`'s own documented "absent
entirely costs one bool on the wire" invariant.
**Frame time deliberately left out, not attempted as a stand-in:** real
rasterization/GPU cost needs a live GL context to mean anything at all --
same already-documented "no GUI in this agent environment" caveat every
other rendering-adjacent item in this file carries, not a corner cut
quietly. What CPU-side work *can* be measured without a GPU (chunk
meshing, snapshot encoding) is exactly what the two checks above cover.
Deliberately a **budget gate, not a benchmarking dashboard**: no
historical tracking, no graphs, nothing scheduled separately from the
normal `vb_tests` run -- matches this item's own "simple benchmark harness"
framing, not a heavier one.
Verified: full `vb_tests` 415/415 green (2 new cases), clean `-Werror`
build of `vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed
clean, reconfigured back to this dir's OFF default afterward).

Before that, most recent landed item was **closing the Cross-Cutting "Soak test target" item: `tests/unit/soak_test.cpp`
now runs N simulated clients doing a random walk + edits, folded into the
normal `vb_tests` run instead of a separate nightly/pre-release job.**
Picked per this file's own standing priority (engine work over content)
from `REMAINING_TASKS.md`'s remaining `[ ]` items after the manifest-
staleness pass below — this and "perf budget checks" were the last two
concrete, unblocked, non-continuous engine gaps left (the rest is blocked
on infrastructure this agent environment doesn't have, or is inherently
continuous/no-single-PR-closes-it, or content policy).
**Shape:** 4 simulated `ClientSession`s over a real `LoopbackNetwork` +
`ServerSession` + `WorldReplicator` + `World` (`WorldGenWorkerPool::
kSynchronous`, same pattern `blockedit_test.cpp` already established),
random-walking with a fixed-seed `std::mt19937` (reproducible, not flaky)
and occasionally pushing a break/place block edit, for 150 ticks, then all
disconnecting. Every tick advances simulated time directly via `tick(0.05)`
calls -- no real sleeps, same "pump" pattern every other session-stack test
in this suite already uses.
**What it actually checks**, matching ARCHITECTURE_SPEC.md's own soak
description ("watch for leaks... and unbounded queue growth"): every client
stays joined the whole run with no connection stuck mid-handshake
(`player_count()`/`pending_count()`); each client's `pending_edit_count()`/
`unacked_input_count()` stay bounded across all 150 ticks rather than
growing (a real queue-growth bug -- an ack that stops being sent, an edit
result that never arrives -- would blow well past the generous bound this
test checks); `World::loaded_coords()` stays bounded rather than growing
roughly linearly with tick count (proving chunk unload keeps working under
sustained churn, not just chunk load); and a final round confirms
`player_count()`/`pending_count()` both return to exactly 0 once every
client disconnects -- no leaked `Conn` entry per soak client that ever
joined. Real ASan/LSan leak detection comes free from this simply being
part of `vb_tests`, which the Linux CI matrix already runs under
`-DVB_ENABLE_ASAN` (this same day's earlier CI pass) -- no separate
sanitizer wiring was needed for that half of the spec's description, only
the "watch queue growth" half needed new code at all.
**Sizing this took real iteration, not a first-try guess:** an initial
6-client, 400-tick draft with clients spread 40 blocks apart (each forcing
its own distinct set of chunk columns) measured **~55 seconds of real CPU
time on its own** -- confirmed via `time`, with `user`/`sys` both reporting
near-zero the whole run (Git Bash's `time` builtin doesn't correctly
attribute a native Win32 child process's CPU time on this platform, a real
environment quirk worth knowing about before trusting a `0.000s user` next
to a large `real` on this machine) -- because real fBm terrain generation
in an unoptimized Debug build, not the session/queue logic actually being
soaked, dominates this test's cost. Scaled down to 4 clients kept only 8
blocks apart (heavily overlapping interest, so far fewer distinct chunk
columns ever get generated) and 150 ticks: ~14s standalone, against a ~70s
baseline for the other 412 tests combined -- a proportionate addition, not
a suite-doubling one. Verified: full `vb_tests` 413/413 green (1 new case),
clean `-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
confirmed clean, reconfigured back to this dir's OFF default afterward).
**Gotcha hit while verifying, same category as this file's other real-UDP
notes:** running the full suite with `--test-case-exclude="*real UDP*"`
right after this change still ran unusually slowly (~90s+ with long gaps
between log lines) before finishing clean -- the exclude filter apparently
doesn't catch every GNS-touching case, and this environment's shared
per-process GNS global state makes sequential runs flaky in duration (not
in outcome) the same way this file's §3/`STATE.md.local` real-UDP hang note
already describes. Not a regression from this change -- confirmed by
watching the log advance through unrelated test names the whole time, never
truly stuck, and it did eventually reach the soak test and finish 413/413
green.

Before that, most recent landed item was **closing Phase 4's "Manifest staleness" gap: a pack that writes `vb.storage`
after startup no longer permanently stales the asset manifest.** Picked per
this file's own standing priority (engine work over content) from
`REMAINING_TASKS.md`'s remaining `[ ]` items, after the git-tag/doc-fix pass
below — this was the one real, concrete engine correctness gap left that
wasn't blocked on infrastructure (macOS CI, a live GUI) or continuous/
no-single-PR-closes-it (soak tests, perf budgets).
**The actual bug:** `src/server/main.cpp` already fixed the *load-time* half
of this back on 2026-09-18 (flushing `vb.storage` before building the
manifest, so the very first hash is correct) — but the manifest itself was
still built exactly once and handed out as a fixed `shared_ptr` for the rest
of the process's life. `storage.json` lives directly under `content_pack`
(same directory `build_manifest()` recursively scans), so it's a real
manifest entry, not metadata outside it. Any *later* `vb.storage` write
(any tick after startup, not just load time) silently rewrites that file on
disk while the already-built manifest keeps advertising its old hash —
`host.asset_file_bytes` reads current bytes off disk at request time, so a
client that syncs after that point downloads bytes that don't match the
hash the manifest told it to expect, and asset-sync verification fails with
the exact `"asset transfer failed (hash mismatch or size cap)"` text this
file's §4 gotcha list already documents for a *different* root cause (see
that entry's own note: "this error string has (at least) two unrelated root
causes").
**The fix:** new `PackRuntime::storage_revision()` (`inc/vb/script/
pack_runtime.hpp`/`.cpp`) — a monotonic counter bumped inside `Impl::
flush_storage()` every time it actually writes to disk, whether called
explicitly (the startup flush) or from `dispatch_tick()`'s own internal
"if `storage_dirty()`, flush" auto-flush. `src/server/main.cpp` polls it
once a second (`manifest_check_ticks`, deliberately throttled rather than
checked every tick — same "periodic backstop, not hard real-time" shape
`autosave_ticks` already established, and a full manifest rebuild rescans
+ rehashes *every* asset file, not just `storage.json`, so doing it at the
tick rate for a pack that flushes every tick would be real, avoidable
work this item's own text never asked for). On a change, it rebuilds the
manifest and atomically swaps it into a new small `ManifestHolder`
(mutex + `shared_ptr<const Manifest>`) that `host.asset_manifest`/
`host.asset_file_bytes` now read through by reference each call, instead of
each lambda capturing a fixed `shared_ptr` by value at startup the way they
used to.
**Deliberately out of scope:** `--singleplayer` never builds a manifest at
all (same-process integrated server, no asset sync needed — confirmed by
grep, `src/client/main.cpp` has zero references to `build_manifest`/
`asset_manifest`), so this gap and its fix are both server-only. The
manifest-rebuild-on-a-live-server code path itself (not just
`storage_revision()`'s own counting, which is directly unit tested) was not
exercised end-to-end — `src/server/main.cpp` isn't linked into `vb_tests`,
same pre-existing gap every other `main.cpp`-only change in this file
already has; no GUI/live-server harness in this agent environment to drive
it further.
Verified: full `vb_tests` 412/412 green (1 new `pack_runtime_test.cpp` case:
`storage_revision()` bumps on an explicit `flush_storage()` call and on
`dispatch_tick()`'s own implicit auto-flush, but *not* on a no-op tick with
nothing dirty — the property the whole point of a revision counter over a
plain "rebuild every tick" approach depends on), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). **Gotcha hit while
verifying:** re-running the full suite a second time against the `-Werror`
build (immediately after the first, already-green run against the
non-`-Werror` build) hung with zero output for several minutes before
producing anything — matches this file's own already-documented real-UDP/
GNS Windows-Firewall-prompt hang (§3/`STATE.md.local`), triggered by shared
per-process GNS global state across sequential test runs, not a regression
from this change; killing the stuck `vb_tests.exe` and re-running fresh
produced the same clean 412/412 (just slower than the first run, ~90s of
real-UDP-test churn before the summary printed instead of near-instant).

Before that, most recent landed item was **closing Phase 0's last concrete remaining item, "First `git tag v0.0.1`",
and fixing two stale cross-references left over from earlier the same day's
`RegionStore`/LZ4 passes.** Picked from `REMAINING_TASKS.md`'s remaining
`[ ]` items after the ASan/UBSan/TSan CI pass below — everything else still
open there is either blocked on infrastructure this agent environment
doesn't have (macOS CI, a live GUI for the two-window playtest), genuinely
continuous/no-single-PR-closes-it (the Cross-Cutting section's soak-test/
perf-budget/protocol-lockstep lines), or content policy (PvP/hunger,
deprioritized by this file's own standing engine-over-content preference).
**The tag:** `git tag -a v0.0.1 -m "..."` — local annotated tag only,
**not pushed to `origin`** (pushing a tag is a shared/visible action, left
for a human to do deliberately via `git push origin v0.0.1` — that's also
what will actually exercise `bundle`/`publish` in CI for the first time,
since neither has ever run against a real tag before). `git describe --tags`
now returns `v0.0.1` instead of erroring `fatal: No names found, cannot
describe anything`.
**The stale cross-references:** `REMAINING_TASKS.md`'s Cross-Cutting §18
line and `ARCHITECTURE_SPEC.md` §18 row 5 both still said `--singleplayer`'s
`RegionStore` wiring (and, in the `ARCHITECTURE_SPEC.md` case, region-file
LZ4 framing itself) was "still open" — both actually landed earlier the same
day (2026-09-28, see the two "Before that" entries below this one). Only
`remaining_tasks/deferred.md` had already been updated to say "Resolved
2026-09-28" when that item landed; the two docs that name it from the
*question/backlog* side, rather than the *changelog* side, didn't get the
matching edit in the same pass. Both fixed to read "no open sub-item left."
No code changed, no test run required — pure doc correction plus one git
tag command.

Before that, most recent landed item was **adding Linux CI jobs that build and test under ASan/UBSan and under TSan,
closing the Cross-Cutting "Sanitizer (ASan/UBSan) debug CI job; TSan job for
the threaded subsystems" item in `REMAINING_TASKS.md`.** Picked per this
file's own standing priority (engine work over content) — this was the one
remaining Cross-Cutting item that was both concrete and fully unblocked (the
`vb_sanitizers` CMake target and `VB_ENABLE_ASAN`/`VB_ENABLE_UBSAN`/
`VB_ENABLE_TSAN` options already existed, linked into every first-party
target — nothing ever turned them on in CI). `.github/workflows/build_linux.yml`
gained two new `build_type` matrix entries (`asan`, `tsan`, both
`RelWithDebInfo`) running the exact same `ctest --output-on-failure` the
existing `release`/`debug` entries already run, with binary staging/artifact
upload skipped for the two sanitizer entries (not real distributables).
Deliberately Linux-only: `cmake/Sanitizers.cmake` already documents MSVC has
no UBSan/TSan at all, and macOS CI doesn't build `VB_WITH_NET` yet (this
file's own still-open Phase 1 item) — Linux is the only platform where all
three sanitizers and the full net-enabled build coexist. **Caveat, same
category as every GUI-adjacent item in this file but for CI instead of
rendering: not verified by an actual GitHub Actions run** — this agent
environment can't trigger/observe one. The change only adds `-DVB_ENABLE_*`
flags to the same configure/build/test steps the already-green release/debug
entries use, so it should work, but the *point* of adding it is to let a real
run surface genuine findings (a real race, real UB, or a third-party
FetchContent'd lib not tolerating being linked against instrumented code)
that this local session has no way to preempt. See `REMAINING_TASKS.md`'s own
entry for the full writeup.

Before that, most recent landed item was **wiring `--singleplayer`'s integrated server to a real `RegionStore`, closing the last item in `remaining_tasks/deferred.md`'s
world-persistence writeup ("`--singleplayer`'s integrated server wired to
`RegionStore`").** Picked per this file's own standing priority (engine C++
work over content Lua) from the remaining open items — everything else
still open in `REMAINING_TASKS.md`/`deferred.md` was either process/build
metadata (Phase 0's `git tag`), blocked on infrastructure this agent
environment doesn't have (macOS CI, a live GUI for the two-window playtest),
or genuinely content policy (PvP/hunger). This was the one concrete,
unblocked engine gap left.
**What was actually missing:** Phase 7.6 (2026-09-25) wired a `RegionStore`
into `src/server/main.cpp` only — `src/client/main.cpp`'s `Singleplayer`
struct (the in-process integrated server `--singleplayer` drives) never got
one, so every singleplayer session's edits were lost the instant the process
exited; the world regenerated from scratch (including any placed/broken
blocks) on the next launch. `deferred.md`'s own tracked line already named
this exact gap.
**The fix, mirroring `src/server/main.cpp`'s own shape as closely as
possible:** `Singleplayer` gained a `std::unique_ptr<vb::world::RegionStore>
region_store` member, declared immediately before `server` (members destroy
in reverse declaration order, so it's guaranteed to outlive the
`WorldReplicator` that `server` owns and holds a raw, non-owning pointer to
it via `ChunkLifecycleSystem::set_region_store`). Constructed in the
constructor body under a new fixed `kSingleplayerWorldDir` ("world_singleplayer")
-- distinct from a dedicated server's own default "world" dir so running both
from the same working directory never collide -- then wired into the
replicator the same way `src/server/main.cpp` does
(`replicator->set_region_store(region_store.get())`, right after
`set_reach()`, right before `server.set_world_replicator(std::move(replicator))`).
**No `client.toml` surface to disable it yet** -- unlike the dedicated
server's `persist_world` config toggle, `--singleplayer` has no config file
on this in-process path at all (same "no ServerConfig here" gap every other
`Singleplayer`-side comment in this file already notes), so persistence is
unconditionally on; a real follow-up, not a cut corner of this pass.
**Autosave, mirroring `src/server/main.cpp`'s own periodic sweep +
unconditional shutdown save:** a new `kAutosaveIntervalSeconds` (60s, same
default as the dedicated server's `autosave_interval_seconds`) accumulator
inside `Singleplayer::tick()` sweeps every loaded chunk through
`save_if_dirty()` + one `flush()` once enough real time has accumulated --
otherwise a chunk that never unloads (a player idling in one spot for a
whole session) would only ever reach disk via the final save below. A new
`~Singleplayer()` destructor does that same unconditional final sweep on the
way out (quitting to the main menu, or closing the whole app), mirroring
`src/server/main.cpp`'s own unconditional `autosave_sweep()` call right
before it returns.
**Verified, not just wired:** confirmed the exact same "only persist edits"
invariant Phase 7.6's own writeup manually verified for the dedicated server
holds here too -- ran a real `voxel_browser.exe --headless --singleplayer
--frames 2 --name CiBot --render-distance 2` from a scratch working
directory (no block ever edited, since `--frames 2` is too short for any
input to land) and confirmed no `world_singleplayer/` directory was created
at all, matching `Chunk::revision() == 0` meaning "still exactly what
worldgen produced, nothing to persist" -- not a missed case. The underlying
mechanism itself (`RegionStore` wired into `ChunkLifecycleSystem` via
`WorldReplicator`, saving a real edit across an unload+reload cycle) already
has full end-to-end test coverage from Phase 7.6's own pass
(`tests/unit/region_store_test.cpp`'s "a block edit survives a real
unload+reload cycle... via WorldReplicator" case) -- this pass reuses that
exact same, already-tested mechanism from a new call site
(`Singleplayer`), so no new test was added for the wiring itself; `Singleplayer`
lives entirely inside `src/client/main.cpp`, not linked into `vb_tests`,
so it isn't directly unit-testable the way `RegionStore`/`ChunkLifecycleSystem`
are. Full `vb_tests` 411/411 green (run with
`--test-case-exclude="*real UDP*"` per this file's own noted Windows
Firewall gotcha for this agent environment), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). **The actual
gameplay-visible result (a human placing/breaking a block in a real
singleplayer session, quitting, relaunching, and seeing the edit still
there) was not manually eyeballed** -- no GUI in this agent environment,
same still-open caveat as every other rendering-adjacent pass in this file;
what *was* verified directly is the "no directory created when nothing was
edited" half and the already-tested underlying save/load mechanism, not the
full human-visible round trip.

Before that, most recent landed item was **fixing `default_spawn_position()` spawning players in open ocean.** User-
reported: "please ensure the spawn point to be on land." Root cause:
`worldgen::default_spawn_position()` (`src/worldgen/generator.cpp`) always
stood the player on the exact `(spawn_x, spawn_z)` column's surface (default
`(0, 0)`) with no check against sea level — if that column's terrain height
happened to be at or below sea level (a very plausible ocean column for an
arbitrary seed), the player spawned floating in open water. **The fix:** it
now spirals outward from the requested column in growing square rings,
checking each column's `surface_height(x, z) > sea_level` (new
`WorldGenerator::sea_level()` accessor — pipeline's own value when a
`PackWorldGenPipeline` is attached, else `WorldGenParams::sea_level`), and
spawns on the first dry-land column found (capped at a 256-ring search,
falling back to the original column if truly nothing turns up — pathological
case only). Callers (`src/server/main.cpp`, `src/client/main.cpp`
`--singleplayer`) needed no changes; they just call
`default_spawn_position(generator)` and get the fixed behavior for free.
Updated `tests/unit/worldgen_test.cpp`'s two spawn tests to stop assuming
the spawn column is always exactly `(spawn_x, spawn_z)` (seed 1's origin
column is actually ocean, which is what surfaced this bug once the land
check was added) and added a new regression test asserting the spawn
surface is always above sea level. Verified: full `vb_tests` 414/414 green.

Before that, most recent landed item was **region file LZ4 framing, closing
`remaining_tasks/deferred.md`'s "region file LZ4/zstd framing" item and
unblocking `ARCHITECTURE_SPEC.md` §18 row 5's remaining open half.** Picked per this file's own standing priority (engine
work over content) right after the wire-compression pass below landed and
its own writeup explicitly named this as the next unblocked follow-up (region
file payloads still carried bare RLE with no LZ4 framing of their own).
**The change:** `RegionStore::flush()` (`src/world/region_store.cpp`) now
runs each entry's chunk-codec payload through a local `compress_for_disk()`
helper — the same threshold-and-only-if-it-helps policy `net::frame_message()`
already established for wire messages (`kCompressionThresholdBytes = 128`,
compress via `protocol::compress_lz4`/`decompress_lz4`, keep the compressed
bytes only if they're actually smaller) — before writing an entry, rather
than sharing a function with `net/`: `world/` has no business depending on
`net/` for one size constant, so this is a deliberate small duplication, not
an oversight. On-disk format version bumped **1 -> 2** to add a per-entry
`u8 flags` byte (bit 0 = `kEntryCompressed`) right after the existing
revision field; a version-1 file (written before this pass, no flags byte)
is still read correctly by treating its flags as always 0 (uncompressed) —
`region_for()` accepts either `kVersion` or `kVersionNoFlags` on read, only
ever writes `kVersion`. **Gotcha this pass had to get right:** the corrupt-
file handling that already existed (any read failure -> warn + treat the
whole region as empty, never fatal) had to be extended to a mid-read LZ4
decompression failure too (a new `entry_corrupt` flag threaded through the
read loop) — and to a compressed entry reaching a binary built *without*
`VB_WITH_COMPRESSION` at all, which is treated as corrupt rather than fed raw
LZ4 bytes to `chunk_codec::decode_chunk_payload` (same "a compressed frame
reaching a build that can't decompress it is a real error, not a silent
misinterpretation" posture `net/session.cpp`'s `decompress_frame_payload()`
already established for wire messages). In-memory `Entry::payload` is always
the *uncompressed* chunk-codec blob regardless of what's on disk — compression
is purely a write-time/read-time disk-format detail, so `save_if_dirty()`'s
existing revision-based dedup logic (skip re-encoding a chunk already cached
at its current revision) needed no change at all.
Verified: full `vb_tests` 410/410 green (2 new `region_store_test.cpp`
cases — a synthetic per-voxel-random 4-block-type "swiss cheese" chunk (the
same shape the wire-compression measurement below used) round-trips through
a real save/flush/reopen/load cycle, proving compression is transparent to
the reader; and a hand-built version-1-format file, written the way this
pass's *old* code would have, still loads correctly through the *new* code),
run with `--test-case-exclude="*real UDP*"` per this environment's known
Windows Firewall gotcha, clean `-Werror` build of `vb_tests`/`voxel_browser`/
`voxel_browser_server` (temporarily reconfigured `build-net-lua` with
`-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
dir's OFF default afterward). Updated `ARCHITECTURE_SPEC.md` §18 row 5,
`architecture_spec/open-questions.md`'s matching writeup, `REMAINING_TASKS.md`'s
Phase 7.6 entry and its Cross-Cutting §18 line, and
`remaining_tasks/deferred.md` to match. **Still genuinely open, not touched
by this pass:** `--singleplayer`'s integrated server still has no
`RegionStore` at all (a separate wiring gap, not a compression-format one);
zstd was never implemented or measured here either, same reasoning as the
wire-compression pass below.

Before that, most recent landed item was **wiring real LZ4 wire compression, resolving ARCHITECTURE_SPEC.md §18 Q4
("Chunk compression: LZ4 vs. zstd vs. palette-only") — the one item that
section's own cross-cutting line called "still fully open."** Picked per
this file's own standing priority (engine work over content) after closing
the stale "cross-chunk relight on edit" item below left Phase 5 with nothing
else engine-shaped open; this was the next concrete, real engine gap still
tracked anywhere in the docs (Phase 0's `git tag v0.0.1` and the
`CMAKE_POLICY_VERSION_MINIMUM` shim are process/build-metadata items, not
code; Phase 1's macOS universal-protobuf CI gap needs a real macOS runner
this agent environment doesn't have).
**What was actually missing:** `VB_WITH_COMPRESSION` already linked LZ4
(alongside xxHash, for asset-sync manifest hashing) and `chunk_codec.hpp`'s
own header comment already described the intended shape ("then optionally
LZ4-framed by the caller via MessageFlag::kCompressed") — but
`MessageFlag::kCompressed` (`inc/vb/protocol/message.hpp`) was pure unused
scaffolding: grepping the whole `src/` tree for it turned up nothing outside
that one enum definition. No message had ever actually been compressed.
**The fix, scoped generically rather than chunk-specific:** new
`vb::protocol::compress_lz4`/`decompress_lz4` (`inc/vb/protocol/
compression.hpp`, `src/protocol/compression.cpp`, compiled only when
`VB_WITH_COMPRESSION`) -- a u32 LE original-size prefix + one LZ4 block
(LZ4's block API has no length of its own). Wired into the single shared
`vb::net::frame_message()` helper every message type already funnels
through to get framed (`inc/vb/net/handshake.hpp`), not into chunk messages
specifically: above a new `kCompressionThresholdBytes` (128 bytes) *and*
only when compressing actually shrinks the payload -- both conditions
matter, see the measurement below for why the second one is load-bearing,
not defensive paranoia. `decompress_frame_payload()` (a new anonymous-
namespace helper in `src/net/session.cpp`, shared by both `ServerSession`'s
and `ClientSession`'s `kMessage` handling) reverses it transparently right
after `read_frame()`, before any per-type `decode()` call runs -- every
existing decode call site needed zero changes. A frame arriving with the
flag set on a binary built without `VB_WITH_COMPRESSION` is treated as a
real error (dropped/disconnected with a clear reason) rather than silently
fed to a struct decoder as raw LZ4 bytes.
**Measured, not guessed, closing the item's own "start LZ4, measure"
instruction:** a throwaway scratch `TEST_CASE` (built, run, and deleted
again -- not committed, per this project's own no-debug-scaffolding
convention) against real `WorldGenerator` output found that a real surface
chunk (smooth fBm heightmap, no caves) already RLE's its 32768-byte raw
volume down to ~12-14 bytes on its own -- LZ4 on top of *that* comes out
larger (17-19 bytes, all fixed per-call overhead), which is exactly why the
threshold + "only if it helps" gate exists rather than compressing
unconditionally. The case that actually benefits is a heavily-edited,
low-run-length chunk: a synthetic per-voxel-random 4-block-type "swiss
cheese" chunk (no long runs anywhere, the realistic shape of a well-mined
area) measured at 50832 bytes RLE'd, 32393 bytes RLE+LZ4'd -- a real ~36%
further reduction in exactly the case that matters for bandwidth on a
heavily-played world. This is why the resolution is "LZ4 above a threshold,
conditionally" rather than "always LZ4" or "zstd instead" -- the measured
numbers gave no reason to reach for zstd's extra dependency/complexity cost
for a marginal further win on an already-small payload.
Verified: full `vb_tests` 408/408 green (4 new cases: `protocol_test.cpp`'s
`compress_lz4`/`decompress_lz4` round-trip + truncated-buffer-rejection
cases; `net_test.cpp`'s `frame_message` threshold-and-only-if-it-helps
policy cases against a real `S2CChat` message, both the
compressed-large-payload and left-uncompressed-small-payload sides), clean
`-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
confirmed clean, reconfigured back to this dir's OFF default afterward).
Updated `ARCHITECTURE_SPEC.md` §18's Q4 row, `architecture_spec/
open-questions.md`'s full writeup (including fixing its Q5/persistence
entry's now-stale "§18 Q4 stays open" cross-reference), and
`REMAINING_TASKS.md`'s cross-cutting §18 line to match. **Deliberately
out of scope, left as real follow-ups, not cut corners:** region file
payloads (`RegionStore`, Phase 7.6) still carry the bare RLE codec with no
LZ4 framing of their own -- only network frames go through
`frame_message()`'s new path; that's `remaining_tasks/deferred.md`'s
already-tracked "region file LZ4/zstd framing" item, now unblocked rather
than resolved by this pass. zstd itself was never implemented or measured
against for the same reason noted above.

Before that, most recent landed item was **confirming + closing the stale "cross-chunk relight on edit" item in Phase 5
(no code change to the mechanism itself — it was already correct).** Picked
per this file's own standing priority (engine work over content) from the
remaining open items; this was the last engine-shaped item still marked
`[~]`/open in Phase 5 (everything else left there is either pure content
policy or the still-genuinely-open "live two-window manual playtest," which
needs a human and a GUI this agent environment doesn't have).
The item's own text ("breaking a floor lets light into the chunk below —
still per-chunk from scratch each edit") dated back to 5.2 (2026-09-16),
before Phase 2's horizontal cross-chunk light propagation pass landed
earlier the same day as this check (2026-09-28, see that entry further down
this file). That pass's own writeup already noted `relight_column`'s
unconditional *vertical* cascade had existed since 2026-09-15 — a full tick
before this Phase 5 item was even written — so the "still TODO" framing was
wrong even at the time, not just stale by the time this session re-checked
it: `WorldReplicator::apply_block_edit()` (`src/net/world_replicator.cpp`)
has called `lighting::relight_column` (which cascades down through every
consecutively-loaded chunk below the edited one, not just a single
`relight_chunk` call on the edited chunk alone) on every real edit for that
entire time. Same pattern as this file's earlier "`ENGINE_PROTOCOL_VERSION`
mismatch" find: a real gap that closed as a side effect of unrelated work,
never picked up as its own line item until an agent happened to re-read the
code behind a stale backlog entry.
**Verified with a new test, not just re-reading the code:** added
`tests/unit/world_replication_test.cpp`'s "breaking a floor block lets sky
light into the loaded chunk below it, in the same edit" — builds two
manually-constructed stacked chunks (a fully solid floor layer over an
otherwise-empty room below, bypassing `WorldGenWorkerPool` entirely so the
scenario is exact and not incidentally already-lit by real terrain), calls
the real `WorldReplicator::apply_block_edit()` to break one floor voxel, and
confirms the room below reads real sky light (`sky() > 0`) immediately
afterward — proving the cascade actually fires through the public edit path
end-to-end, not just that `relight_column` itself is capable of it (already
covered by `lighting_test.cpp`'s existing, more synthetic cases).
**Gotcha hit writing it:** `vb::world::base_block` is a namespace (a
collection of `inline constexpr BlockId` values), not a type or value --
`using vb::world::base_block;` fails to compile (MSVC `C2873: symbol cannot
be used in a using-declaration`); every existing call site in this file
already spells it out fully (`vb::world::base_block::stone`) for exactly
this reason, which this test now does too.
Verified: full `vb_tests` 404/404 green (401 pre-existing + this 1 new case
+ 2 pre-existing skipped real-UDP `gns_transport_test.cpp` cases, run with
`--test-case-exclude="*real UDP*"` per this file's own noted Windows
Firewall gotcha for this agent environment), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). Updated
`REMAINING_TASKS.md`'s Phase 5 entry and `remaining_tasks/phase5.md`'s own
`[~]` line and 5.2 status paragraph to match. **Still genuinely true, left
open by this pass on purpose:** `relight_chunk()` itself always recomputes a
whole chunk's light from scratch on any relight, never incrementally from
just the edited voxel outward — a real perf characteristic, but a
deliberate, already-documented one (`lighting.hpp`'s own header comment
calls loaded columns "shallow in practice... the extra relight_chunk() calls
this costs... are cheap"), not the correctness gap this item's stale text
conflated it with.

Before that, most recent landed item was **per-block hardness/tool break-time variation, closing Phase 5's "vary by
block/tool" gap.** Picked per this file's own standing priority (engine
C++ work over content Lua) from the remaining open items. Per-block
hardness already existed (`BlockType::max_damage`, Phase 6.5) -- what was
actually still missing was the matching "tool" half: `ServerSession::
punch()` (`inc/vb/net/session.hpp`, `src/net/session.cpp`) always counted
exactly one hit per swing against a block's `max_damage`, with no seam at
all for a pack to make some swings count for more. New optional
`block_damage` parameter (default 1, `std::uint16_t`, every pre-existing
call site/test unaffected) — a `max_damage > 0` block's punch counter now
accumulates by `block_damage` instead of a hardcoded `++`; a `max_damage
== 0` (instant-break) block stays unaffected by it entirely, same as
before (there's no "partial" instant break). `player:punch(block_damage)`
(`src/script/pack_runtime.cpp`, `sol::optional<int>`, validated `1..65535`
-- `entity:punch(): block_damage must be between 1 and 65535` otherwise)
is the pack-facing half: a pack decides how much a given swing counts for
however it likes (e.g. looking up `player:get_held_item()` against a
Lua-side tool table), with **no tool/hardness concept added to the engine
itself** -- the same "generic primitive, not a game-specific concept"
posture `vb.combat.set_params`'s other knobs already have.
**Deliberately out of scope:** PvP damage is entirely unaffected by this
parameter -- `damage_player()`'s call site inside `punch()` never reads
it, so a pack's tool-damage table only ever changes mining speed, never
combat balance; that stays `vb.combat.set_params`'s own `player_damage`,
a separate knob, on purpose (conflating the two felt like scope creep
past what this item was actually asking for). `content/base` itself was
not changed -- no tool items exist yet in any shipped pack (items are
still raw block ids, Phase 4's own still-open gap), so there's nothing
real to wire this up to there yet; this closes the engine-side mechanism
gap, not a content policy choice.
Verified: full `vb_tests` 406/406 green (a new `blockedit_test.cpp` case
drives `ServerSession::punch()`'s `block_damage` parameter directly --
breaks a `max_damage=3` block in 2 swings at `block_damage=2` instead of
3 at the default; a new `pack_runtime_integration_test.cpp` case proves
the same end-to-end through a real `vb.on("player_input")` handler calling
`player:punch(2)`, the same dispatch shape `content/base/mechanics.lua`
itself uses), confirmed clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed
clean, reconfigured back to this dir's OFF default afterward).

Before that, most recent landed item was **wall-clock `server_time_est` + smoothing on the client, closing Phase 3's
last remaining item.** Picked per this file's own standing priority
(engine C++ work over content Lua) from the remaining open items — this
one had been sitting blocked on "needs `GnsTransport` RTT" since Phase 3,
now unblocked by the 2026-09-28 `GnsTransport` real-UDP test-coverage pass
earlier today giving this session a reason to actually look at that
transport's API surface again.
**RTT plumbing:** `Transport` (`inc/vb/net/transport.hpp`) gained a new
`round_trip_time_seconds(ConnId) const` virtual, defaulted to `nullopt`
(same posture `remote_address()` already has) so `LoopbackTransport` needs
no override at all -- it genuinely has nothing to report, matching this
item's own "loopback has no latency to estimate" text.
`GnsTransport::round_trip_time_seconds()` (`src/net/gns_transport.cpp`)
overrides it with `ISteamNetworkingSockets::GetConnectionRealTimeStatus()`'s
own `m_nPing` (milliseconds, a running average GNS already maintains from
real round trips) -- `nullopt` on a failed lookup or before GNS has
measured a real ping yet (`m_nPing < 0`), never a fabricated number.
**The estimator:** new header-only `vb::net::ServerTimeEstimator`
(`inc/vb/net/server_time_estimator.hpp`, same pure-math/unit-tested-
without-a-live-session posture as `render::EyeHeightSmoother`) tracks a
running wall-clock estimate of "what tick the server is at right now":
`advance(dt_seconds)` keeps it progressing every `ClientSession::tick()`
call, snapshot or not; `on_snapshot(server_seconds, one_way_latency_seconds)`
nudges it toward what each arriving snapshot implies (that snapshot's own
tick converted to seconds, plus half the transport's RTT as the estimated
one-way transit time) via exponential smoothing (15% of the gap per
sample) rather than overwriting it outright, so jitter in real packet
arrival timing doesn't visibly kick it around frame to frame -- except the
very first sample of a session, or a gap bigger than 1 real second (a long
stall, a reused estimator after reconnect), which both snap outright
instead of slow-easing across an already-known-wrong span.
**The real bug this surfaced, not just the originally-scoped gap:**
`ClientSession::interpolated_pos()` (`src/net/session.cpp`) used to target
`last_server_tick_ - 1` directly -- but `last_server_tick_` only changes
when a new snapshot actually arrives, so the interpolation fraction `a`
was frozen solid between arrivals (always exactly 0 under a normal
steady 20 Hz stream, since `last_server_tick_` equals the freshly-arrived
`cur_tick` every time) and only ever visibly moved -- in one discrete step
-- on the frame a new packet happened to land, not smoothly across every
render frame in between as the interpolation delay was always intended to
produce. Retargeting it at `server_time_.estimate_seconds() * tick_rate`
instead (falling back to the old `last_server_tick_`-based math before the
first snapshot, i.e. `server_time_.primed()` is false) fixes this for any
transport, not only `GnsTransport` -- the smoothing itself only needed
real elapsed local time, not real network latency; RTT is what makes the
*offset itself* track true server time rather than just "ticks since the
last packet," per this item's own original scope.
Verified: full `vb_tests` 404/404 green (6 new
`server_time_estimator_test.cpp` cases covering `ServerTimeEstimator`'s
pure math directly: unprimed initial state, first-sample snap, smooth
`advance()` progression, easing toward a nearby sample instead of jumping,
convergence under several repeated samples, and a large-gap snap; 2 new
`gns_transport_test.cpp` cases: the `!VB_WITH_NET` stub always reports
`nullopt`, and a real-UDP localhost round trip reports a small non-negative
RTT once GNS has actually measured one), confirmed stable across 3
repeated full-suite runs (no flakes, real UDP included), clean `-Werror`
build of `vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed
clean, reconfigured back to this dir's OFF default afterward). The actual
smoother-in-practice remote-entity motion (a human watching another player
move over a real, jittery connection) was **not** manually eyeballed — no
GUI in this agent environment, same still-open caveat as every other
rendering-adjacent pass in this file; `netcode_test.cpp`'s existing
"remote entity keeps moving as the other client watches" case (unchanged,
still green) is the only indirect coverage of the render-facing behavior
itself.

Before that, most recent landed item was **the item grid widget for `UiRuntime`, closing REMAINING_TASKS' Phase 4 "item
grid widget for `UiRuntime` — needs a real item/inventory concept" gap.**
Picked per this file's own standing priority (engine C++ work over content
Lua) from the remaining open items — the blocker in that item's own text
("needs a real item/inventory concept") was already resolved by Phase 5.4's
inventory sync/hotbar work, leaving only the engine-side rendering gap
itself open. Followed this codebase's established "generic primitive, not
a baked-in concept" posture (the same one `kRect`/`kText` already have,
Phase 6.16) rather than inventing a dedicated grid layout concept
engine-side: a new `vb::script::WidgetType::kIcon` (`inc/vb/script/
ui_runtime.hpp`) draws exactly one registered block/item id's real atlas
texture at `x/y/w/h`, nothing else — an `item` field (a numeric block id,
default air) plus the existing `color` field reused as a tint multiplier
(default opaque white = no tint, so an untinted icon draws the real
texture colors unchanged). `content/base/ui/inventory.lua`'s known "the
item-grid widget the spec describes for this screen doesn't exist... a
list stands in" comment is now resolved: it composes a real item grid out
of `icon` plus `rect` (slot background/border) and `text` (the count
label), the exact same "compose it in Lua" shape Phase 6.16 already gave
the hold-to-break progress bar out of `rect`.
**Rendering plumbing:** `UiRenderer` had no texture to draw an icon from at
all before this — the client's block texture atlas lives entirely inside
`ChunkRenderer` (built once at join, handed over via `set_atlas()`), so
three new getters (`has_atlas()`, `atlas_texture()`, `atlas_rect_for(id)`,
the last following `underwater_tint()`'s existing bounds-check-with-
fallback shape) let `UiRenderer::draw()` reuse that same atlas instead of
building a second one just for UI. `UiRenderer::draw()` gained an optional
`const ChunkRenderer *atlas_source = nullptr` parameter — nullptr (or a
`ChunkRenderer` with no atlas set yet, e.g. a screen opened before the
join-time atlas build finishes) falls back to that block's flat
placeholder color (`fallback_color_for()`, already used elsewhere for the
identical "no real texture yet" case) via a plain `DrawRectangle` instead
of `DrawTexturePro`, never a crash or a blank slot. `src/client/main.cpp`'s
two real `.draw()` call sites (the modal-screen `ui_renderer` and the
always-on `hud_renderer`) both now pass `chunk_renderer.get()` — the
`--headless` client's own separate `ui_renderer` local (line ~687) is
never actually `.draw()`n on that path, so it needed no change.
Verified: full `vb_tests` 397/397 green (2 new `ui_runtime_test.cpp`
cases: an `icon` widget round-trips its `item` id and an explicit `color`
tint, and a missing `item` field defaults to air rather than garbage;
`content_pack_test.cpp`'s existing whole-pack load exercises the rewritten
`content/base/ui/inventory.lua` for a real syntax/load check), clean
`-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with
`-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
dir's OFF default afterward). The actual rendered icon grid (a human
opening the inventory screen and seeing real block textures in a real
window) was **not** manually eyeballed — no GUI in this agent environment,
same still-open caveat as every other rendering-adjacent pass in this
file. `ChunkRenderer`/`UiRenderer` themselves also stay untested directly
(both need a live GL context — same pre-existing posture as
`underwater_tint()`'s own C++ side, only its Lua-facing shape gets a unit
test).

Before that, most recent landed item was **the two-client replication test re-run over real `GnsTransport`, closing
Phase 1's own last remaining item ("the two-client replication test runs
over `LoopbackTransport` only; re-run over `GnsTransport`").** Picked per
this file's own standing priority (engine C++ work over content Lua) from
the remaining open items. Pure test-coverage gap, no production code
changed: `tests/unit/gns_transport_test.cpp` gained a new case ("two
clients within interest range replicate to each other, over real UDP
(GnsTransport)") mirroring `replication_test.cpp`'s existing
`LoopbackTransport` scenario (join, come into interest range, see each
other's position, move far away, get a removal) but driving two real
`GnsTransport` instances over real UDP loopback instead of the in-process
`LoopbackNetwork` — `ServerSession`/`ClientSession`/`InterestGrid` only
ever talk to the `Transport` interface, so nothing about the replication
path itself needed a real-UDP-specific code change. **One real timing
difference from the `LoopbackTransport` version's fixed-pump-count
style:** over real UDP, "entered interest" and a position update can land
in separate packets, so this test polls until the position actually
converges to the expected value rather than stopping at the first tick
presence is detected — confirmed this mattered, not just theoretical: an
earlier version of this test that stopped at bare presence passed in
isolation but flaked when run as part of the full suite (saw a stale
pre-`set_player_state` position), because GNS's global per-process state
and callback dispatch is shared with dozens of other tests running back to
back in the same process (`gns_transport.hpp`'s own header comment already
flags this "one shared global interface" posture). Verified: full
`vb_tests` 395/395 green, confirmed stable across 3 repeated full-suite
runs (no flakes) plus a clean `-Werror` rebuild of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed
clean, reconfigured back to this dir's OFF default afterward, same pattern
as every other recent phase).

Before that, most recent landed item was **punch-rate cooldown enforced engine-side, closing REMAINING_TASKS' Phase
6.18 "no punch-rate cooldown enforced engine-side" gap.** Picked per this
file's own standing priority (engine C++ work over content Lua) from the
remaining open items. New `net::ServerSession::PunchParams::
punch_cooldown_seconds` (`inc/vb/net/session.hpp`, default `0.0` = disabled,
so every pre-existing direct-`punch()` call/test is unaffected unless a pack
opts in) plus a matching per-connection `Conn::punch_cooldown_remaining`
timer, decremented once per server tick right next to `set_max_messages_per_
second`'s own `msg_tokens` refill loop in `system_network_io()`
(`src/net/session.cpp`) — same "gate a per-connection resource with a
per-tick countdown" shape, different resource. `ServerSession::punch()`
rejects a call still on cooldown with an empty `PunchResult` (the same
silent-no-op shape "nothing within reach" already had), and **arms the next
cooldown immediately once a call is accepted, before resolving what it hit**
— a whiff costs the same swing time as a landed hit, matching how a real
attack-rate cap works rather than only throttling punches that connect.
Pack-facing via `vb.combat.set_params{punch_cooldown_seconds=...}`
(`PackRuntime::effective_punch_params`, `src/script/pack_runtime.cpp`), the
same table `hit_radius`/`player_damage`/`heal_after_seconds`/
`heal_interval_seconds` already live on — deliberately opt-in with no engine
default (unlike the heal timers, which do ship a real default rate): there's
no obviously-correct default swing cadence the way there is for "how fast a
punched block starts healing." Verified: full `vb_tests` 392/392 green (2
new `blockedit_test.cpp` cases — a configured cooldown rejects an immediate
second `punch()` call but lets a later one land once `server.tick()` has
advanced real time past it, and a disabled-by-default case proving two
back-to-back calls with no cooldown set both land, matching every
pre-existing punch test's own calling style), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). **Still open,
unchanged by this pass:** no swing animation; PvP still has no armor/
knockback — this item's own text bundled 3 things together and only the
cooldown third is closed here.

Before that, most recent landed item was **the connect-screen byte-progress bar, closing REMAINING_TASKS' Phase 5 gap
("No connect-screen byte-progress bar — status-text-only").** Also confirmed
and closed a separate, stale Phase 1 line item in the same session: "Surface
`ENGINE_PROTOCOL_VERSION` mismatch in the client connect UI" turned out to
already be fully wired end-to-end (`ClientHandshake::on_frame` already fails
with a real message string, `src/client/main.cpp`'s `kConnecting` case
already routes it into `AppState::kError`, `MainMenu::draw_error()` already
renders it) — no code changed for that one, just the backlog entry.
For the byte-progress bar itself: `assetsync::ClientAssetCache` gained
`sync_total_bytes()`/`sync_received_bytes()` (`src/assetsync/cache.cpp`),
both computed fresh from the existing `pending_` map (`PendingFile::
expected_size` and `.buffer.size()`) rather than kept as separate running
counters that `ingest_chunk`/`compute_missing` would otherwise have to
remember to update in lockstep. `net::ClientSession::asset_sync_total_bytes()`/
`asset_sync_received_bytes()` (`inc/vb/net/session.hpp`) forward through the
session's own possibly-null `asset_cache_` (0/0 when there's no cache, same
posture `virtual_pack_fs()` already had for singleplayer/no-asset-sync).
`render::MainMenu::draw_connecting()` gained an optional `float fraction =
-1.0f` — `-1` (every non-`kSyncingAssets` handshake stage, and singleplayer,
which has no `ClientAssetCache` at all) keeps the exact original text-only
layout; a real `>= 0` value grows the panel by one row and draws a
`GuiProgressBar`, matching 7.1's `draw_loading()` bar look. Verified: full
`vb_tests` 390/390 green (2 new `assetsync_cache_test.cpp` cases: byte
totals/received tracked correctly across two pending files including a
mid-transfer partial-chunk read, and 0/0 before any `compute_missing()` call
at all), clean `-Werror` build of `vb_tests`/`voxel_browser`/
`voxel_browser_server` (temporarily reconfigured `build-net-lua` with
`-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
dir's OFF default afterward). The actual rendered bar (a human watching it
fill while downloading a real content pack over a real connection) was
**not** manually eyeballed — no GUI in this agent environment, same
still-open caveat as every other rendering-adjacent pass in this file.

Before that, most recent landed item was **render-only step-up smoothing, closing Phase 3's "Step-up jerk" gap.**
`VoxelCollisionSystem`'s step-up resolves a full climb (up to `step_height`,
1.05m) in one physics tick, correctly, but the client used to feed that raw
`feet.y` straight into the camera every frame (`controller.set_position({
feet.x, feet.y + eye_height, feet.z })`, `src/client/main.cpp`) — so a real
step-up visually popped the camera up in one frame instead of climbing
smoothly. New `vb::render::EyeHeightSmoother` (`inc/vb/render/camera.hpp`,
header-only pure math, no raylib dependency — same "unit-tested without a GL
context" posture as `frustum.hpp`/`entity_visual_layout.hpp`) sits between
the two: `update(target_y, dt)` exponentially eases the *rendered* Y toward
`target_y` over a 0.12s time constant, except a jump bigger than
`kSnapThreshold` (2.0m — comfortably above any real step-up, comfortably
below a teleport/respawn distance) is applied immediately with zero
smoothing, so a respawn or a fresh connection never eases in from wherever
the previous life's body happened to be. **Deliberately Y-only:** `X`/`Z`
still come straight from `feet` every frame, unsmoothed — horizontal movement
was never the jerky part, and smoothing it too would just add input lag for
no benefit. `src/client/main.cpp` gives each of the two client loops
(`--headless` and the real windowed one) their own `EyeHeightSmoother`
instance, reset alongside `controller` both at the very first spawn and
inside `enter_playing()` (the reconnect/respawn reset path) — an explicit
reset rather than relying solely on the snap-threshold is what guarantees a
*fresh* life never has any of a *previous* life's in-flight smoothing state
bleeding into it, even in the edge case where the two positions happen to be
close enough to fall under `kSnapThreshold`.
Verified: full `vb_tests` 388/388 green (6 new `render_test.cpp` cases on
`EyeHeightSmoother` — first update snaps to the target, a one-block step-up
eases in over a single frame rather than landing there immediately,
convergence to the target after ~10s of ticks, a >2m jump snaps immediately
rather than easing, a tiny per-frame delta from ordinary walking/falling
tracks almost exactly with negligible lag, and `reset()` mid-ease drops the
in-flight smoothing rather than blending from it), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). The actual smoothed
step-up (a human walking up a single block and watching the camera rise
instead of pop) was **not** manually eyeballed — no GUI in this agent
environment, same still-open caveat as every other rendering-adjacent pass in
this file.

Before that, most recent landed item was **horizontal cross-chunk light propagation, closing Phase 2's own last
remaining item ("Horizontal cross-chunk light propagation... still
per-chunk-only").** `LightEngine::relight_chunk`'s old `const Chunk *above`
parameter is now `const Neighbours &neighbours` (`inc/vb/world/lighting.hpp`)
-- 5 optional fields, `above` (unchanged meaning) plus new `north`/`south`/
`east`/`west`, each "the loaded chunk at the corresponding adjacent
`ChunkCoord`, or null if nothing's loaded there / that border is closed
off." A single-`Chunk*`-argument constructor on `Neighbours` keeps every
pre-existing `above`-only call site, including `relight_column`'s own,
compiling unchanged -- purely additive at the API surface. `relight_chunk`
(`src/world/lighting.cpp`) seeds sky light across each of the 4 new
vertical *faces* from whichever horizontal neighbour is loaded, using the
same "attenuate by 1 step, `if (seeded > sky[i])` relax" shape the interior
BFS already used for a plain sideways step -- not the top face's special
"straight down, no falloff" case, since horizontal propagation across a
border decays by 1 per step exactly like an interior horizontal step
already did (confirmed by the pre-existing "sky light spills under an
overhang and falls off by 1 per step" test, unchanged by this pass).
`relight_column` (unconditional vertical cascade, 2026-09-15) now builds
the *full* `Neighbours` set via `find()` at every level of that cascade, so
any relight -- an edit, an initial chunk load, or the cascade itself --
picks up whatever horizontal neighbours happen to already be loaded at that
moment, the same passive "use what's there" posture `above` always had.
**Real reactive gap closed on top of that, not just the passive form:** the
passive form alone only helps when a chunk happens to relight *after* its
neighbour is already correctly lit -- it does nothing for the actual
live-editing case that motivated this item (break one block near a chunk
border, and the chunk on the other side, already stably lit with no other
reason to ever relight again, never finds out). New `push` parameter on
`relight_column_impl`: when relighting a chunk in the cascade actually
changes its light and a horizontal neighbour is loaded in that direction,
this now also relights that neighbour's *whole column* recursively (via the
same cascade, with `push=false` on the inner call), and reports it through
the same `on_relit` callback -- so `WorldReplicator`'s existing per-edit
delta-building code (`src/net/world_replicator.cpp`'s `apply_block_edit`)
sends the pushed neighbour's own delta to its own watchers immediately, on
the same edit, rather than waiting for the next tick's separate
"still-visible chunk whose revision moved" catch-all sweep. **Deliberately
deferred until the whole triggering column finishes cascading, not fired
per-level as each change is found:** a pushed neighbour's own relight reads
the triggering column's chunks back via `find()`, and firing early would
let it see a half-updated column (levels below the one that just changed
still holding pre-relight data).
**Why one hop is provably enough, not just "good enough for now" like the
existing vertical cascade's own down-through-the-stack cost note:** every
propagation step costs at least 1 of light's 0-15 range, and a chunk is
`kChunkDim` (32) blocks wide -- light that has just crossed one border has
at most 14 of budget left, nowhere near enough to cross a second full-width
chunk and reach a third one. So a pushed neighbour's own relight, even
though it may itself find further changes, never tries to push a second
time (`push=false` on that inner call) -- it doesn't need to, not just
"chooses not to for cost reasons." Diagonal neighbours are still never
touched directly, unchanged from before this pass -- an edit's effect on a
diagonal chunk, if any, only ever arrives indirectly through whichever of
the two shared orthogonal neighbours pushes into it, one hop at a time,
exactly like everything else here.
Verified: full `vb_tests` 382/382 green (2 new `lighting_test.cpp` cases --
a direct `relight_chunk` case proving sideways spill/falloff from a single
`west` neighbour under an otherwise fully-sealed roof, using the same
"stone ceiling with one open column" shape the pre-existing single-chunk
overhang test uses, just pinned to the chunk edge instead of the middle;
and a `relight_column`-based end-to-end case that starts two adjacent
fully-sealed chunks, "breaks a block" in one to open a gap right at its own
border column, calls `relight_column` on *only* that edited chunk's own
coord -- exactly what a real block-edit call site does -- and confirms the
untouched neighbour picks up the new spill automatically, with `on_relit`
firing for both coords), clean `-Werror` build of `vb_tests`/
`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). **Deliberately out
of scope, per the pre-existing vertical asymmetry this pass didn't
reopen:** block light still never crosses a chunk border at all, vertically
or horizontally -- only sky light does, matching exactly what the
vertical-only implementation already covered before this pass; extending
cross-chunk propagation to block light too is a separate, still-open
follow-up, not attempted here. The actual rendered result (a human mining
sideways near a chunk border and watching light spill in correctly instead
of sitting in a dark band until they cross it) was **not** manually
eyeballed -- no GUI in this agent environment, same still-open caveat as
every other rendering-adjacent pass in this file.

Before that, most recent landed item was **a per-connection message-rate flood guard, closing REMAINING_TASKS' long-
tracked "per-player rate limit / flood guard belongs with `GnsTransport`"
item (Phase 3) and its Phase 6.3 duplicate ("rate limiting on custom-keybind
events").** New `ServerSession::set_max_messages_per_second(double)` /
`ServerConfig::max_messages_per_second` (`server.toml`, default `0` =
unlimited, same posture as `max_connections_per_ip`): a token bucket per
*playing* connection, one token spent per post-join C2S message regardless
of type (input batch, block edit, chat, UI event, block-break begin/stop),
refilled at the configured rate in `system_network_io()`'s own per-tick
step (capacity == the rate itself, so up to one second's worth of burst is
tolerated before drops start) — checked right after the existing
`if (it->second.playing) {` branch in the per-message dispatch switch, before
any per-type handler runs. An empty bucket **drops** the message, it does
not disconnect the connection — a transient burst (e.g. a lag spike
replaying a backlog) shouldn't cost a real player their session, only a
sustained flood gets bled off. Deliberately one bucket per connection
covering every message type together, not a separate limiter per type
(chat, keybinds, block edits, ...): the actual resource being protected is
total per-connection dispatch/bandwidth cost, and this closes Phase 6.3's
own tracked "rate limiting on custom-keybind events" gap for free — a
keybind flood is just an `InputCmd`/`C2SInputBatch` flood, already covered.
Seeded to a full bucket at `kConnected` (a fresh connection starts with its
whole burst allowance available, not an empty bucket it has to wait a
second to fill). Exposed read-only via `vb.config.get(
"max_messages_per_second")`, mirroring `max_connections_per_ip`'s existing
Phase 6.13 surface — deliberately not a pack-overridable knob (an operator
policy, not content policy, same split as every other `server.toml` value
`vb.config.get` exposes). Verified: full `vb_tests` 380/380 green over
`LoopbackTransport` (new `netcode_test.cpp` case: 1 msg/sec limit, three
back-to-back `send_chat()` calls before any refill only deliver the first,
then ~1 second of pumped ticks refills the bucket and a fourth message
lands; `config_test.cpp` TOML default/parse cases;
`pack_runtime_test.cpp`'s `vb.config.get` round-trip case), clean `-Werror`
build of `vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed
clean, reconfigured back to this dir's OFF default afterward).
**Gotcha hit running the full suite in this agent environment:** the two
real-UDP `gns_transport_test.cpp` cases (`GnsTransport::remote_address`,
the per-IP-cap test) hung the whole `vb_tests.exe` run indefinitely when run
unfiltered — GNS opening a real UDP listen socket appears to trigger a
Windows Firewall prompt this headless agent session can never answer, not a
bug in this change. Worked around by running with
`--test-case-exclude="*real UDP*"` for verification (380/380 green, 2
skipped) rather than chasing the hang further; a future agent hitting an
apparently-frozen `vb_tests.exe` with no output at all after several minutes
should suspect this before assuming a real deadlock in freshly-changed code.

**`EntityKind` tick/spawn/hit/death callbacks folded into a real `SystemRunner`
phase (Phase 4 gap, closed).** `PackRuntime::dispatch_tick` used to be called
as a separate step in each embedder's own loop (`server/main.cpp`, `client/
main.cpp`'s `Singleplayer::tick()`), strictly *after* `ServerSession::tick()`
had already finished every `SystemRunner` phase for that tick — a
script-driven entity move (`self:set_pos()` from `on_tick`) reached
`sync_interest`/`broadcast_snapshots` one tick late. This was a deliberate,
twice-reaffirmed deferral (2026-09-17's original hardcoded-map precedent,
reaffirmed 2026-09-25 when `SystemRunner` itself landed but `dispatch_tick`
was explicitly kept externally driven) — REMAINING_TASKS' own line describing
it as blocked on "the formal `SystemRunner` itself" was stale by the time this
was picked up, since that landed 2026-09-25. New `ServerSession::
set_script_tick_hook(fn(double))` (`inc/vb/net/session.hpp`) registers a new
`"script_tick"` phase in `build_systems()`, placed right after
`check_respawns` and before `update_item_drops`/`sync_interest`/
`broadcast_snapshots` — `ServerSession` still has zero direct `PackRuntime`
reference, same `std::function` hook-seam decoupling as `set_landed_hook`/
`set_region_hooks`/etc.; `PackRuntime::attach_session()` installs it
unconditionally (unlike the conditional hooks right above it in that
function — `dispatch_tick` does real work, global timers included, even for a
pack that registers no entity-kind callbacks at all). Both embedders'
per-tick loops no longer call `pack_runtime.dispatch_tick()` directly.
**Real behavior change, not just a refactor:** a script entity's `on_tick`
move now shows up in the *same* tick's outgoing snapshot instead of the next
one — a strict latency improvement, matching the spec's original
`ScriptPreTickSystem` placement ahead of interest/replication, but worth
knowing if a future timing-sensitive test seems to be off by one tick from
what an older mental model would predict.
**Gotcha hit wiring this up:** roughly 19 existing test call sites (across
`pack_runtime_integration_test.cpp`, `content_pack_test.cpp`,
`kitchen_sink_pack_test.cpp`) call `rt.attach_session(server)` and then pump
`server.tick(0.05); client.tick(0.05); rt.dispatch_tick(0.05);` in a loop —
each of those explicit `rt.dispatch_tick()` calls became a redundant *second*
dispatch per pump step once `attach_session()` started installing the hook
(entity ticks/timers would fire twice per loop iteration). Fixed by deleting
the now-redundant explicit call from every one of those pump loops (`pack_
runtime_test.cpp`'s own direct `rt.dispatch_tick()` calls are untouched —
those tests never attach a session, so no hook is ever installed there).
Verified: full `vb_tests` 381/381 green, clean `-Werror` build of `vb_tests`/
`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward).

Before that, most recent landed item was **sandboxed `require` + per-callback wall-clock budget (Phase 4.1, closed).**
`vb::script::Vm` reinstates a safe `require` global that resolves only
against an in-memory virtual module map (`Vm::install_require`, built by
`load_content_pack`'s own directory walk — every pack `.lua` file except
`ui/*.lua`), with `package.loaded`-style caching and cycle detection; the
existing instruction-count hook now also samples a `std::chrono::
steady_clock` deadline every 1000 instructions (`VmLimits::
wall_clock_budget_ms`, default 250) so a callback with few but slow
instructions still gets cut off, not just one that runs too many VM ops.
Both failure modes still classify to the existing `ScriptError::
kBudgetExceeded` — no new enum value. Full writeup + gotchas (the
`AllocState`/`lua_getallocf` trick for reaching hook state, why the virtual
FS isn't `ClientAssetCache`'s synced map) is in `REMAINING_TASKS.md`'s Phase
4 section, not repeated here.

**Fall damage (REMAINING_TASKS' Phase 6 "no fall damage" gap, closed as
6.22).** New `net::ServerSession::set_landed_hook(fn(NetId, double
impact_speed))` — fires once per player exactly on the tick a fall is
arrested by hitting ground, never per-tick while airborne or already
grounded. Wired inside `handle_input_batch`'s existing per-cmd loop
(`src/net/session.cpp`): captures `was_on_ground = move.on_ground` and
`fall_speed_before = -move.velocity.y` *before* each `physics::step_movement`
call, fires the hook right after if `!was_on_ground && move.on_ground &&
fall_speed_before > 0.0`. **Deliberately reads the pre-step velocity, not a
new post-collision value threaded out of `MoveState`:** `step_movement`
already zeroes `velocity.y` internally the instant a downward sweep hits
something solid (`src/physics/movement.cpp`'s `grounded = true; out.velocity.y
= 0.0;`), so by the time it returns there's nothing left to read the real
impact speed from without widening `MoveState`'s public struct (and every
existing physics test's understanding of it) just for this one value. Using
the *incoming* velocity is off by at most one tick's worth of gravity
(`params.gravity * dt`, a few tenths of a m/s at a typical 20-50ms tick) —
judged an acceptable approximation for a damage formula, not worth the extra
surface area. `PackRuntime` wires this the same "opt-in, zero cost when
unused" way every other hook here does: a new `vb.on("player_landed",
function(player, impact_speed) ... end)` event, only installed
(`ServerSession::set_landed_hook`) when a pack actually registers one.
Pure notification, no veto/return value, same posture as `region_enter`/
`region_exit` — the engine computes and reports the raw speed only, zero
built-in fall-damage formula or threshold. `content/base/fall_damage.lua`
(new) is the reference policy: no damage below a flat 8 m/s safe-speed
threshold, then 1 HP per m/s above it, via the existing
`player:damage(amount, "fall")` primitive (6.6) — the first real content
caller of that primitive (previously only the decision hook existed, nothing
triggered it). No protocol change: this is a server-local hook, nothing
about a landing is ever replicated to any client. Verified: full `vb_tests`
371/371 green (1 new `pack_runtime_integration_test.cpp` case — spawns a real
player 5 blocks above real generated terrain via a `LoopbackTransport`,
drives real `InputCmd`s with `dt=0.05` and no jump/move through real
`pump()` ticks, confirms the hook fires exactly once with a plausible impact
speed and never fires again across 10 more ticks resting on the ground),
clean `-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
confirmed clean, reconfigured back to this dir's OFF default afterward).
**Gotcha hit while writing the integration test, worth knowing before writing
another physics-driven one:** `protocol::InputCmd::dt` defaults to `0.0f`,
and `physics::step_movement` early-returns unchanged (`if (dt <= 0.0) return
out;`) on a zero `dt` — a test cmd built as `InputCmd falling;` with every
other field left at its default silently never moves the player at all (no
crash, no assertion failure until the final `REQUIRE` about ticks later,
which made this look like a hook-wiring bug at first rather than a test
setup bug). Every existing input-driven integration test before this one
happened to not care because none of them depended on real gravity/movement
actually advancing the position — they used the input hook path
(`player_input`) to react to buttons regardless of whether the physics step
did anything. Fixed by explicitly setting `falling.dt = 0.05f` to match
`pump()`'s own per-tick `0.05` argument.

Before that, most recent landed item was **chunk frustum culling + a transparent second pass** (REMAINING_TASKS'
long-standing Phase 2 remaining item: "Frustum culling, transparent second
pass, texture atlas — Phase 4" — the texture atlas itself landed separately
2026-09-23). New `inc/vb/render/frustum.hpp`, header-only and raylib-free
(same "pure math, unit-tested without a GL context" posture as
`entity_visual_layout.hpp`): `build_frustum()` builds the 6 view-frustum
planes directly from camera basis vectors (position/forward/up/fovy/aspect/
near/far) via the "cross product of the far-plane corner vectors"
construction, deliberately *not* by extracting planes from a combined
view-projection matrix -- that would tie this pure header to raylib/rlgl's
internal row/column matrix convention (verified against the OpenGL-style
right=cross(forward,up) basis this codebase's own
`core::forward_from_yaw_pitch` already assumes). `aabb_in_frustum()` is the
standard conservative "positive vertex" AABB-vs-plane test -- never wrongly
culls something actually inside, may keep a few boxes just outside a corner.
`ChunkRenderer::draw()` (`src/render/chunk_renderer.cpp`) signature changed to
take the `Camera3D` being rendered with (one call site,
`src/client/main.cpp`); near/far are hardcoded to 0.01/1000.0 to match
`BeginMode3D`'s own un-overridden `RL_CULL_DISTANCE_NEAR/FAR` defaults
(nothing in this codebase calls `rlSetClipPlanes`) -- the frustum this builds
must agree with what raylib actually rasterizes, or culling would disagree
with the real clip planes. A chunk whose 32-block AABB is provably entirely
outside the frustum gets no `DrawModel` call at all, not just an early
depth-reject.
Transparent second pass, same pass: `ChunkRenderer` now uploads **two** GPU
models per chunk -- `split_transparent()` (new) partitions a chunk's meshed
quads by the same flat fallback-color alpha `fill_mesh_arrays` already used
for vertex-color alpha (today: only `base:leaves`, `a=220`), reading it once
per quad rather than per vertex since a quad's 4 vertices already share one
`block_id` and are contiguous by construction
(`chunk_mesh_snapshot.cpp`'s own per-face `first` numbering) -- not a
per-texel alpha check, and not a new `BlockType` field (a real alpha-cutout
textured block would need its own opt-in flag, not attempted here). `draw()`
renders every visible chunk's opaque model first (any order -- depth buffer
sorts it out), then every chunk with transparent geometry a second time with
`rlDisableDepthMask()` set and sorted back-to-front by chunk-center distance
from the camera, flushing the render batch (`rlDrawRenderBatchActive()`)
around the depth-mask toggle so it doesn't retroactively apply to
already-batched opaque draws -- chunk granularity only, not per-triangle
(matches this engine's block scale). `GpuChunk` went from one `Model`+capacity
pair to two named `GpuMesh` slots (`opaque`/`transparent`); `upload_part()`
(new) is the old single-mesh reuse-if-it-fits/recreate-if-it-doesn't logic,
now run once per slot instead of once per chunk.
Verified: full `vb_tests` 370/370 green (7 new `frustum_test.cpp` cases --
ahead/behind/beside/beyond-far/nearer-than-near/straddling-the-boundary AABB
cases, plus one proving a non-normalized, non-orthogonal `up` vector still
works since both get re-derived internally), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). The actual rendered
result (a human walking around and watching off-screen chunks stop being
drawn, leaves/water blending correctly over terrain behind them) was **not**
manually eyeballed -- no GUI in this agent environment, same still-open
caveat as every other rendering-adjacent pass in this file.

Before that, most recent landed item was **real crack-stage texture art + `crack_texture` override, closing Phase
6.5 in full** (REMAINING_TASKS' last open piece of that item). New
`vb::world::BlockType::crack_texture` (pack-relative crack-stage spritesheet
path, empty = engine default) + `BlockRegistry::set_crack_texture()`
mirroring `set_texture()`'s "attach without disturbing already-frozen
fields" posture; `vb.register_block{crack_texture=...}` wires it through.
Protocol bumped **25 -> 26**: `BlockRegistryRecord` gains `string
crack_texture`. New `vb::render::CrackAtlas` (pure `build()`/GPU `upload()`
split, like `TextureAtlas`): one shared row of `kStages` (8) cells is the
engine's own procedurally-generated default crack pattern (deterministic
per-stage line drawing — no art tools in this environment); a block with a
valid `crack_texture` (a real `kStages`-frame spritesheet) gets its own row
instead, falling back to the shared default row on any decode/shape failure.
New `vb::render::CrackOverlay` (GPU-only) replaces the old flat translucent
`DrawCube` in `src/client/main.cpp`'s `kPlaying` draw block: a persistent
unit cube mesh whose texcoords are remapped into the resolved `CrackAtlas`
rect (re-uploaded to the GPU only when the rect actually changes) and drawn
with the atlas bound, alpha still climbing with `break_progress`.
**Real pre-existing bug found and fixed in the same pass, unrelated to
`crack_texture` itself:** all three sites that build/apply a
`BlockRegistryRecord` (`src/server/main.cpp`'s and `src/client/main.cpp`'s
`host.block_registry` callbacks, and `ClientSession::apply_block_registry()`
in `src/net/session.cpp`) used an aggregate-init listing only the record's
first 5-6 fields, silently dropping `max_damage` (and now `crack_texture`)
on every hop — so no client had ever actually received a nonzero
`max_damage` for any block, meaning `client.break_progress()` (the previous
entry below) was permanently `nullopt` in practice regardless of a block's
real `max_damage`, since the very first commit that introduced these
callbacks. Caught by writing a regression test first (`block_registry_test.
cpp`) and confirming it failed against the pre-fix code before fixing it.
Verified: full `vb_tests` 363/363 green (8 new cases across
`block_registry_test.cpp`, `pack_runtime_test.cpp`, `protocol_test.cpp`, and
a new `crack_atlas_test.cpp` covering `CrackAtlas::build`'s default-row/
override-row/wrong-shape-fallback/missing-from-vfs-fallback/stage-clamping
behavior), clean `-Werror` build of `vb_tests`/`voxel_browser`/
`voxel_browser_server` (temporarily reconfigured `build-net-lua` with
`-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
dir's OFF default afterward). The actual rendered crack-stage overlay (a
human punching a block and watching a real textured crack pattern progress
on it) was **not** manually eyeballed — no GUI in this agent environment,
same still-open caveat as every other rendering-adjacent pass in this file.

Before that, most recent landed item was **replicating live block-damage (punch count) to nearby players, closing
Phase 6.5's deferred half** (REMAINING_TASKS' "Replicate block-damage value
to nearby players" gap). Targets the mechanism that's actually live today --
6.18's punch-based `ServerSession::block_punch_counts_` -- not the original
6.5-era `world::BlockDamageSystem`/`C2S_BlockBreakBegin`/`Stop` path, which no
client has ever driven (`content/base/mechanics.lua` calls `player:punch()`,
not `send_block_break_begin`) and stays exactly as dead as it was before this
pass. New `S2C_BlockDamage` (`inc/vb/protocol/world.hpp`, message id 54,
`ENGINE_PROTOCOL_VERSION` 24 -> 25): `IVec3 pos`, `u16 punches`, deliberately
no `max_damage` field -- the receiving client already knows
`BlockType::max_damage` from its own chunk mirror + block registry, so this
stays a pure delta. `ServerSession::broadcast_block_damage(pos, punches)`
(`src/net/session.cpp`) sends it to every playing connection currently
mirroring the chunk (`WorldReplicator::player_has_chunk`) -- not just
whoever's punching -- called from three sites: `punch()`'s punch-count
increment, `punch()`'s break-completion (sends `punches = 0`), and
`update_block_punch_healing()`'s per-tick heal loop (sends the new value, or
`0` on full heal). `punches == 0` means "cleared" both ways (broken or fully
healed) -- `ClientSession::block_damage()` (`inc/vb/net/session.hpp`) erases
the entry rather than ever storing a stale `0`. **Gotcha carried over from
this same client-side map:** a chunk leaving a client's own view
(`S2C_ChunkRemove`) means the server silently stops broadcasting further
updates for it (nothing left mirroring it to broadcast to) -- without
cleanup, a damaged block's last-known punch count would linger forever and
resurface wrong if the player wandered back. Fixed by sweeping
`block_damage_` for any entry in the removed chunk right inside the
`kS2CChunkRemove` case. `client.break_progress()` (Phase 6.16's HUD
primitive, previously hardcoded to always `nullopt` with a comment awaiting
exactly this) now computes a real fraction (`punches / max_damage`) for
whichever block the local player is currently looking at
(`src/client/main.cpp`). A default generic crack overlay ships alongside it:
a translucent black cube drawn over the targeted block, darkening in step
with that fraction (`DrawCube` at `alpha = fraction * 180`, scaled 1.004x to
avoid z-fighting against the block's own mesh) -- real crack-stage texture
art and a pack-facing `crack_texture` override are unblocked by this but
**not** attempted this pass (real asset-pipeline work, deliberately scoped
out to keep this change reviewable; see `remaining_tasks/phase6.md` 6.5).
Verified: full `vb_tests` 355/355 green (a `protocol_test.cpp` round-trip
case for `S2CBlockDamage`; a new `blockedit_test.cpp` case proving a
non-punching second player watching the same chunk sees another player's
live punch count update and clear on break; the existing self-heal test case
now also asserts the replicated value clears on a full heal, not just the
server-side count), clean `-Werror` build of `vb_tests`/`voxel_browser`/
`voxel_browser_server` (temporarily reconfigured `build-net-lua` with
`-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
dir's OFF default afterward). The actual rendered crack overlay (a human
punching a block and watching it darken) was **not** manually eyeballed --
no GUI in this agent environment, same still-open caveat as every other
recent rendering-adjacent pass.

**Wired HUD widgets to `report_click`/`report_change`/`report_list_change`**
(REMAINING_TASKS' Phase 6.16 gap: "HUD widgets aren't wired to
report_click/report_change (display-only for now)"). `vb::render::UiRenderer::
draw()` already returns a `UiFrameResult` (clicked/changed_text/changed_list)
generically for *any* widget list passed to it — modal screens and the HUD
alike — but `src/client/main.cpp`'s `hud_renderer.draw("hud", ...)` call
discarded that result outright, so a HUD `button`/`textbox`/`list` widget's
`on_click`/`on_change` Lua callback could never actually fire. Fixed by
capturing it and routing it through three new `UiRuntime` methods --
`report_hud_click`/`report_hud_change`/`report_hud_list_change` -- mirroring
the existing modal-screen `report_click`/`report_change`/`report_list_change`
exactly, right next to that call in `src/client/main.cpp`.
**Why not just reuse the existing `report_click` etc.:** those look a widget
id up in `UiRuntime::Impl::widget_by_id`, a map rebuilt every `render_frame()`
call for whichever modal screen is currently open (or empty if none is) --
a HUD's own widgets are never in it, since `evaluate_hud_frame()` populates a
separate `hud_widgets_vec` with no id map at all. Added the missing
`hud_widget_by_id` map (built in `evaluate_hud_frame()`, same shape as
`widget_by_id`) and generalized `Impl::run_callback()` to take *which* map to
look the id up in, plus a `bool is_hud` used only to pick the right
`C2S_UiEvent.ui_name` if the callback calls `ui.send_event(...)` -- a HUD
widget has no modal screen name to attach the event to, so it sends
`ui_name = "hud"` instead of `current_name` (which is empty when no modal
screen happens to be open, but must never leak a *stale* modal name if one
happens to be open at the same time a HUD widget is clicked -- the two are
otherwise independent by design, per the existing "modal screen opening/
closing alongside it doesn't reset hud state" test). No protocol change --
`C2S_UiEvent.ui_name` is already just a free-form string the server hands
back to `vb.on("ui_event", ...)`, so `"hud"` needs no reservation or special
casing server-side. Verified: full `vb_tests` 353/353 green (3 new
`ui_runtime_test.cpp` cases -- `report_hud_click`/`report_hud_change`/
`report_hud_list_change` firing the right callback, plus a same-id-different-map
case proving `report_click("hud_btn")` does *not* reach a HUD widget's
callback; 1 new `pack_runtime_integration_test.cpp` end-to-end case proving a
real HUD widget's `ui.send_event` reaches a real server's `vb.on("ui_event",
...)` with `ui_name == "hud"`), clean `-Werror` build of `vb_tests`/
`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). **No new interactive
HUD widget was added to `content/base/ui/hud.lua`** -- the player list/chat
log/hotbar are all still non-interactive `rect`/`text` widgets (previous
entry below); this pass only closes the *engine-side* wiring gap so a future
pack (or `content/examples/kitchen_sink`) *can* add one. The actual
click-fires-callback behavior in a real window was **not** manually
eyeballed -- no GUI in this agent environment, same still-open caveat as
every other rendering-adjacent pass in this file.

Before that, most recent landed item was **migrating the player list / chat log / hotbar HUD off hardcoded C++ into
`ui.define_hud`** (REMAINING_TASKS' Phase 6.16 gap). New `WidgetType::kText`
(`inc/vb/script/ui_runtime.hpp`) is a raw, colored, alignable text-draw
primitive — `kLabel` goes through raygui's `GuiLabel`, which has no color
parameter at all, so it couldn't reproduce the old green own-name / gray
others-name distinction. Alignment (`left`/`center`/`right`, a new field on
`Widget`) exists specifically because `vb::script::UiRuntime` has no raylib
dependency and can't call `MeasureText` itself to right-align text (the old
player list was right-aligned against the screen edge) — that measurement
happens in `vb::render::UiRenderer::draw()` (`src/render/ui_renderer.cpp`,
already raylib-linked) instead; `align` just tells it which anchor `x` means.
Six new read-only `client.*` accessors (`player_name`, `players`, `chat_log`,
`chat_open`, `inventory`, `selected_slot`) mirror the existing
`client.break_progress()`/`client.screen_size()` shape, fed by three new
`UiRuntime` setters (`set_player_list`/`set_chat`/`set_inventory`) called
once per frame from `src/client/main.cpp` right next to the pre-existing
`set_break_progress`/`set_screen_size` calls. `content/base/ui/hud.lua` grew
three widget-building functions (`push_player_list`/`push_chat_log`/
`push_hotbar`) that reproduce the old hardcoded layout pixel-for-pixel (same
colors, same positions, same selected-slot outline logic) using `rect`/
`text` widgets instead of direct `DrawText`/`DrawRectangle` calls.
**Deliberately left alone:** the chat **input box** itself (typing,
Enter-to-send) — that's real keyboard text-entry capture via a plain
`GuiTextBox`, same posture as `MainMenu`, and was already explicitly scoped
out of `UiRuntime` before this pass (its own comment said so); this pass
didn't touch that scope decision. No new interactive HUD widgets were added
either, so REMAINING_TASKS' separate "`report_click`/`report_change` not
wired to HUD widgets" item is still open — nothing here needed it. Verified:
full `vb_tests` 350/350 green (no test loads `content/base/ui/hud.lua`
directly — it's client-runtime-only, never exercised by a unit test, same as
every other Lua UI file in this codebase), clean build of `vb_tests`/
`voxel_browser`/`voxel_browser_server` on `build-net-lua`. The actual
rendered HUD (a human confirming the player list/chat/hotbar still look
exactly as before) was **not** manually eyeballed — no GUI in this agent
environment, same still-open caveat as every other rendering-adjacent pass
in this file.

Before that, most recent landed item was **Phase 7.5's override half: `vb.render.set_fog{underwater_tint=}`,
closing REMAINING_TASKS' last open Phase 7 item (7.1-7.6 are now all done).**
7.5's *default* (submerged liquid's own texture-average color) landed
2026-09-23 alongside the real texture/atlas system; the override half was
deliberately left open pending a protocol bump. Landed now: an optional
`underwater_tint = {r=, g=, b=}` (each `0-255`) sibling field on the existing
`vb.render.set_fog{start=, ["end"]=}` call (`src/script/pack_runtime.cpp`) —
all three channels are required if the table is given at all, and an
out-of-range or partial one rejects the whole `set_fog` call, same posture as
a bad `start`/`end`. Protocol bumped **23 -> 24**: `S2CFogParams`
(`inc/vb/protocol/world.hpp`, type 52) gains `bool has_underwater_tint`
followed by, only if true, `u8 underwater_tint_r/g/b` —
`has_underwater_tint = false` means "no pack override, client keeps its own
texture-average/placeholder default," not "black," the same "absence isn't a
value" shape `fog_start`/`fog_end` already had relative to each client's own
`view_distance` default. `src/client/main.cpp`'s underwater branch (the same
`kPlaying`-state block that already computed the camera's own eye voxel) now
checks `client->fog_override()->has_underwater_tint` first and only falls
back to `ChunkRenderer::underwater_tint()`'s texture-average default when no
pack override is present — the fallback path itself (landed 2026-09-23) is
untouched. Above-water fog color remains permanently sky-only (2026-09-19's
decision), unaffected by this — 7.5 was always scoped as an underwater-only
exception, not a reopening of that rule. Verified: full `vb_tests` 350/350
green (a new `protocol_test.cpp` round-trip case with the tint set, plus
`pack_runtime_test.cpp` cases for the override applying, staying unset when
omitted, and being rejected on a missing channel or an out-of-range value),
clean `-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with
`-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
dir's OFF default afterward). The actual rendered tint swap (a human swimming
with a pack-set override active) was **not** manually eyeballed — no GUI in
this agent environment, same still-open caveat as every other recent
rendering-adjacent pass.

Before that, most recent landed item was **user-requested: distinct per-facing debug art for `base:player`, plus a
real bug fix in pose selection.** User feedback on the real base-pack player
sprite (STATE.md's "Same-day follow-up #7" below): "not very obvious [...]
if I'm looking at the front or back, or left or right side" — the front/
side/back silhouettes were too visually similar to debug the facing system
by eye. First pass baked a big letter (F/R/B) into each of the 3 authored
rows; the user then asked "so we don't have left side?", surfacing that
`select_pose()`'s mirroring (one authored "side" pose flipped horizontally
for the opposite side, `facings=4` -> 3 authored rows) has no way to bake a
distinct "L" — it's the same pixels, just flipped at draw time. Asked
whether to (a) keep the mirrored "R" as good-enough, (b) bump to facings=8,
or (c) add real engine support for a distinct, unmirrored left pose; user
picked (c). Landed as a new `mirror` field (default `true`, so every
existing pack is unaffected): `protocol::EntityVisualDef::mirror` (kind
default, `S2C_EntityKindRegistry`) and `protocol::EntityVisualOverride::
mirror` (per-instance, `EntityRecord::visual_override`) —
`kEngineProtocolVersion` bumped **22 -> 23**. `render::build_entity_visual_
layout()`'s row count becomes `mirror ? facings/2+1 : facings`; `render::
select_pose(bucket, facings, mirror=true)` gained the `mirror` parameter and
returns `{bucket, false}` unconditionally when `mirror=false` (no flipping,
ever); `render::EntityPresentationState`'s constructor gained a `mirror`
parameter threaded through to its own `select_pose()` call. `vb.register_
entity{visual = {mirror = false}}` / `vb.world.spawn(kind, pos, {visual_
override = {mirror = false}}})` are the new Lua knobs (`PackRuntime`'s
`parse_entity_visual`/`parse_entity_visual_override`).
**Real bug found and fixed while wiring this up, unrelated to the `mirror`
feature itself:** `render::EntityRenderer`'s per-tracked-entity
`EntityPresentationState` was **permanently constructed with a hardcoded
`kDefaultFacings = 8`** (`impl_->states.try_emplace(id, kDefaultFacings)`)
and never updated once the entity's real kind (or instance override)
resolved a *different* facings count -- so `base:player` (facings=4) was
having its pose picked with 8-sector bucket math the whole time, silently
indexing rows that could fall outside its own 3-row (now up-to-4-row)
spritesheet. Nobody had caught this because nobody had ever manually
eyeballed the rendered result (see every prior entity-visual entry's own
"not manually eyeballed, no GUI in this agent environment" caveat) --
this user-driven debug-art request is what finally surfaced it. Fixed by
having `EntityRenderer::sync()` re-resolve the real facings/mirror every
frame (cheap: two map lookups) from whichever `KindVisual` `draw()` will
actually use for that id -- instance override wins over the kind default,
matching `draw()`'s own priority -- and only rebuilding `TrackedEntity::
state` (which would otherwise reset `clip_time`/the direction-bucket
tracker every single frame) when the resolved value actually changes.
`content/base/entities/player.lua` now sets `mirror = false`, and
`content/base/textures/player.png` was regenerated (768x512, 4 real rows:
front/right/back/left) with a big letter per row (F/R/B/L) baked in via a
throwaway stdlib-only PNG writer (same pattern as "Same-day follow-up #7"
below) -- deliberately placeholder/debug-styled, not real character art,
since the point of this pass is making the facing system's correctness
obvious to a human, not shipping final art. `content/base/textures/
dropped_item.png` was intentionally left untouched (not a facing-confusion
case the user raised). Verified: full `vb_tests` 346/346 green (a
`build_entity_visual_layout` mirror=false row-count case, `select_pose`/
`EntityPresentationState` mirror=false no-flip cases, a `merge_visual_
override` mirror-only-override case), clean `-Werror` build of `vb_tests`/
`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward). The actual rendered
left/right distinction (a human walking around a real player billboard and
watching the F/R/B/L letters change) was **not** manually eyeballed -- no
GUI in this agent environment, same still-open caveat as every other recent
rendering-adjacent pass; this pass's own bug fix is a strong argument for
getting a real human eyeballing pass scheduled, not just more unit tests.

Before that, most recent landed item was **Same-day follow-up #9: held item / hotbar selection** (entity-management
follow-up, closes REMAINING_TASKS' 6.20 gap: "Placed block is still a
hardcoded `base_stone_id` ... no 'held item'/hotbar-selection primitive
exists"). Protocol bumped **21 -> 22**: `InputCmd` (`C2S_InputBatch`) gains a
`u8 selected_slot` (0-based), reported every cmd like `buttons`/`keybinds`.
Client (`src/client/main.cpp`) reads number keys 1-9 into a persistent local,
gated on `mouse_captured` (so typing a digit into chat doesn't reselect),
and outlines the selected hotbar slot. Server mirrors the latest value into
`ecs::PlayerInput::selected_slot` (`ServerSession::handle_input_batch`),
readable via `ServerSession::selected_slot(NetId)`; a pack's
`vb.on("player_input", ...)` chain can override it too (input table's
`selected_slot`, 1-based -- `PackRuntime::Impl`'s new
`selected_slot_from_table`), same veto/replace shape as
move/yaw/pitch/buttons/keybinds (`net::ServerSession::PlayerInputOverride`
gains the field). New `PlayerHandle` methods `get_selected_slot()` (1-based,
matches `get_inventory()`'s own array) and `get_held_item()` (resolves that
slot against the real inventory, `nil` if out of range/empty). `content/
base/mechanics.lua`'s right-click placing now reads `get_held_item()`
instead of a hardcoded stone id, and spends one unit via `player:take()` on
a successful placement -- closes the loop with break -> drop -> pickup ->
inventory that already existed, instead of an infinite dispenser.
**Gotcha:** adding these two methods to `PlayerHandle`'s sol2 `new_usertype`
(already 20+ methods) pushed `pack_runtime.cpp` over MSVC's object-file
section-count ceiling (`fatal error C1128: ... compile with /bigobj`) --
fixed with a per-source `/bigobj` in `src/script/CMakeLists.txt`. Getting
*that* to actually apply was its own gotcha: `set_source_files_properties()`
is scoped to the directory it's *called from*, not the directory the
consuming target is defined in -- `vb_core` is defined in
`src/core/CMakeLists.txt`, so setting the property from
`src/script/CMakeLists.txt` (the file's own directory, where it's
`target_sources()`'d into `vb_core`) silently never reached the actual
compile rule until adding CMake 3.18's `TARGET_DIRECTORY vb_core` argument
to the call. Verified: full `vb_tests` 342/342 green (a `netcode_test.cpp`
round-trip case for `InputCmd::selected_slot`, a `pack_runtime_test.cpp`
case for `get_held_item()`/`get_selected_slot()`'s no-session default, a
`pack_runtime_integration_test.cpp` case proving a real client's
`InputCmd::selected_slot` reaches both accessors end-to-end), clean build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` on `build-net-lua`. The
actual hotbar-highlight rendering (a human pressing 1-9 and watching the
outline move) was **not** manually eyeballed -- no GUI in this agent
environment, same still-open caveat as every other recent rendering-adjacent
pass.

**Same-day follow-up #8: per-instance `ScriptState.visual_override` (skins)**
(REMAINING_TASKS' Phase 4 item left open by follow-up #6/#7 above, spec
`architecture_spec/rendering.md` §11.3's "Per-instance override"). New third,
optional argument to `vb.world.spawn(kind, pos, opts)`: `opts.visual_override`
takes the same shape as `register_entity`'s `visual` table
(`variant`/`texture`/`facings`/`origin`/`clips`) but every field is
independently optional -- an omitted one inherits the kind's own `visual`
unchanged (`render::merge_visual_override`, new pure function in
`inc/vb/render/entity_visual_layout.hpp`), so a pack overriding just `texture`
(a player-skin variant) keeps the kind's `facings`/`clips`/`origin`.
Validated the same way as `register_entity`'s `visual` whenever a field is
given (`PackRuntime`'s new `parse_entity_visual_override`).
Protocol bumped **20 -> 21**: `EntityRecord` (within `S2C_EntitySnapshot`)
gains an optional `visual_override` (`protocol::EntityVisualOverride`, every
field optional, own presence-bool-per-field codec in `src/protocol/
snapshot.cpp` -- deliberately not sharing `world.cpp`'s `write_entity_visual`/
`read_entity_visual`, which encode a fully-specified `EntityVisualDef` for a
different message, not a partial override; same "no shared helper to import"
posture as `RegionStore`'s own `read_whole_file()`, §4 below). Populated only
on the **one** `entered` record a client receives when a NetId first enters
their interest set (`net::ServerSession::to_record`/`broadcast_snapshots`,
backed by a new `ServerSession::set_script_entity_visual_override()`/
`script_entity_visual_overrides_` map, set from `vb.world.spawn`) --
`updated`/`local` records never carry it (their `has_override` bit is always
false, meaning "unchanged", not "cleared"), and `ClientSession` caches
whatever it first learned (new `entity_visual_overrides_` map, exposed via
`entity_visual_override(NetId)`) for that NetId's whole replicated lifetime --
same "learned once, immutable" posture this codebase already gives
`EntityRecord.kind` itself, chosen deliberately to avoid resending a
potentially large override (a texture path + a full clip list) every tick to
every observer the way a naive "just add it next to `kind`" approach would
have. Client-side, `EntityRenderer::sync()` lazily decodes an override's
texture (merged over the kind's own `EntityVisualDef`, or an all-default one
if the kind never set `visual` at all) the first time it sees a given NetId's
override -- at most once per id ever (an `instance_visual_attempted` set
guards this, since an override never changes), cached in a new
`instance_visuals` map that `draw()` checks before `kind_visuals`. The
kind-visual and instance-visual decode paths were unified into one shared
`decode_kind_visual()` helper (previously duplicated logic inline in
`set_kind_visual`). New `EntityRenderer::set_virtual_fs()` keeps a persistent
copy of the synced/on-disk pack filesystem for this lazy decode -- unlike
every kind's own `visual` (fixed at registration, before any client joins, so
`set_kind_visual` only ever needs to run once per kind in one join-time loop),
an override's owning entity can spawn at any later moment during the session,
so the renderer needs standing access to the vfs rather than a one-shot
parameter. **Deliberately out of scope, left as a real follow-up:**
`entity:set_visual_override()`/any live-update or clear path -- the override
is fixed at spawn time only; there is no wire mechanism to change or clear it
for a client that has already seen the entity (would need resending on an
already-`stayed` record, which the "learned once" wire design above
deliberately doesn't support yet). The reserved `self.visual_override` Lua
table (the spec's own key) is kept in sync purely for pack introspection --
nothing engine-side ever reads it back; the parsed, validated, replicated
copy already lives server-side in `script_entity_visual_overrides_`.
Verified: full `vb_tests` 340/340 green (1 new `protocol_test.cpp` round-trip
case, including proof that an `updated` record never carries an override even
when the entity has one; 3 new `entity_visual_layout_test.cpp` cases for
`merge_visual_override`'s empty/texture-only/full-replace behavior; 2 new
`pack_runtime_integration_test.cpp` cases -- one spawning a real kind with its
own `visual` and a texture-only override, proving a real client's
`entity_visual_override()` comes back with only `texture` set and every other
field still `nullopt`; one proving a malformed override (`facings = 5`)
rejects the whole `vb.world.spawn` call, not just the override), clean
`-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
confirmed clean, reconfigured back to this dir's OFF default afterward). The
actual rendered skin swap (a human watching two instances of the same kind
render with visibly different textures) was **not** manually eyeballed -- no
GUI in this agent environment, same still-open caveat as every other recent
rendering-adjacent pass.

**Same-day follow-up #7: real base-pack art for `base:player`/
`base:dropped_item`** (the follow-up #6 entry below deliberately left open).
Neither ever flowed through the `vb.register_entity` kind mechanism at all:
players replicate with `EntityRecord::kind` hardcoded to `kInvalid` at join
(`src/net/session.cpp`), and dropped items carry a fixed reserved sentinel,
`world::kItemDropKind = 0xFFFF` (`inc/vb/world/item_drops.hpp`), deliberately
outside the dense id space `S2C_EntityKindRegistry` indexes by (`kinds[i] ==
EntityKindId - 1`) -- so `EntityRenderer` could never find a `KindVisual` for
either, regardless of what a pack registered. Fixed with a new, generic
`vb.register_entity{represents = "player" | "item_drop"}` field (validated at
registration: unknown value throws, a role claimed by two kinds throws) --
deliberately not a hardcoded `"base:player"`/`"base:dropped_item"` string
check anywhere in engine code (this repo's "generic primitives, not
game-specific ones" convention). `PackRuntime::Impl` tracks at most one
`player_kind_id`/`item_drop_kind_id`; `PackRuntime::attach_session()` (the
single call site both `src/server/main.cpp` and `src/client/main.cpp`'s
`--singleplayer` path already go through) forwards them to two new
`ServerSession` setters, `set_player_visual_kind()`/
`set_item_drop_visual_kind()`. The join handler and `spawn_item_drop()` now
read `player_visual_kind_.value_or(EntityKindId::kInvalid)` /
`item_drop_visual_kind_.value_or(world::kItemDropKind)` instead of the old
hardcoded values -- a pack that never sets `represents` sees zero behavior
change, exact "default + override" posture as everywhere else in this
codebase. `content/base/entities/player.lua` (new) and `dropped_item.lua`
(edited) register real `visual = {...}` art: `content/base/textures/
player.png` (768x384, 3 facings-rows x 6 idle+walk-columns) and
`dropped_item.png` (512x384, 3 rows x 4 idle-bob-columns), both real,
hand-pixeled-then-upscaled PNGs generated with a throwaway stdlib-only
(`zlib`+`struct`) Python PNG writer (no art tools available in this
environment, no PIL/raylib dependency needed) -- not the synthetic
test-time-only images follow-up #6 used, these are checked into
`content/base/textures/` like `stone.png`/`water.png`. **Gotcha hit writing
the integration tests:** a `ServerSession` built without first calling
`pack_runtime.install_entity_kind_registry(host)` on the `HandshakeServerHost`
passed to its constructor never sends `S2C_EntityKindRegistry` at all, so
`ClientSession::entity_kind()` stays permanently empty regardless of what
`represents=` resolved to server-side -- both new tests initially built
`ServerSession server(net.server(), cfg)` (no host) and failed with
`entity_kind()` returning `nullptr`; fixed by building a real
`HandshakeServerHost`, calling `install_entity_kind_registry(host)` *before*
constructing `ServerSession(net.server(), cfg, host)` (`ServerSession` copies
`host` in its constructor), matching the exact ordering `src/server/main.cpp`
and `src/client/main.cpp`'s `--singleplayer` path already use. A second,
separate mistake in the same test: positioning a test player only 0.5m from a
spawned drop caused it to be picked up (and thus removed from replication)
before the visibility assertion ever ran -- fixed by using the same 2m
"visible but not yet picked up" distance the existing
`vb.world.spawn_item_drop` replication test already established. Verified:
full `vb_tests` 334/334 green (2 new `pack_runtime_test.cpp` cases for
`represents=` validation, 2 new `pack_runtime_integration_test.cpp` end-to-end
cases proving a real client resolves the overridden kind via `entity_kind()`
for both a spawned drop and another player), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward), `content/base loads
cleanly...` test confirms the new/edited Lua files parse and register
end-to-end. The actual rendered sprite/animation was **not** manually
eyeballed -- no GUI in this agent environment, same still-open caveat as
follow-up #6 and every other recent rendering-adjacent pass. **Deliberately
out of scope:** per-instance `ScriptState.visual_override` (skins), 8-facing
art and run/jump/fall/hurt/dead clips for the player (idle+walk/4-facings was
the scoped fidelity) -- undeclared clips already fall back to the first
declared clip (`resolve_clip`), same as any other kind.

**Same-day follow-up #6: real per-kind entity sprite art,
`vb.register_entity{visual = {...}}`** (REMAINING_TASKS' long-tracked Phase
4/6.1 gap -- the schema was finalized 2026-09-17 in
`architecture_spec/rendering.md` §11.3 but nothing read it; every script
entity rendered as a flat single-color, single-frame billboard). Protocol
bumped **19 -> 20**: `EntityKindRegistryRecord` gains an optional `visual`
(`protocol::EntityVisualDef` -- texture path, resolved frame_width/height,
facings, origin, a `clips` list), absent for a kind that only sets
`width`/`height` (e.g. `kitchen_sink:sentry` -- unaffected). `PackRuntime`'s
`register_entity` binding shape-validates `visual` at registration (unknown
variant / missing texture / bad facings / out-of-range origin / empty or
malformed clips all throw) with zero image decoding on that (headless)
side -- the real PNG's pixel dimensions are validated separately,
client-side, by a new pure header `vb/render/entity_visual_layout.hpp`
(`build_entity_visual_layout`), once `EntityRenderer::set_kind_visual()`
(wired from `src/client/main.cpp`, same one-shot join-time spot the block
texture atlas is built) actually decodes the synced/on-disk texture -- a
mismatch there is a `VB_WARN` + flat-placeholder fallback, not a pack-load
failure. `EntityRenderer::draw()` now indexes into the real spritesheet using
the pose/clip machinery that already existed and was already unit tested
(`select_pose`/`resolve_anim_clip`/`EntityPresentationState` -- none of it
changed): a new `render::anim_clip_name(AnimClip)` bridges the clip enum to a
pack's clip names (falling back to the first declared clip if a pack never
named the resolved one, per spec), frame index comes from `clip_time * fps`
wrapped via modulo (the finalized schema has no separate loop/hold-last-frame
flag -- simplification noted explicitly, not a missed field). **Deliberately
out of scope, left as real follow-ups:** per-instance
`ScriptState.visual_override` (skins) and real base-pack art
(`base:player`/`base:dropped_item` shipping actual spritesheets, still
"5.1"); this pass proved the mechanism using synthetically-generated PNGs at
test time (raylib `ExportImageToMemory`, `texture_atlas_test.cpp`'s own
pattern), not new binary art checked into `content/`. Verified: full
`vb_tests` 331/331 green (12 new cases across `protocol_test.cpp`,
`pack_runtime_test.cpp`, a new `entity_visual_layout_test.cpp`, and
`entity_visual_test.cpp`'s `anim_clip_name` coverage), clean `-Werror` build
of `vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily
reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed
clean, reconfigured back to this dir's OFF default afterward). The actual
rendered sprite/animation (a human watching a real spritesheet animate on a
billboard) was **not** manually eyeballed -- no GUI in this agent
environment, same still-open caveat as every other recent rendering-adjacent
pass.

**Same-day follow-up #5: automatic despawn-on-health for generic script
entities** (REMAINING_TASKS' Phase 6.1 "no health primitive exists; a pack
tracks HP on `self` itself" gap). Opt-in per kind via
`vb.register_entity{health=...}` (new `EntityKindDef::max_health`, rejects
`health <= 0` at registration time) — a kind that never sets it is completely
unaffected (`entity:damage()` stays notification-only: fires `on_hit`,
nothing else, exactly as before this landed). A kind that opts in gets a
per-instance current health (`ScriptEntity::health`, seeded from the kind
default in `vb.world.spawn`), new `entity:get_health()` (returns
`{current, max}` or `nil` if untracked) / `entity:set_health(value)` (clamped
`[0, max]`, errors if untracked) accessors, and `entity:damage()` now
decrements the tracked value and calls the existing `despawn_entity()` (fires
`on_death`, deregisters from the interest grid) once it reaches 0 -- the same
path `entity:remove()` already used, just triggered automatically. Server-
side bookkeeping only, never replicated (no client HUD reads a script
entity's health, so no protocol version bump).
**Gotcha hit while wiring this up, worth knowing before touching
`dispatch_entity_hit` again:** `on_hit` is arbitrary pack Lua and can itself
call `self:remove()` -- kitchen_sink's `entities/sentry.lua` does exactly
this, tracking its own hand-rolled hp on `self` rather than the new engine
primitive. The first draft found the `ScriptEntity` iterator once, fired
`on_hit`, then kept using that same iterator to touch `.health` -- a
use-after-erase the instant `on_hit` despawns the entity itself, since
`despawn_entity()` erases the map entry out from under it.
`kitchen_sink_pack_test.cpp`'s existing sentry test caught this immediately
(an MSVC STL iterator-debug assertion, not a silent corruption) the first
time the changed `pack_runtime.cpp` was rebuilt and the full suite run --
confirmed by `git stash`-ing back to the pre-change tree and re-running the
identical test in isolation, which passed clean. Fixed by re-`find`ing the
entity by id *after* the `on_hit` call returns, instead of reusing the
pre-call iterator. Takeaway: any C++ code here that calls into pack Lua and
then wants to keep touching the same engine-side entity/map entry afterward
must assume the Lua call may have deleted that very entry, and re-look it up
-- `dispatch_entity_tick`'s per-tick loop already re-`find`s for exactly this
reason (its own comment: "removed by an earlier handler this tick"), but
`dispatch_entity_hit` had not been paying that same tax until this pass.
Verified: full `vb_tests` 319/319 green (a new
`pack_runtime_integration_test.cpp` case spawns a `health=5` kind next to an
opted-out kind, proves 2 hits of 3 auto-despawns the tracked one at exactly 0
with no explicit `:remove()` call while the untracked one survives 1000
damage notification-only; a `pack_runtime_test.cpp` case covers the
registration-time `health <= 0` rejection), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward -- same verification
pattern as every other recent pass).

**Same-day follow-up #4: client-side kind-specific rendering for script
entities** (REMAINING_TASKS' Phase 6.1 "EntityKind id threads through but
nothing branches on it" gap). Protocol version bumped **18 -> 19**: new
`S2C_EntityKindRegistry` (type 53, `inc/vb/protocol/world.hpp`) carries
`{name, width, height}` per `vb.register_entity` kind, `kinds[i]` describing
`EntityKindId i+1` (matches `PackRuntime::Impl::entity_kinds`' own
registration-order id assignment, so no id needs to ride the wire). Sent
between `C2S_Ready` and `S2C_JoinAccept` alongside `S2C_BlockRegistry`/
`S2C_KeybindRegistry` via a new `HandshakeServerHost::entity_kind_registry`
hook, same `nullopt` = no frame, no behavior change posture as every other
opt-in registry here. `PackRuntime::install_entity_kind_registry(host)`
mirrors `install_keybind_registry`'s shape exactly; wired into both
`src/server/main.cpp` and `src/client/main.cpp`'s `--singleplayer` host
(alongside `install_join_veto`, mirroring `host.block_registry`'s presence in
both paths). `vb.register_entity{width=, height=}` (new optional fields on
`EntityKindDef`, default `0.8`/`1.8` -- `render::EntityRenderer`'s own
existing placeholder quad dimensions) lets a pack size a kind's billboard;
`ClientSession::entity_kind(EntityKindId)` looks a script entity's record up
by id (`nullptr` for `kInvalid`/players or an id with no registry entry --
same "missing = default" fallback as everywhere else), and
`EntityRenderer::sync()` now re-checks it every frame per tracked entity
(cheap, one map lookup) to pick the billboard's width/height, replacing the
one flat `kPlaceholderWidth`/`kPlaceholderHeight` every kind used to render
as regardless of what it was. Players (`EntityRecord::kind == kInvalid`)
still always get the placeholder default. `content/examples/kitchen_sink`'s
`entities/sentry.lua` demonstrates the new fields (`width=1.0, height=1.2`).
**Deliberately not attempted:** real per-kind sprite art/atlas
(`visual = {...}`, REMAINING_TASKS' own still-open item) -- this only closes
the "nothing branches on kind at all" gap with a differently-sized flat
placeholder, not real art. Full `vb_tests` 317/317 green on `build-net-lua`
(5 new cases: a protocol round-trip + cap-free-list test, two
`block_registry_test.cpp` end-to-end client-applies-it/no-hook-means-empty
cases mirroring the existing keybind-registry pair, two `pack_runtime_test.cpp`
cases for `install_entity_kind_registry`'s built/empty output), clean
`-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
confirmed clean, reconfigured back to this dir's OFF default afterward --
same verification pattern as every other recent phase). The actual rendered
size difference (a human watching two different-sized billboards in a real
window) was **not** manually eyeballed -- no GUI in this agent environment,
same still-open caveat as every other recent Phase 6/7 item's own
verification note.

**Same-day follow-up #3: closed Phase 3's two remaining ECS items --
`vb::ecs::SystemRunner` (§7.2) + the client-side lightweight registry.**
`SystemRunner` (`inc/vb/ecs/system_runner.hpp`/`src/ecs/system_runner.cpp`,
new `vb/ecs` module) is a named, ordered list of tick phases;
`ServerSession::build_systems()` registers `tick()`'s existing phases under
it plus one new one, `system_sync_interest()`. Script entities (Phase 6.1)
are now real registry entities (`spawn_script_entity`/
`set_script_entity_state`/`remove_script_entity` write `Position`/
`EntityKind`/`NetReplicated`/`Rotation`/`Velocity` components instead of
only touching the interest grid), giving the runner a genuine second
consumer besides players -- `system_sync_interest()` generically pushes
every such entity into `interest_` once a tick
(`registry_.view<Position, NetReplicated>(exclude<PlayerTag>)`).
Client-side: `ClientSession`'s old bespoke `RemoteSample`/`remote_samples_`
interp bookkeeping is now a real `entt::registry` + the
already-defined-but-previously-unused `ecs::InterpBuffer` component.
`remote_entities()`/`interpolated_pos()`'s public API is unchanged (3 test
files + `entity_renderer.cpp` needed no edits). See
`remaining_tasks/phase3.md` for the full write-up.

**Gotcha hit and fixed during this pass, worth knowing before touching
`ServerSession::tick()` again:** `interest_` (the `replication::InterestGrid`)
and `registry_` (the EnTT registry) are *not* interchangeable "current
position" sources within a single tick, even though both eventually hold the
same value. Several `network_io`-phase handlers -- `handle_block_edit`,
`handle_block_break_begin`, `punch()` (called synchronously from a
`vb.on("player_input", ...)` Lua hook, i.e. *during* `handle_input_batch`) --
need a player's *just-simulated-this-tick* position/rotation for reach/hit
checks. The first attempt at `system_sync_interest()` made it fully generic
(all `Position+NetReplicated` entities, players included) and moved it to a
single once-per-tick pass after `network_io` -- this silently broke reach
checks (a block breaks only if still in range *after* the input that moved
the player toward it, but the edit message can arrive in the same batch as
that move) and 2 of 312 tests failed on it. Fix: players keep their original
*immediate* `interest_.upsert()` calls (`handle_input_batch`,
`check_respawns`, `set_player_state`, join) exactly as before;
`system_sync_interest()` is scoped to script entities only (`exclude
<PlayerTag>`), which have no such synchronous same-tick reader. The 4 reach-
sensitive handlers above were also changed to read `ecs::Position`/
`ecs::Rotation` straight off `registry_` instead of `interest_`, since that's
always live regardless of which pass has run. Takeaway: `registry_` is the
one true "right now" source; `interest_` is a replication-facing mirror that
different entity kinds are allowed to refresh on different cadences (players:
immediately; script entities: once/tick) as long as every *reader* is aware
of which cadence it's getting.

**Same-day follow-up #2: fixed the multiplayer loading screen dismissing over
a still-empty world again** (user-reported regression, same symptom the
original Phase 7.1 loading-screen bug had). Root cause: Phase 7.6's world
persistence (below) added a *second*, unbudgeted synchronous-work path into
`ChunkLifecycleSystem::update()` -- step 2's saved-chunk disk load+relight
loop iterates the entire `desired` view box calling `region_store_->load()`
synchronously with no cap, unlike step 1's `ingest_budget_`-bounded worldgen
ingest right above it. Rejoining a previously-saved world with a real
`voxel_browser_server` re-loads its *entire* initial view box (~2000 chunks
at view_distance=8) from disk in one `update()` call, blocking that tick (and
the network send that follows it) long enough that the client's 5s
loading-screen stall deadline fires before any chunks arrive. Fixed by
sharing `ingest_budget_` across both loops: step 2 now stops after
`ingest_budget_` disk-load attempts (hit or miss) per `update()` call and
leaves the rest for a later tick, instead of falling through to
`pool_.submit()` for a deferred coord (which would silently regenerate --
and discard -- a chunk that actually has saved data). Only fires when
`persist_world` is on and rejoining a world with existing saves; a fresh
world or `persist_world=false` is unaffected (matches `region_store_ ==
nullptr` early-outs already in place). `vb_tests` 312/312 green on
`build-net-lua`. See `src/world/chunk_lifecycle.cpp`'s step-2 comment.

**Same-day follow-up: fixed a singleplayer FPS dip during chunk streaming**
(user-reported: "the frame rate dipped" while chunks loaded, distinct from
the 2026-09-15 dip already fixed in `chunk_mesh_snapshot.cpp`). Root cause:
`ChunkLifecycleSystem::update()`'s `kIngestBudgetPerTick` (32 chunks
inserted+relit per call, `src/world/chunk_lifecycle.cpp`) was sized assuming
one `update()` call per real tick interval -- true for a dedicated server,
which sleeps between ticks, but not for `--singleplayer`'s
`Singleplayer::tick()` (`src/client/main.cpp`), which runs a fixed-step
catch-up loop of up to `kMaxStepsPerFrame` (5) server ticks inside a *single*
rendered frame after any stall. Each step called `update()` with the full
budget, so a stall could relight up to 5x32=160 chunks synchronously in one
frame -- expensive enough (flood-fill sky+block light over ~32k voxels each)
to cause the next frame's stall too, a self-sustaining stutter. Fixed with
`ChunkLifecycleSystem::set_ingest_budget()` / `WorldReplicator::
set_chunk_ingest_budget()` passthrough (new, both header-only setters);
`Singleplayer::tick()` now computes `expected_steps` up front (pure function
of `tick_accum_/kFixedDt`, capped the same as the loop below it) and divides
the base budget across them before the catch-up loop runs, so one frame's
total ingest work stays bounded to roughly the original per-tick budget
regardless of how many steps it catches up on. A dedicated server never
calls `set_chunk_ingest_budget()` at all, so its behavior (and the existing
`kIngestBudgetPerTick`-sized test expectations) is unchanged. Full
`vb_tests` 312/312 green, clean `voxel_browser`/`voxel_browser_server`
rebuild, on `build-net-lua`. **Not fixed by this pass** (separate, still-open
item, see §6 below): `WorldReplicator::tick()`'s per-player chunk *encode+
send* loop is still an uncapped burst with no byte budget of its own --
today's fix only bounds the ingest/relight side.

Most recent landed item is **Phase 7.6: world persistence** (chunks now
survive a `voxel_browser_server` restart), closing ARCHITECTURE_SPEC.md
§18 row 5. **Reversed that row's 2026-09-17 "lean toward LMDB" direction
note** after an AskUserQuestion with the user during this pass — landed as
flat per-region files instead, no new dependency. New `vb::world::RegionStore`
(`inc/vb/world/region_store.hpp`/`src/world/region_store.cpp`) groups chunks
into one file per 16x16-chunk X/Z region (Y ungrouped -- this engine's
generated worlds are only a few chunks tall, unlike Minecraft's motivating
case), reusing the existing `chunk_codec.hpp` palette+RLE payload as-is. Only
*edited* chunks are ever written: `Chunk::revision() == 0` (untouched since
worldgen) is skipped, and a chunk already cached/on-disk at its current
revision is never re-encoded -- `save_if_dirty()` only updates an in-memory
per-region cache, `flush()` is the one call that actually rewrites a dirty
region file, so many edits to the same region across a sweep cost one disk
write, not one per chunk. `ChunkLifecycleSystem` takes an optional
`RegionStore*` (nullptr = disabled, the usual opt-in-seam posture in this
codebase): its request step loads a wanted chunk from disk instead of
submitting it to worldgen when one was saved there, its unload step saves an
edited chunk before evicting it. `WorldReplicator::set_region_store()`
forwards straight through to it. `src/server/main.cpp` owns the `RegionStore`
(three new `server.toml` keys: `persist_world` default `true`, `world_dir`
default `"world"`, `autosave_interval_seconds` default `60.0`), sweeps every
loaded chunk through it on that interval and unconditionally once more right
before exiting. **Gotcha hit while writing this:** `RegionStore` needed its
own local copy of the `read_whole_file()` idiom (ifstream + `ate`/`tellg` +
manual `vector<byte>` fill) -- `std::vector<std::byte>` still can't be built
directly from `std::istreambuf_iterator<char>` under this repo's toolchain
(same §4 gotcha `assetsync/cache.cpp` and `script/db.cpp` already worked
around independently; there's no shared helper to import, `assetsync` isn't a
dependency of the `vb::world` module). **Deliberately not covered:**
`--singleplayer`'s in-process integrated server has no `RegionStore` wired in
at all (it has no `ServerConfig`/`server.toml` on that path to begin with --
see `make_singleplayer_pack_runtime`'s own comment in `src/client/main.cpp`),
and region files carry no LZ4/zstd framing yet (RLE only) -- both tracked as
their own `remaining_tasks/deferred.md` items now, not silently dropped. Full
`vb_tests` 312/312 green (5 new `region_store_test.cpp` cases, one of them a
real `ChunkLifecycleSystem`-through-`WorldReplicator` end-to-end round trip:
edit a block, walk far enough to unload+save it, walk back and confirm it
loads from disk with the edit intact instead of regenerating), clean
`-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
confirmed clean, reconfigured back to this dir's OFF default afterward).
Also manually ran `voxel_browser_server.exe --ticks 40` against a scratch
working directory with no client connecting: confirmed no `world/` directory
gets created at all when nothing was ever edited (the "only persist edits"
design working as intended, not an oversight). Full REMAINING_TASKS.md
write-up under Phase 7's new 7.6 entry.

Before that, most recent landed item was the **real texture/atlas system**, closing the
"no real texture system in this engine at all" gap REMAINING_TASKS.md
repeatedly cited as blocking Phase 4's textured meshing, 6.5's crack
overlay, 5's sprite atlases, and 7.5's underwater tint default.
`kEngineProtocolVersion` bumped 16 -> 17 (`texture` field added to
`BlockRegistryRecord`/`BlockType`). New `vb::render::TextureAtlas`
(`inc/vb/render/texture_atlas.hpp`) decodes each block's synced PNG via
raylib `Image` functions and packs one fixed 16x16 cell per block id into a
single atlas, built once per session in `src/client/main.cpp` right after
the block registry is applied. **Gotcha found while wiring this up:**
`BlockRegistry::add_or_get` silently discards the `BlockType` it's passed
when the name already exists (by design — idempotent re-registration must
not reset an earlier call's fields) — so a pack file re-declaring an
already-hardcoded `BlockRegistry::base()` block (this pass's
`content/base/blocks/stone.lua`/`water.lua`, attaching a texture to blocks
`src/world/block.cpp` already defines) would silently no-op its `texture`
too. Fixed with a narrow `BlockRegistry::set_texture(id, path)`, the one
field this rule doesn't apply to — don't add more fields to that exception
without re-checking every pack that relies on "first registration's values
win" (`pack_runtime_test.cpp`'s idempotent-registration case is the
regression guard). Full write-up: `state/changelog-recent.md`. Full
`vb_tests` 306/306 green, clean build of both binaries, all 4 CTest cases
pass, on `build-net-lua`.

Before that, most recent landed item was **Phase 7.4: wire `content/base`'s UI screens to a
real trigger**. Root cause was one level deeper than "just add a keybind":
Phase 6.3's `vb.register_keybind`/`S2C_KeybindRegistry` gives a pack a
*named* bit in `InputCmd.keybinds`, but nothing on the client ever mapped a
**physical key** to a pack-registered custom name — only the pre-registered
engine names (movement + `primary`/`secondary`, Phase 6.19) got a real key.
Fixed with a new `kCustomKeybinds` table in `src/client/main.cpp`
(`{"base:pause", KEY_ESCAPE}`, `{"base:inventory", KEY_E}`) read
unconditionally in `sample_input_cmd` (not gated behind `mouse_captured`,
unlike the engine-name lookups — opening a menu must work whether or not the
mouse is currently captured), plus a new `content/base/keybinds.lua`
registering those two names and a `vb.on("player_input", ...)` rising-edge
handler calling `player:open_ui("base:pause", {})` /
`player:open_ui("base:inventory", { slots = player:get_inventory() })` — same
pattern `content/examples/kitchen_sink/keybinds.lua` already demonstrated.
`ui/pause.lua`/`ui/inventory.lua`'s stale "nothing opens this yet" header
comments updated. Still just a hardcoded default mapping, not a real
settings-screen UI for custom keybinds (Phase 5.3's rebind screen only
covers the 6 `MovementBindings` axes) — that's a further, separate step.
Full `vb_tests` 300/300 green on `build-net-lua`; the actual keypress →
screen-opens behavior wasn't manually eyeballed (no GUI in this agent
environment). Full REMAINING_TASKS.md write-up under Phase 7's 7.4 entry.

Before that, most recent landed item was **Phase 7.3: walkable liquid blocks** — collision
was already correct (verified, not rebuilt: `is_solid` already gated
`step_movement`, `base:water` was already non-solid); underwater rendering
is 7.2's same sky-color fog mechanism with a fixed close preset
(`fog_start=2.0f`/`fog_end=8.0f`) overriding whichever fog distance was
already chosen, triggered in `src/client/main.cpp`'s `kPlaying` block
whenever `client->chunk_store().registry().is_liquid()` is true for the
camera's own floored eye position (**same-day follow-up fix:** the fog
change alone wasn't enough — `src/world/chunk_mesh_snapshot.cpp`'s face
culling had always symmetrically treated a liquid neighbour like an opaque
one, so an opaque block's face touching water was culled too, making
submerged terrain invisible from the water side, a pre-existing bug the old
`mesher_test.cpp` even asserted as expected; fixed by keying culling off the
*current* voxel's own type — a liquid neighbour now only culls another
liquid's own face, never an opaque block's — see REMAINING_TASKS.md 7.3's
entry for the full writeup); and a new generic `BlockType::region`
flag (`base:water` only in the base set; `vb.register_block{region=}`
defaults to the block's own `liquid` value) drives
`ServerSession::update_region_occupancy()` (`src/net/session.cpp`, one new
per-tick pass reading the same `interest_` position source
`update_item_drops` uses, diffed against a `NetId`-keyed
`region_occupancy_` map) firing `vb.on("region_enter"/"region_exit", player,
pos, block_name)` exactly on the crossing, not per tick spent inside — no
wire-protocol change, this is server-local Lua dispatch only, gated
zero-cost behind `ServerSession::RegionHooks` (unset function fields = no-op)
the same way `BlockBreakHooks` already is. No engine-side swim-speed/
movement-slowdown policy was added — that's left entirely to a pack built on
top of the hook, same "engine provides the primitive" posture as everything
else in Phase 6/7. **Second same-day follow-up fix:** the mesher fix alone
made the lake surface look "broken, like there are holes" from *above*
(user-reported, screenshot-confirmed) — real cause was
`src/render/chunk_renderer.cpp`'s `tint_for()` giving water `alpha=200`
(semi-transparent), a Phase-2-era value that was inert as long as submerged
terrain was always culled (nothing behind it to blend with). Once the mesher
fix made that terrain render, the same alpha let the sandy lakebed blend
through as flat, hard-edged, chunk/voxel-shaped patches — no wave/refraction
shading exists to sell it as water, so it read as corrupted geometry rather
than "shallow clear water." Fixed by bumping water to `alpha=255`, opaque
like every other block; underwater visibility while swimming is untouched,
it's driven entirely by the fog system once the camera's own eye voxel is
inside the water, not by this material's alpha.
Full `vb_tests` 300/300 green on this machine's plain
`build` dir (both `VB_WITH_NET`/`VB_WITH_LUA` ON); also re-verified a clean
`-Werror` build (`-DVB_WARNINGS_AS_ERRORS=ON`, matching every CI workflow)
of `vb_tests`/`voxel_browser`/`voxel_browser_server` before reconfiguring
back to this dir's plain-local OFF default. Full REMAINING_TASKS.md
write-up under Phase 7's 7.3 entry.

Before that, most recent landed item was **Phase 7.2: distance fog**, protocol version
bumped to **16**. A real GLSL 330 shader (`kFogVs`/`kFogFs` in
`src/render/chunk_renderer.cpp`, loaded once in `ChunkRenderer`'s
constructor) replaces raylib's default mesh shader on every chunk's
material — attribute/uniform names match raylib's own defaults exactly
(`vertexPosition`/`mvp`/`matModel`/`colDiffuse`/`texture0`) so `DrawMesh`
keeps auto-wiring those; only the `fogViewPos`/`fogColor`/`fogStart`/
`fogEnd` uniforms and a linear mix at the end of the fragment shader are
new. `ChunkRenderer::set_fog(view_pos, sky, start, end)` is called once per
frame from `src/client/main.cpp`'s `kPlaying` case with the exact `SkyColor`
already computed for that frame's `ClearBackground` — fog can't
independently drift from the sky by construction. New wire message
`S2C_FogParams` (type 52, `f32 fog_start, f32 fog_end`) is opt-in like
`S2C_MoveParams`/`S2C_DayNightCurve`, but with no server-side universal
default to fall back to (the server doesn't know each client's own
`view_distance`) — `ClientSession::fog_override()` is `std::optional`, and a
client with none computes its own default from `view_distance * kChunkDim`.
Lua: `vb.render.set_fog{start=, ["end"]=}` (`PackRuntime::
effective_fog_params()`) — note `end` needs a quoted key, it's a Lua
reserved word (caught the hard way: the first test-case draft used a bare
`end = ...` key and failed at Lua parse time, not at the C++ validation it
was meant to exercise). No color field, by design (matches 6.8's "operator/
pack persona" split already established for day/night). Full `vb_tests`
green (298/298) on `build-net-lua`; the actual rendered fog wasn't manually
eyeballed (no GUI in this agent environment). Full write-up under
REMAINING_TASKS.md's Phase 7.2 entry.

Before that, most recent landed item was **Phase 7.1: engine-side loading screen** — a new
`AppState::kLoading` in `src/client/main.cpp`, entered right after a
successful join instead of dropping straight into `kPlaying`, left once the
initial view-box of chunks has streamed in (`client->chunk_store().size()`
over an expected count mirroring `WorldReplicator`'s `chunks_in_view` box
shape, (2·view_distance+1)²·7) or an 8-second deadline elapses. Stage 1:
`MainMenu::draw_loading()` (`src/render/main_menu.cpp`) — a generic
`GuiProgressBar`, no data dependency. Stage 2: operator branding is text-only
(`S2CServerInfo::motd`, already replicated during the handshake) — no color
field was added (would need a protocol bump, left for a future pass, see
REMAINING_TASKS.md 7.1's own note). `kLoading` also drives the load itself
(pumps `sp->tick()`/`client->tick()` + `chunk_renderer->sync()` at a higher
budget than `kPlaying`'s steady-state 8/frame) rather than passively waiting.
Full `vb_tests` green (294/294) on `build-net-lua`; the windowed state
machine itself wasn't manually eyeballed this pass (no GUI in this agent
environment) — build + full test suite is the verification that exists.
Full REMAINING_TASKS.md write-up under Phase 7's 7.1 entry.

Before that, most recent landed item was **6.21: unified interaction reach + physics/action
read-back** — `net::ActionParams` (new struct, `inc/vb/net/
world_replicator.hpp`, sibling to `BlockEditHooks`) replaces both
`WorldReplicator`'s old hardcoded, non-overridable `kMaxReachBlocks` constant
and the separate `ServerSession::PunchParams::reach` with one shared value:
`WorldReplicator::set_reach()`/`reach()` hold it, and both `in_reach()`/
`apply_block_edit()` and `ServerSession::punch()` (via
`world_replicator()->reach()`, `src/net/session.cpp`) read it. Exposed to Lua
as `vb.action.set_params{reach=...}` / `vb.action.get_params()` (new
`vb["action"]` table, `src/script/pack_runtime.cpp`,
`PackRuntime::effective_action_params`, same "engine default, pre-freeze
override" shape as `vb.physics`/`vb.combat`) — a new namespace, deliberately
not folded into `vb.combat.set_params` (which keeps only `hit_radius`/
`player_damage`/`heal_after_seconds`/`heal_interval_seconds`), so a pack
author hunting for the mining-reach knob doesn't have to think to search
"combat" for it. Also added `vb.physics.get_params()` (effective
`physics::MoveParams` as a plain table). `content/base/mechanics.lua`'s
placing raycast now reads `vb.physics.get_params().eye_height`/
`vb.action.get_params().reach` instead of its own hardcoded `EYE_HEIGHT`/
`max_dist` constants, so a pack override can never silently desync Lua's
target-selection from the engine's own reach check. Wired into both
`src/server/main.cpp` and `src/client/main.cpp`'s `--singleplayer` path right
next to the existing `move_params`/`punch_params` wiring. New tests: a
`blockedit_test.cpp` case proves one `WorldReplicator::set_reach()` call
moves both the block-edit and `punch()` accept/reject boundary together; new
`pack_runtime_test.cpp` cases cover `vb.action.set_params` (override +
post-freeze rejection + built-in default) and
`vb.physics.get_params()`/`vb.action.get_params()` round-tripping the
effective values. Full `vb_tests` green (294/294) on `build-net-lua`;
`voxel_browser`/`voxel_browser_server` also rebuild clean.

Before that, most recent landed item was **Phase 5.3: keybindings screen** — a new
Settings -> Keybindings raygui screen (`MainMenu::open_keybindings`/
`draw_keybindings` in `src/render/main_menu.{hpp,cpp}`) lets a player click
an action's current key and press any physical key to rebind it, for the 6
`MovementBindings` axes (forward/back/left/right/jump/sprint; mouse
primary/secondary are left alone). Rebind capture is `GetKeyPressed()`
polled only while a row is "listening" (Esc cancels without changing it).
Persisted as 6 new `std::int32_t key_*` fields on `vb::core::ClientConfig`
(raw raylib `KEY_*` values, hardcoded as plain ints since `vb_core` doesn't
depend on raylib — see the field comments), read/written in
`src/core/config.cpp` alongside the existing fields, round-trip tested in
`tests/unit/config_test.cpp`. `src/client/main.cpp` builds its live
`MovementBindings` from config at startup and rebuilds it immediately on
Keybindings-screen Save (no restart needed, unlike window size/vsync).
Distinct from and composes with Phase 6.19 below (that's a pack-visible
*name* registry; this is which *physical key* produces the held/not-held
state Phase 6.19 exposes by name) — resolves the last open half of
REMAINING_TASKS' "Keybindings screen (5.3 Settings)" item. Full `vb_tests`
green (290/290) on `build-net-lua`.

Before that, most recent landed item was **6.19: engine-default keybind
pre-registration** — `PackRuntime`'s
constructor now seeds 8 fixed names (`move_forward`, `move_back`,
`move_left`, `move_right`, `jump`, `sprint`, `primary`, `secondary`) into
the same Phase 6.3 `keybind_names` registry `vb.register_keybind` writes
into, before any pack script runs. `src/client/main.cpp`'s
`sample_input_cmd()` now sets the matching `InputCmd::keybinds` bits (found
by name in `ClientSession::registered_keybinds()`) alongside the existing
`cmd.move`/`cmd.buttons` it already set from `MovementBindings` — purely
additive, no wire-format or physics change. This resolves REMAINING_TASKS'
open "do movement axes fit the boolean-keybind shape" question (yes —
keyboard/mouse input is already boolean) but does **not** add key
rebinding: `MovementBindings`' WASD/Space/Shift/mouse mapping is still
hardcoded client-side; a real rebind UI is still Phase 5.3's keybindings
screen, untouched by this. Bumped 2 tests that assumed a bare
`keybind_names`/`registered_keybinds()` size or index (`pack_runtime_test.cpp`'s
cap test, `pack_runtime_integration_test.cpp`'s "dash" test) to account for
the 8 pre-registered slots. Full `vb_tests` green (289/289) on
`build-net-lua`.

Before that, most recent landed item was **6.18: Growtopia-style discrete
punch combat** — attack is a single
"punch" per click (not hold-to-mine), resolved server-side via
`ServerSession::punch(NetId)` against a vertical-cylinder hit-test over
blocks and nearby players, exposed to Lua as `player:punch()`. Right after
it, **6.17 decoupled block breaking from the engine entirely** (the
client's old hardcoded hold-to-break timer is gone; a content pack now owns
break timing via `player:break_block()` + `vb.on("player_input", ...)`) and
movement bindings were pulled into a `MovementBindings` struct (still
continuous/server-authoritative, not migrated to the boolean keybind
registry). Full `vb_tests` green (289/289) on `build-net-lua`; all 4 CTest
cases pass. **Full writeups for 6.18 (+ its block-self-heal follow-up),
6.17, 6.15, 6.14, 6.13, and the Phase 1.3 networking-polish item live in
`state/changelog-recent.md`** — these were never copied into the older §8
log and are NOT covered by the two files below. Full writeups for 6.16 and
every earlier landed feature live in `state/changelog-part1.md` (Phase
6.1-6.16 + investigations + Phase 0-4 bootstrap) and
`state/changelog-part2.md` (Phase 5.x content/UI + Phase 6 design passes +
librg) — **read all three before assuming something is unimplemented or
before re-deriving a design decision already made.**

**Known regression, deliberately accepted:** `client.break_progress()`
always returns `nil` now (6.17 removed the client-local timer it read; no
server-authoritative replacement exists yet — `BlockDamageSystem`'s damage
*value* was never wired to the wire protocol, see `state/changelog-part1.md`
Phase 6.5's entry). `content/base/ui/hud.lua` silently draws no progress bar
until that lands. Traded deliberately per explicit user direction
("breaking is opt-in content").

**Known simplification, not attempted:** no engine-side punch-rate cooldown
(6.18) — a pack that doesn't edge-detect input could call `punch()` every
tick; left as the calling pack's responsibility, same posture as every
other "engine provides the primitive" seam in this codebase.

---

## 0. Repo snapshot

- `674d88f Initial commit` + the Phase 0 restructure (uncommitted at time of
  writing). Still **no tags**. Remote: `https://github.com/RechieKho/voxel_browser.git`.
- Source tree matches spec §4: `vb_core` (`src/core/`), `vb_render`
  (`src/render/`), `voxel_browser` (`src/client/`), `voxel_browser_server`
  (`src/server/`), `vb_tests` (`tests/`). Other module dirs are `.gitkeep` stubs.
- Builds green on Windows/clang with `-DVB_WARNINGS_AS_ERRORS=ON`.
- `ARCHITECTURE_SPEC.md` / `REMAINING_TASKS.md` are still design intent for
  Phase 1+.

---

## 1. Build-blocking bugs — ✅ all fixed in Phase 0

All four were fixed during the Phase 0 restructure (2026-09-10):
version info moved to a generated `vb/core/version.hpp`; raylib pinned to
`5.5` (not `6.0` — that tag never existed) with raygui matching `4.0`;
`cmake_minimum_required` bumped to `3.25`; a `CMAKE_POLICY_VERSION_MINIMUM
3.5` shim in `cmake/Dependencies.cmake` works around CMake ≥ 4.0 rejecting
doctest 2.4.11's `cmake_minimum_required(VERSION 3.0)` (remove the shim
once doctest ships a fix). Full detail in `state/changelog-part1.md`'s
2026-09-10 entries.

## 2. Naming — ✅ resolved

`PROJECT_NAME` is `voxel_browser`. Executables: `voxel_browser` (client),
`voxel_browser_server`. CI artifacts are
`voxel_browser-<target>-<arch>-<build_type>`.

---

## 3. CI landmines

- **No tags exist.** `setup_metadata.yml` runs `git describe --tags
  --abbrev=0` for the `version` output — this **errors** with no tags in
  history. `bundle`/`publish` depend on `setup_metadata`; first `git tag
  v0.0.1` will unblock. Until then anything past `build_*` is untested /
  likely red.
- `publish.yml` triggers only on `v*.*.*` tags and pulls artifacts from
  `runner.yml` by name `${project_name}` (note trailing space in the YAML
  on `name:` line 26 — `action-download-artifact` may or may not trim it).
- `lint.yml` installs clang-format via `pip install` then runs it via
  `pipx run clang-format` (dead weight from the pip install). Lints `src/**`
  with `--Werror` — every new file under `src/` must be clang-format-clean.
- Build workflows use `actions/checkout@v4 submodules: recursive`, but
  **there are no submodules** — deps are `FetchContent`. Harmless.
- `build_linux.yml` installs X11/GL dev packages for raylib, plus
  `libssl-dev libprotobuf-dev protobuf-compiler` for `VB_WITH_NET`.
- **`VB_WITH_NET` CI coverage is uneven:** Linux (apt) and Windows (vcpkg)
  build it; **macOS does not** (single-arch Homebrew protobuf vs. the
  universal arm64+x86_64 build — see `state/dependencies-detail` pointer
  below). If macOS ever gets it, `build_macos.yml`'s configure step is
  where to add it.
- **Never call `find_package(Protobuf REQUIRED)` a second time anywhere in
  the tree.** GameNetworkingSockets' own `src/CMakeLists.txt` already calls
  it; a second call (even as a "fail fast" convenience) fatal-errors on
  some protobuf installs (Homebrew, not vcpkg) with "Some (but not all)
  targets in this export set were already defined." Fixed 2026-09-11 by
  deleting the redundant call — don't reintroduce it.
- **Machine-local build/toolchain/agent-shell gotchas (vcvars64.bat path,
  Bash-vs-PowerShell-tool quirks, which pre-built `build-*` dirs actually
  exist, macOS-specific findings) live in `STATE.md.local`, not here** —
  gitignored (`*.local`), specific to whatever physical machine an agent
  session runs on; check it first when setting up a build in an agent
  shell, and add to it rather than here when you hit a new one.

---

## 4. Config / style quirks

- `.clang-format` is **Godot's** clang-format file verbatim. The C++ rules
  that matter: **tabs** (`UseTab: Always`, `TabWidth 4`), `ColumnLimit: 0`
  (no auto-wrap), `AccessModifierOffset: -4`, pointers right-aligned,
  `Cpp11BracedListStyle: false`. `Standard: c++17` but CMake sets
  `CMAKE_CXX_STANDARD 20` — fine unless C++20-only syntax confuses the
  formatter; bump to `c++20` if that happens.
- `.gitignore` ignores `build`, `*.local`, `compile_commands.json`,
  `.vscode/*` (except `extensions.json`). `CMAKE_EXPORT_COMPILE_COMMANDS
  ON` is set — symlink/copy `build/compile_commands.json` to root for
  tooling.
- `CMakeLists.txt:54` uses `file(GLOB_RECURSE SOURCE_FILES ...)` — adding a
  `.cpp` doesn't trigger reconfigure. Keep `cmake` re-runs in the loop.
- `libfantastic` (the lib target) is `add_library(... INTERFACE)` — assumes
  header-only. `vb_core` is a real STATIC lib; don't carry the INTERFACE
  assumption forward.
- `.gitignore`/`tests/CMakeLists.txt` are checked in with CRLF endings
  (every other text file is LF-only) despite `core.autocrlf=input` locally
  — `git add` warns harmlessly today, but the first real edit through a
  CRLF-preserving tool will silently flip the whole file's line endings,
  burying the actual diff. No `.gitattributes` forces this repo-wide. If
  ever cleaned up, use `git add --renormalize <path>` after adding a
  `.gitattributes` rule, not a manual find/replace.

**Longer-form gotchas (compiler/toolchain traps, full detail in
`state/gotchas.md`):**
- `std::erase`/`std::remove` on a `std::vector<ChunkCoord>` (or any small
  trivially-copyable struct that size/alignment shape) fails to compile
  under this repo's clang-targeting-MSVC-STL toolchain
  (`static_assert(false, "unexpected size")` inside `<xutility>`). Use a
  manual erase loop instead.
- A local `clang-format --dry-run --Werror` binary may not be trustworthy
  as-is against this repo's `.clang-format` — verify by diffing against an
  unmodified `HEAD` file before trusting either a pass or a wall of
  violations.
- `doctest`'s `--test-case=` filter is a **glob pattern**, not a substring
  match — wrap in `*...*` and quote it (zsh glob-expands an unquoted
  pattern itself).
- MSVC (`cl.exe`) rejects a ternary between two different instantiations of
  a templated smart-pointer type with converting constructors (`C2445`) —
  use plain `if`/`else` instead. Clang/GCC not cross-checked.
- A sibling file's "isn't implemented yet" comment can go stale the moment
  a later phase lands it, without the comment ever being updated — grep the
  actual current binding (e.g. `src/script/pack_runtime.cpp`) before
  reusing a sibling's "this doesn't work yet" framing in new code.
- A client built without `VB_WITH_COMPRESSION` can never asset-sync
  against a server built with it on (`hash_bytes()` stubs to all-zero) —
  every real (non-`--singleplayer`) connection needs **matching**
  `VB_WITH_COMPRESSION` on both binaries; there's no runtime negotiation.
- A second, independent bug produces the identical `"asset transfer failed
  (hash mismatch or size cap)"` text even with matching compression flags:
  the server used to build the asset manifest *before* `vb.storage`'s
  deferred first-tick flush reached disk. Fixed by calling
  `pack_runtime.flush_storage()` before `build_manifest()` in
  `src/server/main.cpp`. **This error string has (at least) two unrelated
  root causes** — check both before assuming which one you've hit.

---

## 5. Dependency notes / unknowns

- **Cellulose** (`github.com/RechieKho/cellulose`) — evaluated as a greedy
  mesher, then reverted after it reproduced an NVIDIA-driver VAO/VBO
  heap-corruption crash (greedy-merged geometry blows past the renderer's
  buffer-reuse headroom far more often than the hand-rolled per-face
  mesher). Hand-rolled `vb::world::chunk_mesher` is the **permanent**
  meshing backend now, not a placeholder. `VB_WITH_MESHING` scaffolding was
  later removed outright at user request (2026-09-17). Full story in
  `state/changelog-part2.md`.
- **FastNoise2**: real upstream is `Auburn/FastNoise2` (the README's
  `electronicarts/fastnoise` link is a fork, don't use it). Pin is
  `v0.10.0-alpha`, not `v0.10.0` (that tag doesn't exist — a real,
  previously-unnoticed bug fixed in Phase 6.14, see `state/changelog-part1.md`).
- **GameNetworkingSockets** — resolved (Phase 1.2, 2026-09-11), the single
  hardest dependency in the project. Protobuf cannot be `FetchContent`-ed
  (must be a real package-manager install: vcpkg/apt/brew). GNS pinned to
  `v1.6.0` (not `v1.4.1`, which doesn't compile under a modern stdlib). ICE/
  WebRTC disabled. Windows crypto is BCrypt; Linux/macOS use system
  OpenSSL. Linked statically. **Full story, including local-verification
  toolchain quirks and the Homebrew double-`find_package` crash, is in
  `state/changelog-part1.md`'s Phase 1.2 entries — read those before
  touching `cmake/Dependencies.cmake`'s GNS block again.**
- **librg** is `zpl-c/librg`, pinned `v7.4.0`, wired in as the default
  entity-replication backend since 2026-09-17 (`VB_WITH_REPLICATION`
  default ON; the original hand-rolled linear scan remains as an explicit
  opt-out). Known limitation: chunk ids need `chunkamount.x*y*z` to fit
  signed int32 and each axis casts to `int16_t` — picked 1024 chunks/axis
  (±512×cell_size world units); an entity straying outside that range is
  silently excluded from interest until it re-enters. Full detail in
  `state/changelog-part2.md`.
- **Lua**: PUC-Lua has no upstream CMake; needs a wrapper `CMakeLists.txt`.
  sol2 is the chosen binding layer (pinned `v3.5.0`, not `v3.3.0` — that
  version's bundled optional impl fails under Clang ≥ 18).
- **raygui** is header-only (`raysan5/raygui`), needs exactly one TU with
  `#define RAYGUI_IMPLEMENTATION` in its own target (`vb_raygui_impl`) so
  project `-Werror` never touches vendored code — same pattern used for
  librg's `LIBRG_IMPL`.

---

## 6. Design decisions still open

Tracked in `ARCHITECTURE_SPEC.md` §18, repeated here for visibility:

1. ~~Lua binding layer: `sol2` vs. raw C API.~~ **Resolved: sol2.**
2. ~~Cellulose meshing API shape.~~ **Resolved and reverted** (2026-09-16)
   — hand-rolled meshing stays permanent.
3. librg version + whether we use its serialization or only its interest
   culling. **Resolved (2026-09-17): interest culling only**, own codec for
   payloads.
4. Chunk compression: LZ4 (spec's starting choice) vs. zstd vs.
   palette-only. Still open.
5. ~~World persistence / region file format.~~ **Resolved 2026-09-25:** flat
   per-region files (`vb::world::RegionStore`), not the previously-noted LMDB
   direction — reverted after review, see "Current status" above. Wired into
   `voxel_browser_server` only; `--singleplayer` still has no persistence.
6. Auth: `auth_mode = none | token` — handshake reserves the field, no
   service exists.

Other undecided:
- Test framework: doctest (in use) vs. Catch2 — never revisited since
  Phase 0's pick.
- Whether the client embeds the server for singleplayer as a library or
  spawns a child process — **currently: in-process library**
  (`Singleplayer` in `src/client/main.cpp` constructs `LoopbackNetwork`/
  `ServerSession`/`ClientSession` directly).
- **`WorldReplicator` streams a whole player's view box in one uncapped
  burst per connect**, no per-tick pacing. GNS's send buffer was raised to
  32 MiB (2026-09-15) which comfortably covers the shipped default view
  distance (8/3, ~2023 chunks, ~545 KB measured) but doesn't add real
  backpressure — a larger view distance, denser world, or several players
  joining at once could still overflow it. Proper fix: a per-connection
  byte-budget-per-tick on the `diff.entered` send loop. **Revisit before
  ever raising the shipped default view distance.** Still open as of the
  2026-09-25 disk-load-budget fix above — that fix bounds
  `ChunkLifecycleSystem::update()`'s *ingest* side (worldgen + region-store
  disk loads) per tick, not this *send* side; a large view box still leaves
  in one uncapped burst once ingested.

---

## 7. Conventions to follow

- Namespaces `vb::<module>` (`vb::net`, `vb::world`, ...); headers under
  `inc/vb/<module>/`, mirrored by `src/<module>/`.
- No exceptions on hot paths; `Result<T,E>` for fallible ops. Never call
  `.error()` when a `Result` holds a value (union UB) — `REQUIRE(result)`
  then deref in tests, not `REQUIRE_MESSAGE(result, msg(err))`.
- Wire structs live in `inc/vb/protocol/`; every one gets a round-trip
  test and bumps `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` when
  changed. Concrete step-by-step for adding one: struct → `MessageType` →
  round-trip test → version bump → `docs/protocol.md` entry → integration
  test (see `CONTRIBUTING.md`).
- Keep both binaries buildable/runnable with `--headless` (CI +
  integration tests depend on it).
- Match `.clang-format`: tabs, no column limit, run clang-format before
  commit (CI `--Werror` on `src/**`).
- New Lua bindings should be **generic primitives, not game-specific
  ones** — game rules belong in `content/*` packs (e.g. `player:take()` is
  a generic inventory primitive; the actual crafting recipe list lives
  entirely in `content/base/crafting.lua`, not in engine code). See
  `CONTRIBUTING.md` for the worked example.
- Heavy deps (GNS, librg, Lua/sol2, FastNoise2, LZ4/xxHash) are declared in
  `Dependencies.cmake` but gated behind `VB_WITH_*` (several now default
  ON — check `CMakeLists.txt` for current defaults, don't assume OFF).
- doctest + MSVC STL: comparing/streaming a `std::string_view` in a
  `CHECK` needs `#include <ostream>` in that test TU (instantiates
  `toString<string_view>`).
- GCC gotcha: `uint64_t` (`unsigned long` on LP64) vs. `...ULL` literals
  trips `-Wsign-conversion` — always name wide constants `constexpr
  std::uint64_t`.
- Don't store `const BlockRegistry&` — callers pass `BlockRegistry::base()`
  temporaries; hold it by value.
- **xxHash/lz4 link order matters**: lz4's vendored source ships its own
  old `lib/xxhash.h` with no `XXH3_128bits`. `VB_WITH_COMPRESSION` links
  `xxHash::xxhash` **before** `LZ4::lz4` on purpose so the real header
  wins `-I` search order — don't reorder this.
- `std::vector<std::byte>` can't be built directly from
  `std::istreambuf_iterator<char>` — use the `read_whole_file()` helper in
  `assetsync/cache.cpp` instead of re-deriving it.
- **Clang: a nested class's `Foo() = default;` can't be used as a default
  argument value (`= {}`) on a member function of the *enclosing* class
  declared before that enclosing class is complete** — Clang errors
  wanting the nested class's field default-member-initializers "within
  definition of enclosing class" to compute the defaulted ctor's implicit
  exception spec, which isn't available yet at that point. Hit this in
  `inc/vb/world/lighting.hpp`'s `LightEngine::Neighbours() = default;`
  used as `relight_chunk(Chunk&, const Neighbours& = {})`'s default arg.
  Fix: give the nested ctor a user-provided empty body (`Neighbours() {}`)
  instead of `= default` — a user-provided ctor's noexcept-ness doesn't
  need the deferred computation, so no reordering/out-of-line-definition
  dance is required.

---

## Detail files

Every file below is full-detail material moved out of this core. Newest
work is summarized above under "Current status"; everything before that is
here, organized as it was originally written (mostly chronological,
newest-first within each file):

- **`state/changelog-recent.md`** — full writeups for the block-self-heal
  follow-up, Phase 6.18 (discrete punch combat), Phase 6.17 (decoupled
  block breaking + `MovementBindings`), Phase 1.3 networking polish
  (two-process smoke test, per-IP connection cap, hostname resolution),
  Phase 6.15 (kitchen-sink example pack), Phase 6.14 (Lua-driven worldgen/
  FastNoise2), Phase 6.13 (read-only server config visibility) — these
  phases were never logged into the older §8 structure below and are not
  duplicated in the next two files.
- **`state/changelog-part1.md`** — Phase 6.1/6.4/6.5/6.6/6.7/6.8/6.9/6.11/
  6.16 landed-feature writeups; the 2026-09-15 investigations (NVIDIA
  driver VAO/VBO heap corruption, FPS dip during chunk streaming,
  cross-chunk sky-light band, near-black flash on predicted breaks, chunk
  meshing moved off the main thread, GNS send-buffer overflow root cause);
  the 2026-09-10/11 Phase 0-4 bootstrap entries (config, transport,
  handshake, session layer, physics/netcode, worldgen, entity visuals,
  block editing, Lua VM); "Gotchas learned this pass".
- **`state/changelog-part2.md`** — Phase 5.1 `content/base` pack + the gap
  it found (neither binary loaded a pack at all before this); Phase 5.2-5.5
  (main menu, chat, player list/join-leave, day/night, death/respawn, sfx
  docs, real inventory sync + hotbar, hold-to-break progress, dropped-item
  entity, crafting recipes, `--singleplayer` running the real content pack,
  documentation pass); the Cellulose meshing spike and revert; librg wired
  in as the interest backend and flipped to default; Phase 6 design-only
  passes (entity classes, UI immediate-mode, keybind channel, `vb.db`,
  block-damage breaking, worldgen biome-selection redesign).
- **`state/gotchas.md`** — long-form compiler/toolchain traps referenced
  from §4 above (the `std::erase` MSVC-STL bug, the MSVC ternary
  ambiguity, the two independent asset-sync hash-mismatch bugs, the
  stale-comment lesson, the clang-format-untrustworthy-locally note).

Anything **not** listed above and not inline in this file no longer exists
in `STATE.md`'s history — if you're looking for something and can't find
it here, check `git log -- STATE.md` for when it might have been removed,
or `REMAINING_TASKS.md`/`ARCHITECTURE_SPEC.md` for design-level context.
