# CSreplacement: research and plan for a CS:GO-like tactical shooter

This repo holds research and design only. There is no game code here. It sets out what it would take to build a tactical FPS where **shooting, movement and positioning come first**, which runs at **700+ FPS** on high-end PCs and has **hit registration players can trust** on online servers.

## Documents

| # | Doc | What's in it |
|---|---|---|
| 01 | [Player research](docs/01-player-research.md) | What CS players want (2026), with sources, and lessons from failed competitors |
| 02 | [Game design & ideation](docs/02-game-design.md) | Pillars, movement model, shooting model, maps, utility, economy, settings, monetization |
| 03 | [Tech stack](docs/03-tech-stack.md) | Engine decision, libraries, networking, backend, anti-cheat, tools |
| 04 | [Netcode & hit registration](docs/04-netcode-and-hitreg.md) | 128 Hz hybrid model, the hitbox contract, lag compensation, testing hit reg |
| 05 | [Performance budget](docs/05-performance-budget.md) | How to hit 700+ FPS 1% lows: CPU/GPU frame budgets and rules |
| 06 | [Build process & roadmap](docs/06-build-process.md) | Team, phases with gates, CI/CD, repo layout, cost, risks, first steps |

## The short answer: what would it take?

**What players want:** CS:GO's feel on infrastructure that works. That means hit reg that matches the screen, anti-cheat that works (the #1 reason players move to FACEIT), consistent movement, 128 tick, stutter-free high FPS, and FACEIT-quality matchmaking built in. They aren't asking for a new formula. Recent "CS with a twist" games such as Spectre Divide shut down within months.

**Core technical bets:**
1. **A custom, lean C++20 engine.** The scope is small and fixed (10 players, static maps, baked lighting, hitscan). That's what makes 1 ms frames possible; general-purpose engines can't get there without massive modification.
2. **A 128 Hz authoritative server with a hybrid timing model.** Movement snaps to ticks so it's consistent and keeps CS:GO muscle memory. Shots are sub-tick timestamped so you hit exactly what you saw.
3. **The hitbox contract.** Hitboxes come from one deterministic pose function, evaluated identically on client and server, and the visible model is skinned from the same skeleton. Cosmetic animation can never move a hitbox.
4. **Measure hit reg continuously.** A bot-duel harness under simulated latency, jitter and loss gates every build. A production "hit-reg disagreement rate" metric could even be published.
5. **Anti-cheat as defence in depth.** Server authority, server-side visibility culling (anti-wallhack), Easy Anti-Cheat (free), statistical aim analysis and demo review.
6. **Strict performance rules.** Baked lighting, no runtime shader compiles, no streaming mid-match, no per-frame allocations, Reflex/Anti-Lag 2, and benchmark gates in nightly CI.

**Team, time and money (rough):** a **6–10 person** senior-heavy indie team needs **18–30 months** to reach a credible closed beta, at about **$1.5–3M** before marketing. A solo developer can realistically build the **feel prototype plus a LAN netcode demo** in 6–12 months. That alone is a valuable proof point for raising money or recruiting.

**Biggest non-technical risk:** player population. Plan for community servers, bots, low-population modes and a pro/creator programme from day one.

## Prototype

[`prototype/`](prototype/) is a minimal single-player **feel lab** in C++20, SDL3 and OpenGL 3.3. It has 128-tick movement, one rifle with a spray pattern, target dummies and a grey-box test map. A GitHub Action builds a Windows `.exe` on every push. See [prototype/README.md](prototype/README.md) to download and play.

## Recommended first step

Run **Phase 0, the "feel lab"**: a fixed 128 Hz movement controller, one rifle, a TrenchBroom test map, and blind tests with 10–20 high-level CS players. If the feel isn't right, nothing else matters. See [06-build-process.md](docs/06-build-process.md).
