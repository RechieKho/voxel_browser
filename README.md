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

Voxel Browser leverages a carefully curated stack of modern, high-performance C/C++ libraries:

| Component             | Technology / Library                                                            | Description                                                                      |
| --------------------- | ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| **Voxel Engine**      | [Cellulose](https://github.com/RechieKho/cellulose)                             | Core voxel meshing, rendering, and block management.                             |
| **Networking Core**   | [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) | Valve's robust transport layer for reliable/unreliable UDP connection handling.  |
| **Network Topology**  | [librg](https://github.com/zpl-c/librg)                                         | Entity replication and chunk-based spatial network filtering.                    |
| **Entity Management** | [EnTT](https://github.com/skypjack/entt)                                        | Header-only, lightning-fast ECS for managing players, mobs, and dynamic objects. |
| **World Generation**  | [FastNoise2](https://github.com/Auburn/FastNoise2)                              | High-performance SIMD noise generation for terrain and biomes.                   |
| **User Interface**    | [raygui](https://github.com/raysan5/raygui)                                     | Immediate-mode GUI for menus, inventories, and HUDs.                             |
| **Scripting**         | [Lua](https://www.lua.org/)                                                     | Embedded scripting language for server-side logic and modding.                   |

---

## 🏗️ Architecture & Design Strategy

### The Server (Authoritative)

The server is the source of truth. It is responsible for:

1. **World Generation:** Utilizing `FastNoise` to generate chunk data based on server-defined seeds and Lua-defined biome rules.
2. **Game State & ECS:** Maintaining the master state of all entities using `EnTT`.
3. **Modding & Asset Management:** Loading Lua scripts and assets from the host's project directory.
4. **Network Replication:** Pushing chunk data, entity updates (via `librg`), and required assets to connected clients.

### The Client (The "Browser")

The client is a thin, rendering-focused application. It is responsible for:

1. **Asset Synchronization:** Shaking hands with the server (via `GameNetworkingSockets`) and downloading missing textures, models, and UI definitions on connect.
2. **Rendering:** Generating chunk meshes via `Cellulose` and rendering the game world.
3. **Input Handling:** Capturing user keyboard/mouse input and passing immediate-mode UI interactions (`raygui`) back to the server.
4. **Interpolation:** Smoothly rendering entity movements between server tick updates.

---

## 🚀 Implementation Strategy

The development of Voxel Browser will follow a phased approach to ensure stability and modularity.

### Phase 1: Core Foundation & Networking

- Initialize the CMake project with all submodules/dependencies.
- Setup **GameNetworkingSockets** to establish a reliable client-server handshake.
- Integrate **librg** to handle basic spatial tracking (e.g., connecting two dummy clients and verifying they can "see" each other in network space).
- Initialize the **Cellulose** window and basic rendering loop on the client.

### Phase 2: World State & Terrain Generation

- Implement chunk data structures on the server.
- Integrate **FastNoise** to generate basic heightmaps (dirt, stone, air).
- Serialize chunk data and transmit it to the client via reliable network packets.
- Feed received chunk data into **Cellulose** for mesh generation and rendering.

### Phase 3: Entity Component System & Physics

- Integrate **EnTT** on the server. Create basic components: `Position`, `Velocity`, `Collider`, `PlayerInput`.
- Implement basic AABB (Axis-Aligned Bounding Box) voxel collision detection on the server.
- Map **EnTT** entities to **librg** network entities to synchronize player movement to clients.

### Phase 4: The "Browser" Engine (Scripting & Assets)

- Embed the **Lua** runtime into the server.
- Create C++ bindings for Lua so scripts can register new block types, listen to player click events, and spawn entities.
- Implement the **Asset Sync Protocol**:
- Server hashes all project files (textures, scripts).
- Client sends a list of locally cached hashes on connect.
- Server pushes missing files over the network.

- Bind **raygui** to allow the server to define basic client-side menus via Lua (e.g., inventory screens).

### Phase 5: Minimum Playable Base

- Finalize the minimal content pack (Dirt, Grass, Wood, Leaves, Stone, Sand).
- Create a basic main menu using `raygui` (Server IP input, Connect button).
- Implement block breaking/placing logic over the network.
- Write comprehensive documentation for the Lua API.

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

