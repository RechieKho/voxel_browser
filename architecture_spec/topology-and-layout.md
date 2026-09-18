# Process Topology & Repository Layout

> Full detail for this topic; linked from `ARCHITECTURE_SPEC.md`. Ground truth — do not duplicate here.

## 3. Process Topology

```
                 ┌─────────────────────────────────────────────┐
                 │                  SERVER                      │
                 │  (authoritative, headless)                   │
                 │                                             │
   Lua content   │  ┌───────────┐   ┌──────────────┐            │
   pack (disk) ─▶│  │ Lua VM +  │   │  World Gen   │            │
                 │  │ bindings  │   │ (FastNoise2) │            │
                 │  └─────┬─────┘   └──────┬───────┘            │
                 │        │                │                    │
                 │  ┌─────▼────────────────▼──────┐             │
                 │  │  Game State (EnTT ECS)      │             │
                 │  │  + Voxel World (chunks)     │             │
                 │  └─────────────┬──────────────┘              │
                 │                │                             │
                 │  ┌─────────────▼──────────────┐              │
                 │  │ Replication (librg)        │              │
                 │  │ Asset Sync service         │              │
                 │  │ Transport (GNS)            │              │
                 │  └─────────────┬──────────────┘              │
                 └────────────────┼────────────────────────────-┘
                                  │  UDP (reliable + unreliable channels)
                 ┌────────────────┼────────────────────────────-┐
                 │                │           CLIENT ("browser") │
                 │  ┌─────────────▼──────────────┐               │
                 │  │ Transport (GNS)            │               │
                 │  │ Asset cache (hash-indexed) │               │
                 │  │ Replication receiver       │               │
                 │  └─────────────┬──────────────┘               │
                 │                │                              │
                 │  ┌─────────────▼──────────────┐               │
                 │  │ Client World (chunk store) │               │
                 │  │ Entity view + interpolation│               │
                 │  │ Local player prediction    │               │
                 │  └─────────────┬──────────────┘               │
                 │                │                              │
                 │  ┌─────────────▼──────────────┐  ┌──────────┐ │
                 │  │ Hand-rolled mesher + render│  │ Sandboxed│ │
                 │  │ raygui HUD / menus         │◀─│ Lua VM   │ │
                 │  │ Input capture              │  │ (UI only)│ │
                 │  └────────────────────────────┘  └──────────┘ │
                 └─────────────────────────────────────────────-─┘
```

The same binary may host an **integrated server** for singleplayer: the client
spawns an in-process server on `localhost` and connects to it through the normal
socket path. There is no separate singleplayer code path.

---
## 4. Repository & Module Layout (target)

```
voxel_browser/
├── CMakeLists.txt              # top-level: options, dependency fetch, add_subdirectory
├── PROJECT_NAME
├── cmake/                      # helper modules (Dependencies.cmake, Warnings.cmake)
├── inc/                        # public headers, mirrors src/ tree
│   └── vb/
│       ├── core/               # math, ids, result types, logging, config
│       ├── world/              # block registry, chunk, world, region io
│       ├── worldgen/           # noise pipeline, biome interface
│       ├── ecs/                # component definitions, system runner
│       ├── net/                # transport, channels, message codec, snapshots
│       ├── replication/        # librg glue, interest management
│       ├── assetsync/          # manifest, hashing, transfer state machine
│       ├── script/             # Lua VM wrapper, binding registration, event bus
│       ├── protocol/           # generated/handwritten wire structs + versions
│       └── render/             # (client) mesher glue, camera, interpolation
├── src/
│   ├── core/  world/  worldgen/  ecs/  net/  replication/  assetsync/  script/
│   ├── server/                 # server executable: main.cpp, tick loop, CLI
│   └── client/                 # client executable: main.cpp, render loop, UI, input
├── content/
│   └── base/                   # the shipped minimal content pack (Lua + textures)
│       ├── pack.toml           # pack manifest: name, version, entry script
│       ├── init.lua
│       ├── blocks/  entities/  ui/
│       └── textures/
├── tests/                      # unit + integration tests (Catch2 or doctest)
│   ├── unit/
│   └── integration/            # headless client<->server harness
└── docs/
    ├── ARCHITECTURE_SPEC.md    # this file (or repo root)
    ├── lua-api.md              # generated + hand-written Lua API reference
    └── protocol.md             # wire format reference
```

### CMake targets

| Target        | Type        | Links                                                        |
| ------------- | ----------- | ----------------------------------------------------------- |
| `vb_core`     | STATIC lib  | fastnoise2, entt, lua, gamenetworkingsockets, librg, xxhash |
| `vb_render`   | STATIC lib  | `vb_core`, raylib                                            |
| `voxel_browser_server` | EXE | `vb_core`                                                    |
| `voxel_browser` (client) | EXE | `vb_core`, `vb_render`, raygui                            |
| `vb_tests`    | EXE         | `vb_core`, test framework                                    |

Dependencies are pulled via `FetchContent` (pinned tags) with `find_package`
fallback, matching the existing raylib pattern in `CMakeLists.txt`.

---
