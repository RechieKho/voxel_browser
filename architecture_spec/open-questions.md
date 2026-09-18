# Open Questions — Full Resolution History

> Full detail for this topic; linked from `ARCHITECTURE_SPEC.md`. Ground truth — do not duplicate here.

## 19. Open Questions

1. **Binding layer**: raw Lua C API vs. `sol2`. **Resolved (2026-09-10): sol2**
   (v3.3.0), for ergonomics; compile-time cost accepted.
2. **Cellulose API fit**: **Resolved (2026-09-11), then reversed
   (2026-09-16), and the dependency fully removed (2026-09-17) — hand-rolled
   meshing is the permanent backend.** Cellulose (a header-only greedy
   mesher) was spiked behind a build flag: it emitted vertex data, not GPU
   buffers, via a reusable `greedy_mesh(vector<
   MeshSample>, size, block_scale, ao, weld) -> ChunkMesh` entry point, fed
   from a flat grid filled from `ClientChunkStore`. It was wired in and
   passed the full test suite, but crashed in real play: greedy-merged
   output has far more volatile per-edit vertex/index counts than the
   hand-rolled per-face mesher (merging means one edit near a merge boundary
   can swing a whole face's quad count a lot), which defeated
   `chunk_renderer.cpp`'s GPU-buffer-headroom mitigation for the NVIDIA
   VAO/VBO-churn heap-corruption bug (see `STATE.md` §1/§8) and reproduced
   it far more often. Reverted, and Cellulose's build-flag/FetchContent
   scaffolding was later removed outright rather than kept dormant —
   `vb/world/
   chunk_mesher` + `chunk_mesh_snapshot`'s hand-rolled per-face mesher
   (fixed 4 vertices/6 indices per visible face, so an edit's effect on GPU
   buffer size stays small and local) is the permanent choice, with no
   planned swap-in. Full story: `STATE.md` §8's 15th entry (2026-09-16) and
   the 2026-09-17 removal entry.
3. **librg version / API**: **Resolved (2026-09-10): librg v7.4.0** (single
   self-contained header, zpl bundled). Interest is chunk-radius based (cells
   independent of voxel chunks). **Use librg for interest culling + its
   create/update/remove framing; keep our own per-entity payload codec and
   envelope/lane routing.** Not yet wired — Phase 1 ships a hand-rolled
   `InterestGrid` with the same diff semantics behind a narrow interface. Full
   write-up in `docs/replication.md`.
4. **Chunk compression**: LZ4 vs. zstd vs. palette-only. Start LZ4, measure.
5. **Persistence**: region file format for world save — deferred past first
   playable, but the chunk store should not assume in-memory-forever.
   **Future direction noted (2026-09-17, not yet planned into a phase):**
   lean toward an LMDB-backed store keyed by `ChunkCoord`, reusing the
   existing `vb/world/chunk_codec` palette+RLE serialization (the same
   format `S2C_ChunkAdd` already uses) as the on-disk chunk payload, with
   LZ4 (§19 Q4) as the compression layer on top. LMDB avoids reinventing
   sector allocation and crash-safety that a hand-rolled Anvil-style region
   file would require; a per-chunk-file or Anvil-style layout remains the
   fallback if a zero-extra-dependency approach is preferred later.
6. **Account/auth**: `auth_mode = none | token` — token verification service is
   out of scope for v0 but the handshake reserves the field. **Direction set
   (2026-09-17, not yet implemented):** the engine will not own an auth
   concept at all — `vb.db` (§10.6) gives packs a generic per-key store, and
   any login flow (recognizing a returning player, credential checks) is
   entirely pack-implemented on top of it plus the UI/input APIs. This
   `auth_mode` field stays reserved for a future *transport-level* token
   check, which is a different, lower-level concern than pack-level identity.
   **Future direction noted (2026-09-17, not yet planned into a phase):** a
   baked-in OpenID Connect client using the loopback-redirect flow (RFC 8252)
   -- the client opens the system browser to the identity provider and spins
   up a local HTTP listener to catch the redirect back, so no embedded
   browser or client secret is needed. This would land as a new `auth_mode =
   oidc` value at the transport/handshake level, alongside (not replacing)
   the pack-level `vb.db`-based identity approach above.
7. **Entity visual presentation**: 3D blocky models vs. 2D sprites.
   **Resolved (2026-09-11): Don't Starve-style Y-axis-billboarded sprites**, not
   blocky models — full design in §11.3. Key parameters locked in: raylib
   `DrawBillboardPro` with a fixed world-up (verified against `rmodels.c`, no
   custom quad math); default 8 discrete facings authored as 5 unique poses +
   engine-side mirroring; animation state resolved client-side from
   `EntityRecord.vel` + `flags` bits (`on_ground` existing, `dead`/`hurt_pulse`/
   `acting` new — not yet wired on the wire, see `REMAINING_TASKS.md` 3.5).
   Deferred to implementation time: exact clip-name base set beyond the priority
   list itself, and whether a blob-shadow decal ships alongside v1 or later.
