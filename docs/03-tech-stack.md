# 03 — Technology Stack

## The big decision: engine

| Option | 700+ FPS (1% lows)? | Hit-reg control | Team size needed | Verdict |
|---|---|---|---|---|
| **Custom lean engine (C++20)** | Yes: we control every microsecond | Total | Small, but senior | ✅ **Recommended** |
| Unreal Engine 5 | Only with very heavy engine modification (VALORANT did this on UE4 with a large engine team). The default frame overhead is far above a 1.4 ms budget. | Good, but fighting the engine's movement/anim/net stack | Large | ❌ Too heavy for this target |
| Unity (DOTS/Netcode for Entities) | Difficult: GC, render pipeline overhead, scripting layers | Moderate | Medium | ❌ |
| Godot 4 | Not designed for this; fine for a **throwaway feel prototype** | Moderate | Small | ⚠️ Prototype only |
| Rust + Bevy / custom Rust | Possible; young ecosystem. Vendor SDKs (EAC, Reflex, Steamworks, Anti-Lag 2) are C/C++ and need bindings | Total | Small, senior | ⚠️ Viable alternative if the team is Rust-native |
| Source 2 / s&box | Not licensable for a standalone commercial competitor in practice | — | — | ❌ |
| GPL id Tech derivatives (ioquake3, DarkPlaces) | Yes | Total | Small | ❌ GPL forces the whole game open source and makes anti-cheat harder |

**Why custom works here:** the scope is small and fixed. That means 10 players, static maps, baked lighting, about 14 weapons, hitscan, and a kinematic controller. Quake, GoldSrc, Source and CS:GO show the architecture. What makes engines big is everything we're explicitly *not* doing: open worlds, dynamic GI, streaming, and complex physics.

## Recommended stack

### Language and build
| Area | Choice | Notes |
|---|---|---|
| Language | **C++20** (a conservative subset: no exceptions in the hot path, no RTTI dependence, explicit allocators) | Vendor SDK compatibility and the talent pool |
| Build | **CMake + Ninja**, presets for `client`, `server`, `tools` | |
| Compilers | MSVC/clang-cl (Windows client), clang (Linux server) | Same `-ffp-contract=off`, **no fast-math** for simulation code |
| Dependencies | **vcpkg manifest mode** (pinned baseline) or CPM.cmake | Reproducible builds |
| Profiling | **Tracy** (BSD-3), in from day 1 | CPU, GPU, locks, allocations, frame marks |
| Tests | **doctest** or Catch2, plus custom golden-trace sim tests | |
| Crash reporting | Crashpad + Sentry (or Backtrace) | Symbol server per build |

### Client runtime
| Area | Choice | License | Notes |
|---|---|---|---|
| Platform/window | **SDL3** for windowing/gamepad/misc; **custom Win32 raw input path** (`GetRawInputBuffer`) for mice | zlib | Batch-read raw input so 8 kHz mice don't cost FPS; QPC-timestamp every delta |
| Graphics API | **Vulkan 1.3** primary (dynamic rendering, timeline semaphores); D3D11/12 back-end optional later | — | Vulkan also gives Linux/Steam Deck. CS2 ships DX11 + Vulkan. |
| RHI layer | Thin custom RHI, **or NVIDIA NVRHI** (MIT) to save time | MIT | Avoid heavyweight abstractions; the world is drawn with a handful of pipelines |
| Shaders | **HLSL → DXC → SPIR-V**, compiled **offline**; all pipelines created at load time with a pipeline cache | | **Zero runtime shader compilation = zero shader stutter** |
| Memory | VulkanMemoryAllocator (MIT); custom frame/linear allocators | | No per-frame heap allocations |
| Math | Custom SIMD math, or GLM for tools only | | |
| Latency | **NVIDIA Reflex SDK** + **AMD Anti-Lag 2 SDK** | free | Both are shipped by CS2 |
| UI (game) | **RmlUi** (MIT, HTML/CSS-like) with a small retained HUD | MIT | HUD drawn in 1–2 draw calls |
| UI (debug) | **Dear ImGui** (MIT) | MIT | Net graph, hitbox debug, console |
| Audio | **miniaudio** (public domain/MIT-0) + **Steam Audio** (Apache-2.0) for HRTF/occlusion | | Separate thread, fixed voice budget |
| Compression | **zstd** (assets), **LZ4** (demos) | BSD | |
| Asset format | glTF 2.0 → **cooked binary blobs** (mmap-able, zero parsing at load) | | |

### Simulation (shared client/server code)
| Area | Choice | Notes |
|---|---|---|
| Player movement | **Custom kinematic controller**: collide-and-slide traces against convex brushes/hulls | Deterministic and tick-quantized; the heart of "feel" |
| World collision | Custom **BVH over convex brushes + triangle soup for detail**, built offline by the map compiler | Same data on client and server |
| Hitboxes | Capsules/OBBs per bone, posed by a **deterministic gameplay skeleton** | See `04-netcode-and-hitreg.md` |
| Grenades | Custom fixed-tick integrator with deterministic bounce/friction | Repeatable lineups |
| Ragdolls / cosmetic physics | **Jolt Physics** (MIT), **client-only, cosmetic** | Never affects gameplay; time-sliced and capped |
| Entity model | Plain structs-of-arrays per entity type (players, grenades, projectiles, bomb) | An ECS framework is overkill for about 50 live entities |

### Networking
| Area | Choice | Notes |
|---|---|---|
| Transport | **Valve GameNetworkingSockets** (BSD-3): reliable + unreliable messages, encryption, fragmentation, bandwidth estimation, built-in lag/loss simulation | Battle-tested in CS and Dota 2. Or a custom UDP layer if we want full control. |
| Relay / DDoS | **Steam Datagram Relay** (when shipping on Steam), or a commercial relay | Hides server IPs and improves routing |
| Serialization | Custom bit-packer with quantization + delta compression against acked snapshots | Hand-tuned; no protobuf in the hot path |
| Network sim in dev | GNS fake-lag/loss + Linux `tc netem` in CI | |

### Server and backend
| Area | Choice | Notes |
|---|---|---|
| Dedicated game server | Same C++ codebase, headless Linux build, **1 match per process, pinned to a core** | 128 Hz tick, budget ≤ 2 ms per tick |
| Hosting | **Bare-metal, high-clock CPUs** in each region, via a game-server host (Edgegap, Gameye, i3D.net, Hathora) or self-managed **Agones on Kubernetes** | Avoid noisy-neighbour cloud VMs for tick stability |
| Platform | **Steamworks** (auth, distribution, friends, overlay, SDR) | Add Epic/others later via EOS |
| Backend services | **Go** (or Rust): matchmaker, party, ranked/Elo (Glicko-2 / TrueSkill-like), match history, reports | |
| Data | **PostgreSQL** (accounts, matches), **Redis** (queues, presence), object storage (demos) | |
| Messaging | NATS or Redis Streams for match lifecycle events | |
| Observability | Prometheus + Grafana (server tick-time histograms, hit-reg disagreement rate), Loki/OpenTelemetry | |

### Anti-cheat (defence in depth)
| Layer | Choice |
|---|---|
| Kernel-level client AC | **Easy Anti-Cheat** via Epic Online Services (free, self-serve, works with Steam) or BattlEye (commercial) |
| Server authority | Server owns movement, damage, economy and visibility. The client only sends inputs. |
| Anti-wallhack | **Server-side visibility culling**: enemies aren't sent to a client until they're potentially visible (PVS + coarse ray tests + smoke occlusion + a lead time for latency). Inaudible sounds aren't sent either. |
| Input sanity | Server validates command rate, view-angle deltas, time-stamps and rewind windows |
| Statistical detection | Offline aim-analysis models over demos (snap profiles, pre-aim through walls, reaction distributions) → review queue |
| Human review | Overwatch-style community/staff demo review |
| Trust | Account trust score (age, phone, purchases, report history) gates ranked |

### Tools and content
| Area | Choice | Notes |
|---|---|---|
| Level blockout/editor | **TrenchBroom** (Quake-style brush editor; editor is GPL, but your maps are yours) with a custom game config | Brushes → fast, precise collision and clean PVS |
| Map compiler | **Custom**: brushes → BVH, PVS/visibility grid, lightmaps | Uses **Intel Embree** (Apache-2.0) for bake rays and **xatlas** (MIT) for lightmap UVs |
| Art | Blender → glTF → cooker | Detail meshes, props, characters |
| Animation | Blender/Maya → cooked clips; **gameplay skeleton separate from cosmetic layers** | |
| Asset cooker | Custom CLI, deterministic output, content-hash cache | Runs in CI |
| Version control | Git + **Git LFS** for binaries (or Perforce once art grows) | |

## Things deliberately left out

- Runtime GI, ray tracing, virtual texturing, world streaming, Nanite-style geometry
- General-purpose physics for gameplay
- A scripting VM in the hot path (Lua/Wasm for *community modes* later, sandboxed and server-side)
- Electron/Chromium for UI
