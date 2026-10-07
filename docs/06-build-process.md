# 06 — Build Process, Roadmap & What It Would Take

## Summary

| | Hobby / solo | Small indie team | Funded studio |
|---|---|---|---|
| People | 1–2 senior engineers | **6–10** | 25–60 |
| Gets you | Feel prototype + LAN netcode demo | **Closed beta of a credible competitive game** | Launch-quality F2P with live ops |
| Time | 6–12 months for the prototype; a full game isn't realistic | **18–30 months** to closed beta | 3–4 years to launch |
| Rough cost* | Your time + ~$2–5k (hardware, test servers) | **~$1.5–3M** to beta | $15–40M+ to launch |

\*Order-of-magnitude estimates. Salaries vary hugely by region, and these exclude marketing, which a competitive F2P launch also needs.

**What actually sets the critical path:** netcode/hit reg, the anti-cheat programme, and matchmaking population. A shooter with great hit reg but 400 concurrent players has 10-minute queues and dies. That's what happened to Spectre Divide. Plan for **community servers + bots + modes that work at low population** from day one.

## Minimum viable team (the indie column)

| Role | Count | Owns |
|---|---|---|
| Engine/graphics programmer | 1 | Platform layer, renderer, perf budget, Reflex/Anti-Lag 2 |
| Netcode/gameplay programmer | 1–2 | Movement, weapons, prediction, lag comp, hitbox contract, server |
| Tools/backend programmer | 1 | Map compiler, asset cooker, CI, matchmaker, accounts, server orchestration |
| Level designer | 1 | Movement lab, 3 competitive maps, map SDK |
| 3D artist (env + props) | 1 | World kit, lightmap-friendly assets |
| Character/weapon artist + animator | 1 | Factions, weapons, gameplay clips |
| Sound designer | 0.5–1 | Footsteps, weapons, utility, mix and occlusion |
| Producer / community lead | 0.5–1 | Playtests, Discord, pro-player feedback, anti-cheat review ops |

## Phase plan with exit gates

Each phase ends with a gate. If a gate fails, fix it before moving on; don't start the next phase in parallel.

### Phase 0 — Feel lab (months 0–3)
**Goal: prove the game feels right before building anything else.**
- Minimal C++ app: window, raw input, simple Vulkan renderer, brush map loaded from TrenchBroom
- Kinematic movement controller at a fixed 128 Hz tick; one rifle with a recoil pattern; hitscan vs. static capsule dummies
- Movement lab map: strafing lanes, jump/peek boxes, counter-strafe timer, spray wall
- Telemetry for time-to-accurate after a counter-strafe, peek exposure, and recoil recovery
- **Gate:** blind test with 10–20 experienced CS players (FACEIT level 8–10 if possible). At least 70% rate movement and shooting "as good as or better than CS:GO". Sustained ≥ 1500 FPS in the lab on the reference rig.

### Phase 1 — Engine core (months 2–6)
- Asset cooker (glTF → binary), texture compression, zstd packs
- Map compiler: brushes → BVH, PVS, lightmaps (Embree + xatlas), material tags for penetration
- Renderer: lightmapped forward pass, characters, viewmodel, decals, particles, PSO cache
- Audio: miniaudio + Steam Audio HRTF
- Tracy everywhere; benchmark-demo harness
- **Gate:** test map renders at 1% lows ≥ 1000 FPS on the high-end rig with no hitch > 3 ms over a 10-minute fly-through.

### Phase 2 — Netcode vertical slice (months 4–9)
- Dedicated headless Linux server, GameNetworkingSockets transport
- Usercmds, clock sync, prediction/reconciliation, snapshot delta compression
- Gameplay pose function + hitbox contract; lag compensation with history ring and rewind cap
- Sub-tick shot timestamps; deterministic spread seeds
- Debug tools: net graph, `cl_showhitboxes 2`, server-confirmed impacts, demo recording
- **Bot duel harness** with `tc netem` in CI; golden determinism tests across MSVC/clang
- **Gate:** hit-reg disagreement ≤ 0.1% across the 10–150 ms latency matrix; server tick p99 ≤ 2 ms with 10 bots; internal 5v5 playtests rate hit reg "better than CS2".

### Phase 3 — Game vertical slice (months 8–14)
- Bomb defusal rules, economy, buy menu, round flow, MR12
- About 8 weapons, full utility set (smoke volumes, flash, HE, molotov, decoy), deterministic grenades
- First competitive map at a "beta-quality" art pass
- Server-side visibility culling (anti-wallhack) including smokes
- Console, configs, binds, crosshair codes, practice commands
- **Gate:** weekly 10-player playtests sustain for 4+ weeks with improving retention scores; benchmark round holds 1% lows ≥ 700 FPS.

### Phase 4 — Online platform (months 12–20)
- Steamworks integration (auth, SDR relay, friends, invites)
- Backend: matchmaker (region + skill + party), ranked rating (Glicko-2), map veto, leaver penalties, match history, reports
- Server orchestration: fleet in 3 regions (EU, NA-East, NA-West), autoscaling, version pinning, rolling deploys
- **Easy Anti-Cheat** integration, account trust score, demo storage, review tooling
- Crash reporting, telemetry dashboards (tick time, hit-reg disagreement, ping distribution)
- **Gate:** 200-player closed alpha runs 2 weeks with < 1% crash rate per match and median queue time < 2 minutes in primary regions.

### Phase 5 — Closed beta (months 18–30)
- 3 maps, 12–14 weapons, Deathmatch/Retakes/Wingman, community server browser
- Map SDK release (TrenchBroom config + compiler + docs) to seed the community
- Statistical aim-analysis pipeline v1, human review queue
- Pro/creator programme: invite CS pros and creators. **Their public verdict on hit reg and feel is the marketing.**
- **Gate:** D30 retention, queue times and hit-reg/cheat metrics meet the bar set for an open beta and launch.

## Repository layout (proposed)

```
/engine
  /core        # allocators, containers, math, logging, job system
  /platform    # window, raw input, timing (Win32 / Linux)
  /render      # RHI (Vulkan), passes, PSO cache
  /audio       # miniaudio + Steam Audio
  /net         # transport wrapper, bit-packer, delta compression
/game
  /shared      # ← compiled into BOTH client and server
    movement/  weapons/  hitbox_pose/  grenades/  rules/
  /client      # prediction, interpolation, HUD, effects
  /server      # lag comp, visibility culling, game rules authority, demos
/tools
  /cooker      # asset pipeline
  /mapc        # map compiler (BVH, PVS, lightmaps)
  /bench       # benchmark-demo runner
  /botduel     # hit-reg harness
/services      # Go: matchmaker, ranked, accounts, reports
/content       # source assets (Git LFS)
/maps          # TrenchBroom .map sources
/docs
```

## CI/CD pipeline

```
 push / PR
   ├─ build matrix: client (Win/MSVC), server (Linux/clang), tools
   ├─ unit tests (doctest)
   ├─ golden determinism tests (movement + hitbox pose; Win vs Linux outputs must match)
   ├─ asan/ubsan build (Linux) for shared/server code
   ├─ allocation audit + PSO-after-load audit
   └─ cook changed assets, compile changed maps (cached by content hash)

 nightly
   ├─ bot duel hit-reg matrix (netem latency × jitter × loss)      → hitreg_disagreement_rate
   ├─ benchmark demos on reference rigs (self-hosted runners)       → avg / 1% low / max frame
   ├─ server soak test (10 bots × 2h)                               → tick p99
   └─ publish build to internal Steam branch

 release
   ├─ versioned server container image → staging fleet → canary region → all regions
   └─ client via Steam branches (beta → default)
```

- **GitHub Actions** with **self-hosted Windows runners** that have real GPUs, for benchmarks and client tests
- Symbols uploaded to the crash-reporting service on every build
- Server images built with Docker, deployed via Agones/Kubernetes or a game-server host's API

## Getting started this week (concrete next steps)

1. **Decide the engine route.** Recommended: custom C++20, per `03-tech-stack.md`.
2. **Set up the skeleton repo:** CMake presets, vcpkg manifest (SDL3, volk/Vulkan headers, VMA, Tracy, doctest, GameNetworkingSockets, zstd), GitHub Actions for Win + Linux.
3. **Write the movement controller first**, in `/game/shared/movement` with golden tests, before any rendering polish.
4. **Build the movement lab map** in TrenchBroom and a minimal `.map` → collision loader.
5. **Recruit 10–20 high-level CS players for Phase 0 blind tests** (Discord, FACEIT community). Their feedback loop is the most valuable asset in the whole project.

## Top risks

| Risk | Mitigation |
|---|---|
| Player population too small for matchmaking | Community servers, bots, Retakes/DM that work with few players, regional launch, creator programme |
| Cheating destroys trust early | AC from day 1, server-side visibility culling, trust-gated ranked, fast public ban waves |
| "Just another CS clone" perception | Lead with measurable claims: published hit-reg agreement, 128 tick, 1% lows. Let pros validate them. |
| Legal/IP | Original maps, names, art, sounds. No Valve assets. Avoid look-alike trade dress. Get legal review before beta. |
| Netcode complexity overruns | Netcode slice (Phase 2) before content; bot harness metrics gate progress |
| Perf erodes as content grows | Benchmarks gate every nightly; per-feature budgets; content budgets for artists (tris, materials, particles) |
| Server costs | 1 match/core is about $0.01–0.03 per match-hour on bare metal; pack more per core once tick p99 allows |
| Burnout / scope creep | The non-goals list in `02-game-design.md` is binding |
