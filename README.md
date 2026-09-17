# 🌐 Voxel Browser

> A minimal, multiplayer-first voxel game base built for infinite extensibility.

**Voxel Browser** is a modern, lightweight voxel engine designed from the ground up for multiplayer and modding. Inspired by projects like Luanti (formerly Minetest), it provides a robust client-server architecture with minimal hardcoded content (just the basics: dirt, wood, stone). The rest is up to the community.

True to its name, the client acts as a "browser." When a player connects to a server, all custom Lua scripts, textures, and assets are automatically synchronized. The server holds the game logic; the client renders the world.

---

## ✨ Key Features

- **Multiplayer by Default:** Built on an authoritative server model with robust, low-latency networking capabilities out of the box.
- **"Browser" Architecture:** Zero manual mod installation for players. Connect to a server, and the client automatically downloads and caches the required Lua scripts and assets.
- **Infinite Extensibility:** The base game is intentionally barebones. Everything from block types to complex mobs and biomes is defined through an intuitive Lua API.
- **Modern Tooling:** Built with modern C++ and actively maintained, high-performance third-party libraries.
- **Data-Driven Design:** Entity management is handled via a blazing-fast Entity Component System (ECS).

---

## 🛠️ Tech Stack

Voxel Browser leverages a carefully curated stack of modern, high-performance C/C++ libraries. A few started as the intended long-term backend for a subsystem but are still gated behind a `VB_WITH_*` build flag (default `OFF`) while a smaller hand-rolled version does the same job today — the column below says which is actually driving the game right now.

| Component             | Technology / Library                                                            | Status today                                                                     |
| --------------------- | ------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| **Voxel Meshing**     | Hand-rolled face-culled mesher (`vb::world::chunk_mesher`)                       | **Active.** [Cellulose](https://github.com/RechieKho/cellulose) is the intended `VB_WITH_MESHING` backend, not yet wired in. |
| **Networking Core**   | [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) | **Active** (`VB_WITH_NET`) — real UDP handshake, chunk/entity/chat replication.    |
| **Entity Replication**| `vb::replication::InterestGrid`                                                 | **Active.** [librg](https://github.com/zpl-c/librg) is now wired in as the `VB_WITH_REPLICATION` interest-culling backend (default OFF; hand-rolled linear scan otherwise), behind the same interface either way. |
| **Entity Management** | `ServerSession` drives players through an [EnTT](https://github.com/skypjack/entt) registry | **Active.** Each playing connection is a real `entt::registry` entity (`Position`/`Velocity`/`Rotation`/`Collider`/`PlayerInput`/`Health`/`PlayerTag`/`NetReplicated`), the actual source of truth. No generic system runner iterates it yet — see `REMAINING_TASKS.md` Phase 3.1. |
| **World Generation**  | Hand-rolled deterministic noise (`vb::core::noise`)                             | **Active** for the base heightmap pipeline. [FastNoise2](https://github.com/Auburn/FastNoise2) is the intended `VB_WITH_WORLDGEN` backend for a future Lua-driven pipeline. |
| **User Interface**    | [raygui](https://github.com/raysan5/raygui)                                     | **Active** — main menu, HUD, and Lua-defined pack UI screens.                      |
| **Scripting**         | [Lua](https://www.lua.org/) 5.4 + [sol2](https://github.com/ThePhD/sol2)         | **Active** (`VB_WITH_LUA`) — server + client-UI sandboxed VMs, the full `content/base` pack. |
| **Rendering / Window**| [raylib](https://github.com/raysan5/raylib)                                     | **Active** — window, GL context, 3D draw calls, billboarded entities.             |

See `STATE.md` for the specifics of each still-gated dependency (API shape,
pin, what's blocking the swap).

---

## 🏗️ Architecture & Design Strategy

### The Server (Authoritative)

The server is the source of truth. It is responsible for:

1. **World Generation:** Deterministic heightmap terrain today (`vb::worldgen`), driven by a server-defined seed; a Lua-driven pipeline with real biomes/carvers/decoration is planned (`REMAINING_TASKS.md` Phase 4.2).
2. **Game State:** Player movement, physics, and block edits are simulated by `ServerSession` through a per-connection EnTT registry entity; a generic system runner for non-player entities is planned but not required for anything shipped so far.
3. **Modding & Asset Management:** Loading Lua scripts (`content/base` by default) and hashing/serving assets from the host's project directory over the Asset Sync protocol.
4. **Network Replication:** Pushing chunk data, entity snapshots, chat, and required assets to connected clients over `GameNetworkingSockets`.

### The Client (The "Browser")

The client is a thin, rendering-focused application. It is responsible for:

1. **Asset Synchronization:** Shaking hands with the server and downloading missing scripts/assets on connect, cached content-addressed on disk.
2. **Rendering:** Meshing and rendering streamed chunks, plus billboarded remote entities and dropped items.
3. **Input Handling:** Capturing keyboard/mouse input (movement, block break/place, chat, crafting commands) and driving Lua-defined `raygui` UI screens the server pushes.
4. **Prediction & Interpolation:** Predicting local movement against the server's authority and smoothly interpolating remote entities between snapshots.

---

## 📌 Project Status

Voxel Browser has gone through its originally-planned Phase 0–5 and is a
playable (if visually minimal) multiplayer sandbox today: connect to a
server, walk around generated terrain, mine and place blocks, chat, see
other players, and craft (`content/base/crafting.lua`'s wood → planks →
sticks example). `REMAINING_TASKS.md` is the authoritative, continuously
updated backlog — the phase list below is a high-level summary, not a
substitute for it.

| Phase                                          | Status |
| ----------------------------------------------- | ------ |
| 0 — Project restructure & build system          | ✅ Done |
| 1 — Core foundation & networking (handshake, transport, interest/replication bootstrap) | ✅ Done |
| 2 — World state & terrain generation (chunks, lighting, meshing, streaming) | ✅ Done |
| 3 — ECS & physics (movement, prediction, remote entity billboards) | ✅ Substantially done — EnTT registry now backs player state (3.1); no generic system runner yet, not required so far |
| 4 — The "Browser" engine (Lua scripting, Asset Sync, client UI VM) | ✅ Done (mechanism); a real Lua-driven worldgen pipeline is the main open item |
| 5 — Minimum playable base (content pack, block editing, main menu, chat/day-night/respawn, crafting) | ✅ Substantially done — see `REMAINING_TASKS.md` 5.5 for remaining docs/polish |

Known gaps worth knowing about before diving in (full detail in
`REMAINING_TASKS.md` and `STATE.md`): no world persistence (everything is
regenerated from the seed on restart), held items and placeable blocks still
share one id space, and macOS CI doesn't build the real networking backend
yet (works locally, just not wired into that platform's workflow).

---

## 💻 Getting Started

_(Phase 0 complete: the build system, module split, and runnable client/server
skeletons exist. Networking, world, and scripting land in later phases — see
`REMAINING_TASKS.md`.)_

### Prerequisites

- CMake **3.25+**
- A C++20 compiler (GCC 11+, Clang 14+, MSVC 19.3+)
- Git
- Linux only, for the client: X11 + OpenGL dev headers
  (`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`)

Dependencies are fetched at configure time via CMake `FetchContent` (pinned
tags) — there are **no git submodules**. A network connection is needed for the
first configure.

### Building from Source

```bash
git clone https://github.com/RechieKho/voxel_browser.git
cd voxel_browser

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Binaries land in `build/`:

| Binary                   | What it is                                        |
| ------------------------ | ------------------------------------------------ |
| `voxel_browser`          | the client ("the browser")                        |
| `voxel_browser_server`   | the authoritative, headless server                |

```bash
./build/voxel_browser_server --config server.toml   # start a server
./build/voxel_browser --singleplayer --name Me       # in-process server + join
./build/voxel_browser --headless --frames 1          # no-GPU smoke run
```

Copy `server.toml.example` / `client.toml.example` and edit; every key is
optional and CLI flags override the file. Real remote connections
(`voxel_browser --server <addr> --port <n>` against a running
`voxel_browser_server`) need `-DVB_WITH_NET=ON`, which links
GameNetworkingSockets — see the next section for its one extra system
dependency (protobuf).

`voxel_browser --singleplayer` runs a real in-process server + client over a
loopback transport, so it works with the plain build above — no `VB_WITH_NET`
needed to try it. To also get the `content/base` pack's chat/crafting/
inventory scripting (rather than just the hardcoded terrain/physics/menu),
configure with `-DVB_WITH_LUA=ON` too:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVB_WITH_LUA=ON
cmake --build build
./build/voxel_browser --singleplayer --name Me
```

Once in, WASD + mouse to move, left-click (held) to break a block,
right-click to place one, Enter to open the chat box — type `/craft
base:planks` after mining some wood to see the base pack's example crafting
system in action.

### Build options

| Option                  | Default | Effect                                              |
| ----------------------- | ------- | -------------------------------------------------- |
| `VB_BUILD_CLIENT`       | `ON`    | build `voxel_browser` + `vb_render` (needs raylib)  |
| `VB_BUILD_SERVER`       | `ON`    | build `voxel_browser_server`                        |
| `VB_BUILD_TESTS`        | `ON`    | build `vb_tests` and register CTest tests           |
| `VB_HEADLESS`           | `OFF`   | client build that never touches the GPU (CI/tests)  |
| `VB_WARNINGS_AS_ERRORS` | `OFF`   | `-Werror` / `/WX` (CI turns this on)                |
| `VB_ENABLE_ASAN` / `_UBSAN` / `_TSAN` | `OFF` | sanitizer builds                       |
| `VB_WITH_NET` / `_REPLICATION` / `_WORLDGEN` / `_COMPRESSION` / `_LUA` / `_MESHING` | `OFF` | pull in the heavy dependency owned by each later phase |

`VB_WITH_NET=ON` needs a real, installed protobuf (GameNetworkingSockets'
build requirement — FetchContent-ing protobuf's source doesn't work, see the
comment in `cmake/Dependencies.cmake`):

- **Windows:** [vcpkg](https://github.com/microsoft/vcpkg) —
  `vcpkg install protobuf:x64-windows`, then add
  `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` to the
  configure command. GitHub's `windows-latest` runners ship vcpkg pre-installed.
- **Linux:** `apt-get install protobuf-compiler libprotobuf-dev libssl-dev`
- **macOS:** `brew install protobuf openssl` (not yet wired into CI's universal
  build — see `STATE.md`)

