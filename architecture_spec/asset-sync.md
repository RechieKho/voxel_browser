# Asset Sync Protocol — Full Reference

> Full detail for this topic; linked from `ARCHITECTURE_SPEC.md`. Ground truth — do not duplicate here.

## 9. Asset Sync Protocol

Goal: after connecting, the client has byte-identical copies of every file the
server's content pack needs, without re-downloading what it already cached from a
previous session or another server.

### 9.1 Manifest

Server, at startup, walks `content/<pack>/` and builds:

```
AssetEntry {
    string   path;      // pack-relative, forward-slashed, normalized
    AssetHash hash;     // xxHash3-128 of contents
    uint64   size;
    AssetKind kind;     // SCRIPT | TEXTURE | MODEL | UI | SOUND | DATA
}
```

The manifest is itself hashed (`manifest_hash`) so a client that reconnects can
skip the whole exchange with one comparison.

### 9.2 Client cache

Content-addressed store at `~/.cache/voxel_browser/assets/<hh>/<hash>` (OS-
appropriate dir). A small SQLite or flat index maps `hash → {size, last_used}`
for LRU eviction. The cache is shared across all servers — identical files are
downloaded once, ever.

### 9.3 Transfer

- Client sends `C2S_AssetRequest` with the list of hashes it is missing.
- Server streams `S2C_AssetData{ hash, seq, total_chunks, bytes }` on lane 3,
  round-robin across requested files, chunk size ~48 KB, with a bytes-in-flight
  window.
- Client verifies each completed file's hash before committing to cache; a
  mismatch aborts the connection with a protocol error.
- Progress is surfaced to the UI (`S2C` count + bytes vs. manifest totals) so the
  connect screen can show a progress bar.

### 9.4 Security

- Server refuses to include paths with `..`, absolute paths, or symlinks
  escaping the pack root.
- Client writes only into the content-addressed cache, never to arbitrary
  manifest paths; the `path` field is metadata used to build the virtual pack
  filesystem in memory, not a write target.
- Per-file and total size caps (configurable) reject hostile servers.
- Script assets are executed only inside the sandbox of §10.

