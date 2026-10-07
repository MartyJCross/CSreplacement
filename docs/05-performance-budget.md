# 05 — Performance Budget: 700+ FPS

## The target, stated precisely

> **On a high-end PC (current-gen X3D-class CPU, high-end GPU, 1080p–1440p, competitive settings): average ≥ 1000 FPS, 1% lows ≥ 700 FPS, no frame > 3 ms during a match.**
> **On a mid-range PC: ≥ 300 FPS with 1% lows ≥ 200 FPS.**

We aim for 1000 average so the 1% lows stay above 700. CS players quote 1% lows, and stutter is the main thing they complain about in CS2.

**At 700 FPS a frame is 1.43 ms. At 1000 FPS it's 1.0 ms.** CPU and GPU run pipelined, so **each must individually fit in about 1.0 ms**.

## Frame budget

### CPU (main thread), target ≤ 0.6 ms
| Stage | Budget | How |
|---|---|---|
| Drain raw input (batched) | 0.01 ms | `GetRawInputBuffer`, no per-event message pumping |
| Network receive/decode | 0.02 ms | Bit-unpack into preallocated buffers |
| Prediction re-sim (≤ ~10 cmds of own movement) | 0.03 ms | Tiny, cache-hot kinematic code |
| Interpolate 9 players + evaluate gameplay poses | 0.04 ms | Pose = few clip samples per player, SIMD |
| Cosmetic animation / effects / particles update | 0.08 ms | Fixed pools, SoA, time-sliced ragdolls |
| Visibility (PVS lookup + frustum cull) | 0.03 ms | Precomputed PVS from the map compiler |
| Build render list + record command buffers | 0.25 ms | Few pipelines, merged static geometry, bindless |
| UI/HUD | 0.03 ms | Retained HUD, 1–2 draw calls |
| Slack | 0.11 ms | |

Audio, the network socket thread and asset I/O run on **other threads**. Nothing blocks the main thread.

### GPU, target ≤ 0.8 ms at 1440p on a high-end card
| Pass | Budget | How |
|---|---|---|
| World opaque (forward, lightmapped) | 0.25 ms | Merged clusters, texture arrays/bindless, no depth prepass unless measured to help |
| Characters (10) + viewmodel | 0.08 ms | LOD'd, single material each |
| Decals / impacts | 0.03 ms | Pooled, capped |
| Smokes (volumetric, half-res raymarch) | 0.15 ms | Low-res voxel grid; budget scales with count |
| Particles (muzzle, molotov, sparks) | 0.07 ms | GPU-instanced, hard caps |
| Tonemap + UI composite | 0.05 ms | No bloom/DOF/motion blur by default |
| Slack | 0.17 ms | |

## Rules that make the budget hold

### Rendering
1. **Baked lighting only.** Lightmaps + light probes for dynamic objects. One shadow-casting dynamic light max (optional; off at low settings).
2. **Static world merged offline** into spatial clusters, culled by PVS. Target **< 500 draw calls per frame** in the worst view.
3. **A handful of pipelines.** World, character, viewmodel, particles, smoke, UI. Material variety comes from texture arrays and parameters, not shader permutations.
4. **All pipelines/PSOs created at load time** with a persisted pipeline cache. **Runtime shader compilation is a bug.**
5. **No runtime streaming during a match.** The whole map, characters, weapons and effects are loaded before the round starts. Memory is cheap; hitches aren't.
6. **Presentation:** flip-model with tearing allowed (`DXGI_PRESENT_ALLOW_TEARING` / Vulkan `IMMEDIATE`), optional `fps_max`, plus Reflex/Anti-Lag 2 to keep the render queue empty when GPU-bound.
7. **Resolution scale** and 4:3 stretched support so low-end and "pro config" players are both happy.

### CPU / engine
1. **No heap allocations in the frame loop.** Use frame-linear allocators and fixed pools. Enforce with an allocation counter that asserts in debug builds.
2. **Data-oriented layout.** Structs-of-arrays for anything iterated per frame. Fits in L2/L3; this is why X3D CPUs love CS.
3. **No virtual-dispatch-heavy scene graph.** Use flat arrays and explicit update order.
4. **Threading:** main thread (input → sim → render list); render thread records/submits the previous frame's list *only if* profiling shows a win at 1 ms frame times (pipelining adds about 1 frame of latency). Jobs for particles, culling and skinning if needed.
5. **8 kHz mice:** batch-read raw input. Never process one window message per mouse report.
6. **Ragdolls/cosmetic physics** are capped (e.g. ≤ 5 active), time-sliced and frozen after settling. CS2 players specifically report ragdoll stutter.
7. **Logging/telemetry** go to a lock-free ring buffer flushed by a background thread.

### Latency (input → photon)
- Sample input **as late as possible** before simulation and rendering (Reflex/Anti-Lag 2 markers do this).
- The camera uses the newest mouse delta even if the sim tick hasn't advanced.
- Measure with the Reflex latency markers and the **flash indicator** + an LDAT/OSLTT-style photodiode on CI hardware.

## Performance testing in the build process

| Test | Where | Gate |
|---|---|---|
| **Benchmark demo playback** (busy round: 10 players, 4 smokes, 2 molotovs, spraying) | Nightly on a fixed reference high-end rig + mid-range rig | Fail if avg, 1% low or max frame time regresses > 3% |
| **Hitch detector** | Every playtest and benchmark | Any frame > 3 ms is logged with a Tracy capture |
| **Shader/PSO audit** | CI | Fail if any pipeline is created after map load |
| **Allocation audit** | CI (debug build) | Fail on any heap allocation in the frame loop |
| **Server tick time** | Bot-match soak test | Fail if p99 > 2 ms |

## Reference hardware (keep fixed for comparability)

- **High-end rig:** current-gen 8-core X3D CPU, top-tier GPU, 6000+ MT/s DDR5, 1440p 480 Hz+ monitor, 8 kHz mouse
- **Mid-range rig:** 6-core previous-gen CPU, mid-range GPU, 1080p 240 Hz
- **Low-end rig:** 4-core laptop CPU with integrated graphics (target ≥ 144 FPS at low settings and reduced resolution)
