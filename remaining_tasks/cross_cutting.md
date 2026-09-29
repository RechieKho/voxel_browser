## Cross-Cutting / Continuous

> Full history for this section; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.

- [ ] **Real, unfixed engine bug found 2026-09-28 while building
      `content/base/entities/zombie.lua` (Phase 6's "mob damage" item):
      a `script::PlayerHandle` stored across calls and read back from
      inside `vb.on("tick", ...)` or a script entity's own `on_tick`
      silently returns corrupted data** — confirmed with ASan (Windows
      MSVC + `clang_rt.asan_dynamic`, `-DVB_ENABLE_ASAN=ON`), not just
      inferred from a crash: `PlayerHandle::get_name()`/`get_pos()` read a
      garbage `rt` (`PackRuntime::Impl*`), a `nullptr`/garbage `session`,
      and a garbage `net_id` (observed `0xFFFFFFFE`) when called this way,
      instead of the real stored values. **Repro (minimal, reproduced
      multiple times with a guaranteed-fresh rebuild each time — see
      `content_pack_test.cpp`'s zombie test's own comment for the summary):**
      `vb.on("chat", function(player, text) stashed = player end)` then,
      from `vb.on("tick", function(dt) print(stashed:get_name()) end)`,
      the printed name is wrong/empty on every subsequent tick, forever —
      not a one-time race. **What's proven NOT the cause:** storage
      location (module-level global, entity-table field, and a bare Lua
      upvalue closure all reproduce it identically); it is not a same-tick
      timing race (reproduces across many ticks after the store, not just
      the first). **What IS the distinguishing factor:** reading a
      *stored* `PlayerHandle`'s methods from within the `dispatch_tick`
      call path (`PackRuntime::Impl::dispatch_tick` → `fire("tick", ...)`
      *or* → `dispatch_entity_tick` → a kind's `on_tick`) is broken;
      reading a **freshly passed-in** `PlayerHandle` argument from within
      *any* handler (`chat`, `player_input`, `player_join`, `player_death`,
      ...) — even storing *that* one and reading it back from a **later,
      separate `chat`/`player_input`-style dispatch** — works perfectly
      (also confirmed with ASan, `rt`/`session`/`net_id` all correct).
      `run_veto()` and `fire()` (`src/script/pack_runtime.cpp`) are
      structurally identical templates, so the bug isn't in either of
      those directly — it's somewhere in how `dispatch_tick` specifically
      reaches Lua (`ServerSession::on_script_tick_` → `PackRuntime::
      attach_session`'s hook lambda → `PackRuntime::dispatch_tick` →
      `Impl::dispatch_tick`), not yet root-caused further. **Worked
      around, not fixed:** `zombie.lua`'s own chase/attack logic is driven
      from `vb.on("player_input", ...)` instead (a fresh `PlayerHandle`
      every real `InputCmd`, used immediately, never stored) — see Phase
      6's own "mob damage" entry for the full design. **Impact if unfixed:**
      any *future* pack that tries the natural "capture a player in a
      hook, act on them later from `on_tick`" pattern will hit this
      silently (no crash in a non-ASan build — it just reads wrong data,
      e.g. `player:damage()` becomes a no-op since `session` reads as
      `nullptr`) — a real correctness trap for content authors, not just
      an inconvenience. Whoever picks this up next: start from the exact
      repro above (it's a ~15-line pack script), build `build-net-lua`
      with `-DVB_ENABLE_ASAN=ON` (needs `clang_rt.asan_dynamic-x86_64.dll`
      on `PATH`, found under the MSVC toolchain's `bin\Hostx64\x64`), and
      get a fresh ASan stack trace before theorizing — several plausible-
      sounding theories (GC collecting a live-referenced value, a `fire()`
      vs `run_veto()` implementation difference, a moved/stale
      `PackRuntime` pointer) were all checked and ruled out empirically
      during this investigation.
- [x] Sanitizer (ASan/UBSan) debug CI job; TSan job for the threaded
      subsystems — landed 2026-09-28. `cmake/Sanitizers.cmake`'s
      `vb_sanitizers` INTERFACE target (and the `VB_ENABLE_ASAN`/
      `VB_ENABLE_UBSAN`/`VB_ENABLE_TSAN` options) already existed and were
      already linked into every first-party target, but no workflow ever
      turned them on — this closes that gap on the Linux matrix only
      (`.github/workflows/build_linux.yml`): two new `build_type` entries,
      `asan` (`-DVB_ENABLE_ASAN=ON -DVB_ENABLE_UBSAN=ON`, combinable per
      `Sanitizers.cmake`'s own mutual-exclusion check) and `tsan`
      (`-DVB_ENABLE_TSAN=ON`), both `RelWithDebInfo` so symbols/line info
      survive in sanitizer output. Both run the exact same `ctest
      --output-on-failure` the existing release/debug jobs already run — no
      new test list, the point is running *existing* tests under
      instrumentation. Binary staging/artifact upload is skipped for these
      two (`if: matrix.build_type == 'release' || matrix.build_type ==
      'debug'`) since a sanitizer build isn't a real distributable. Windows
      (MSVC) and macOS were deliberately left alone: `Sanitizers.cmake`
      itself already documents MSVC has no UBSan/TSan support at all (ASan
      only, and even that needed the `_DISABLE_*_ANNOTATION` workaround
      already in that file for third-party static-lib linking), and macOS CI
      doesn't even build `VB_WITH_NET` yet (Phase 1's own still-open item)
      — Linux is the one platform where all three sanitizers and the
      full net-enabled build are simultaneously supported. **Not verified by
      an actual GitHub Actions run** — this agent environment has no way to
      trigger/observe a real Actions run; the change is a straightforward
      extension of the existing, already-green release/debug matrix entries
      (same install/configure/build/test steps, just extra `-DVB_ENABLE_*`
      flags plumbed through `matrix.sanitizer_cmake_flags`), but the first
      real run may still surface a genuine sanitizer finding (a real race or
      UB) or an environment quirk (e.g. a third-party FetchContent'd lib not
      tolerating being linked against instrumented code) that a local
      Windows-only agent session can't preempt — same category of caveat as
      every GUI-adjacent item in this file, just for CI infrastructure
      instead of rendering.
- [x] Soak test target (N simulated clients, random walk + edits) — landed
      2026-09-28 as `tests/unit/soak_test.cpp`, folded into the normal
      `vb_tests` run rather than a separate nightly/pre-release job. 4
      simulated `ClientSession`s over `LoopbackNetwork` (real `ServerSession`
      + `WorldReplicator` + `World`, `WorldGenWorkerPool::kSynchronous`)
      random-walk (fixed-seed `std::mt19937`, reproducible) and
      break/place-edit for 150 ticks, then all disconnect. Asserts three
      things ARCHITECTURE_SPEC.md's Testing Strategy calls out: every client
      stays joined and no pending connection is left stuck
      (`player_count()`/`pending_count()`), `pending_edit_count()`/
      `unacked_input_count()` stay bounded rather than growing across the
      whole run (a real queue-growth bug — an ack that stops being sent, an
      edit result that never arrives — would blow well past the generous
      bound), and `World::loaded_coords()` stays bounded rather than growing
      roughly linearly with tick count (chunk unload actually keeps working
      under sustained random-walk churn). A final round confirms
      `player_count()`/`pending_count()` both return to 0 after every client
      disconnects — no leaked `Conn` entry. Real ASan/LSan leak detection
      comes for free from this simply being part of `vb_tests`, which the
      Linux CI matrix already runs under `-DVB_ENABLE_ASAN` (this same day's
      earlier CI pass) — no separate sanitizer wiring needed for that half.
      **Deliberately kept small** (4 clients, close together, 150 ticks):
      real fBm terrain generation dominates this test's cost in an
      unoptimized Debug build far more than the sim logic actually being
      soaked — an earlier draft (6 clients spread far enough apart to force
      a distinct set of chunk columns each, 400 ticks) measured ~55s of real
      CPU time standalone; this version measured ~14s against a ~70s
      baseline for the other 412 tests combined, a proportionate addition
      rather than a suite-doubling one. Verified: full `vb_tests` 413/413
      green, clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server`.
- [x] Perf budget checks: chunk mesh time, snapshot size — landed 2026-09-28
      as `tests/unit/perf_budget_test.cpp`, two budget-gate `TEST_CASE`s
      folded into the normal `vb_tests` run (no historical tracking, no
      graphs, nothing scheduled separately — matching this item's own
      "simple benchmark harness" framing). Chunk mesh time: generates a
      real chunk at the generator's own reported surface height (a fixed
      chunk-y guess turned out unreliable — an earlier draft hit an entirely
      open-sky chunk for this seed/column and failed its own "not empty"
      sanity check), meshes it 3x via `mesh_chunk()` and keeps the fastest
      run, asserts `< 100ms` (measured ~21ms in this environment's
      unoptimized Debug build — real headroom, not a hair's-width pass).
      Snapshot size: encodes a 50-entity `S2CEntitySnapshot` (deliberately
      busier than any single player's interest radius would realistically
      ever surface at once) and asserts the encoded size stays under a
      per-entity byte budget (measured ~52 bytes/entity; budget is 90
      bytes/entity + a fixed 256-byte allowance) — would catch a real
      regression such as `visual_override` starting to ride on every
      record instead of only `entered` ones. **Frame time was deliberately
      left out**, not attempted as a stand-in: real rasterization/GPU cost
      needs a live GL context to mean anything, and this agent environment
      has no GUI. Verified: full `vb_tests` 415/415 green (2 new cases),
      clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server`.
