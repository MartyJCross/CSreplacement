# 01 — What Counter-Strike Players Want

Research snapshot: October 2026. Sources: Steam community discussions, esports press, Valve patch coverage, Riot's tech blog, and post-mortems of competitors that failed. Full source list is at the bottom.

## TL;DR

CS players don't want a new game. They want **CS:GO's feel, running on infrastructure that actually works**. Ranked roughly by how loud and how persistent each complaint is:

| # | What they want | Why it matters | Evidence |
|---|---|---|---|
| 1 | **Hits that land when they look like they land** | The top complaint since CS2 launched. "I saw the headshot, the server disagreed." | Steam threads on hit reg; CS2 subtick explainers; repeated Valve lag-comp patches through 2026 |
| 2 | **Anti-cheat that works** | This is the single biggest reason players leave official matchmaking for FACEIT. They accept kernel-level AC if it actually works. | VACnet bypass fixed only in July 2026; FACEIT reputation; change.org "Save CS2" petition |
| 3 | **Movement that is crisp, consistent and predictable** | CS2 movement is widely described as "floaty" and inconsistent next to CS:GO. Players' muscle memory (counter-strafing, jump timing) was trained on CS:GO. | earlygame and Reddit analyses of subtick movement inconsistency |
| 4 | **128 tick** (or something that feels like it) | Half of this is perception and half is real. FACEIT has sold "128 tick" for a decade. Pros such as ropz have weighed in publicly. | ropz on tick rate; "even 64 tick is better" debate; FACEIT tick guides |
| 5 | **High, stable FPS with good 1% lows and no stutter** | CS2 stutters (shader loads, ragdolls, busy fights) and has worse 1% lows than CS:GO on the same hardware. Competitive players buy 360–540 Hz monitors. | Steam perf threads; GGRecon stutter guide; Animgraph 2 render-queue regressions (Blur Busters forum) |
| 6 | **Less peeker's advantage; holding an angle should be viable** | CS's tactical identity depends on defenders being able to hold positions. | Riot's 128-tick article; CS2 datamines show Valve still working on it |
| 7 | **Competitive integrity in matchmaking** | Map veto, tight skill spread, real leaver penalties, serious lobbies. This is FACEIT's other selling point. | Talkesport "Why CS2's MM still feels inferior to FACEIT" |
| 8 | **Community ecosystem** | Community servers, workshop maps, surf/KZ/retakes/DM, practice configs, demos. This is where CS culture and new players come from. | 2026 wishlists from Dot Esports and bo3.gg; Valve integrating surf |
| 9 | **Consistent utility** | Grenade lineups must be reproducible every time (CS2's jump-throw changes upset a lot of players). | Community wishlists and movement threads |
| 10 | **Economy and format tuning** | MR12 snowballing, loss-bonus curves, pistol-round weight. | 2026 wishlists |

## Detailed findings

### 1. Hit registration is still the #1 trust problem

- Players report clean headshots that the server scores as body shots or misses, shots that visibly leave the gun but register 0 damage, and hit reg that depends on whether the mouse was perfectly still at the moment of firing.
- **Root causes the community has identified**, and which Valve's own patches confirm:
  - *Lag compensation rewound hitboxes to a point that didn't match the screen.* Valve patched "a case mid-spray where lag compensation would rewind target hitboxes further into the past than what was on screen."
  - *Animation desync.* Hitbox poses on the server don't match the animated model on the client. In April 2026 Valve rebuilt the whole animation system ("Animgraph 2"), and that introduced new desync reports under server load.
  - *Subtick visual vs. server mismatch.* Subtick improves the shooter's accuracy but adds a noticeable delay for the person being shot, which reads as "I was already behind the wall."
- **Lesson for us:** hit reg can't be "an engine feature" bolted onto an animation system. It has to be designed so that the **client's render of a hitbox and the server's rewind of that hitbox are produced by the same deterministic function from the same inputs**, and we have to **measure** that agreement continuously. See `04-netcode-and-hitreg.md`.

### 2. Cheating decides where competitive players live

- VAC runs in user mode and is widely considered weak against kernel-level cheats. For more than a year, a bug let rage-hackers avoid VACnet processing; it was fixed in July 2026.
- False-positive ban waves (for example, flagging `+turnleft` spin binds) have hurt trust too.
- Players will move to a platform with kernel-level AC (FACEIT) as soon as they care about rank. Community-reported cheater rates differ by orders of magnitude between official MM and FACEIT. The exact figures are anecdotal, but the perception is universal.
- **Lesson:** anti-cheat goes in from day 1 as **defence in depth**: server authority, server-side visibility culling (anti-wallhack), kernel AC, statistical aim analysis, and demo review. It can't be a third-party install players opt into later.

### 3. Movement feel is muscle memory, and muscle memory is CS:GO

- Analyses of CS2 movement argue that subtick input timing gives "a near infinite range of values between two ticks that all affect speed". As a result, two identical-feeling counter-strafes can produce different velocities. CS:GO's 64/128 Hz grid was **coarser but consistent**.
- What players want preserved: counter-strafing (opposite key stops you fast and makes your first bullet accurate), walk/crouch speeds, jump height, ladder and stair behaviour, the air-strafe skill ceiling, and repeatable jump-throws.
- **Lesson:** quantize movement to a fixed tick so it's deterministic and repeatable, and only timestamp the things that benefit from sub-tick precision (shots). See the hybrid model in `04-netcode-and-hitreg.md`.

### 4. Tick rate: give them 128, and make it real

- Riot shipped VALORANT at 128 tick after testing with high-speed cameras. They found about 80 ms to be a "fair" peeker's-advantage window and got below 60 ms with 128 tick plus other work. Their main constraint was server CPU: each frame has to finish in 7.8 ms.
- **Lesson:** 128 Hz authoritative simulation is table stakes for our audience, and it's achievable for 10-player matches with a lean server. It's also a marketing line players already understand.

### 5. Performance and latency are features

- CS2 complaints center on stutter, not averages: shader compilation hitches, ragdoll spikes, and frame drops when many players or a lot of utility are on screen. 1% and 0.1% lows are the metric enthusiasts quote.
- The hardware audience: 360/480/540 Hz monitors, 4–8 kHz polling mice, X3D CPUs. CS:GO famously ran at many hundreds of FPS on that kind of hardware.
- **Lesson:** budget for **1% lows ≥ 700 FPS**, not averages. Ban runtime shader compilation and runtime streaming during a match. Integrate NVIDIA Reflex and AMD Anti-Lag 2 (both free SDKs; CS2 ships both). See `05-performance-budget.md`.

### 6. Peeker's advantage and holding angles

- Peeker's advantage ≈ peeker's latency + defender's latency + interpolation delay + tick quantization + render/display latency. Every ms we remove from that chain is a design win.
- Players also hate the inverse: being killed "after" they reached cover on their own screen. That comes from large rewind windows for high-ping shooters.
- **Lesson:** small interp buffers, 128 Hz, regional servers close to players, good routing, and a **hard cap on rewind** so high-ping players don't punish everyone else.

### 7. Matchmaking quality

- What FACEIT does that players value: map veto, tight skill spread, leaver penalties, serious lobbies, and 128 tick.
- **Lesson:** ship "FACEIT-quality" ranked inside the game. That's our clearest differentiator against official CS2 MM.

### 8. Community, modding and culture

- 2026 wishlists ask for community map rotations, surf/KZ in the main game, and deeper workshop support. CS has a long-running culture of community servers, configs (`autoexec.cfg`), crosshair codes, practice maps and demo review.
- **Lesson:** a **console, config files, practice commands, demo recording and a map SDK** aren't extras. CS players expect them, and modders are what keep a game alive.

### 9. Quality-of-life CS players will check for in the first five minutes

- **Sensitivity parity:** use the same yaw convention (`m_yaw 0.022` degrees per count) so players can bring their CS sensitivity over unchanged.
- **Raw input**, no mouse acceleration, and 8 kHz mice that don't tank FPS.
- **Crosshair customization** with shareable codes, viewmodel offsets, FOV handling, support for 4:3 stretched resolutions, and **no forced motion blur, bloom or post-processing**.
- **Audio clarity:** directional footsteps you can trust. Sound is information in CS.
- Fast queue times, fast map loads, and no forced launcher.

## What *not* to do: lessons from recent competitors

- **Spectre Divide** (Mountaintop Studios, 2024–2025) shut down six months after launch. It peaked around 400k players in week one and fell below 4k concurrent. A novel mechanic didn't overcome a saturated market, monetization complaints and thin content. Its studio ran out of money.
- **XDefiant** (Ubisoft) was also shut down in 2025.
- **The pattern:** "CS/Valorant, but with a twist" doesn't move players. CS2 is free and has 25 years of habit and a skin economy behind it. A challenger needs a **clear, demonstrable technical edge on exactly what CS players complain about** (hit reg, AC, tick, FPS) plus a **community and competitive platform reason** to stay.

## Implications → design pillars

1. **Trustworthy hit reg, measured and published.**
2. **Deterministic, CS:GO-like movement.**
3. **128 Hz authoritative servers, small interpolation, capped rewind.**
4. **1% lows ≥ 700 FPS on high-end hardware; runs well on low-end.**
5. **Anti-cheat as defence in depth from day 1.**
6. **FACEIT-quality ranked built in.**
7. **Moddable: console, configs, map SDK, demos, community servers.**

These pillars feed `02-game-design.md`.

## Sources

- [CS2 2026: VAC Live is completely dead, optimization is broken, cheaters everywhere (Steam)](https://steamcommunity.com/app/730/discussions/0/845130462379847459/)
- [The hit reg is just awful now (Steam)](https://steamcommunity.com/app/730/discussions/0/594032343582357563/)
- [[CS2] Serious issues with hit registration and anti-cheat — ValveSoftware/csgo-osx-linux #4339](https://github.com/ValveSoftware/csgo-osx-linux/issues/4339)
- [CS2 Subtick System: Why Bullets Miss and How It Works (csgo-news)](https://csgo-news.com/en/guides/why-bullets-miss-an-analysis-of-the-cs2-subtick-system/)
- [CS2 Tick Rate & Subtick System: Full Guide 2026 (skin.club)](https://community.skin.club/en/articles/what-is-subtick-and-how-does-it-work)
- [CS2 sub-tick explained (profilerr)](https://profilerr.net/cs2-sub-tick-explained-how-does-it-work-and-is-it-better-than-128/)
- [CS2: Issues With Movement In CS2 (earlygame)](https://earlygame.com/general-news/cs2-movement-issues)
- [r/GlobalOffensive — CS2 movement inconsistency](https://redlib.hackliberty.org/r/GlobalOffensive/comments/1k5g10i/cs2_movement_inconsistency)
- ["Even 64 tick is better": CS2 community debates the sub-tick system (Sportskeeda)](https://sportskeeda.com/esports/even-64-tick-better-cs2-community-debates-sub-tick-system)
- [CS pro ropz on 128 tick in CS2 (GGRecon)](https://www.ggrecon.com/articles/cs-pro-ropz-opens-up-in-rant-on-128-tick-rate-in-cs2/)
- [How Bad Is the CS2 Cheating Problem? (csdb.gg)](https://csdb.gg/how-bad-is-the-cs2-cheating-problem/)
- [CS2 VACnet update — rage cheat bypass fixed (ExitLag)](https://www.exitlag.com/blog/vacnet-update-cs2/)
- [SAVE CS2: Demand a Stronger Anti-Cheat Solution (change.org)](https://www.change.org/SaveCS2Valve)
- [CS2 gets plagued with cheaters, players call for VAC overhaul (Sportskeeda)](https://sportskeeda.com/esports/cs2-gets-plagued-cheaters-players-call-for-vac-overhaul)
- [Why CS2's Matchmaking Still Feels Inferior to FACEIT (Talkesport)](https://www.talkesport.com/editorials/why-cs2s-matchmaking-still-feels-inferior-to-faceit/)
- [Does FACEIT Use 128-Tick Servers? (tradeit)](https://tradeit.gg/blog/does-faceit-use-128-tick-servers/)
- [How to fix CS2 stuttering with high FPS (GGRecon)](https://www.ggrecon.com/guides/cs2-how-to-fix-stuttering-with-high-fps/)
- [Animgraph 2 broke the rendering queues in CS2 (Blur Busters forum)](https://forums.blurbusters.com/viewtopic.php?p=124604)
- [Valve improves lag compensation in latest CS2 update (Talkesport)](https://www.talkesport.com/news/cs2/valve-improves-lag-compensation-in-latest-cs2-update/)
- [CS2 Nov 6 patch notes: lag compensation and bullet hit feedback (Dot Esports)](https://dotesports.com/counter-strike/news/counter-strike-2-nov-6-patch-notes-defuse-bug-fixed-lag-compensation-and-bullet-hit-feedback)
- [Valve reportedly works on fixing peeker's advantage (Escorenews)](https://escorenews.com/en/csgo/news/52588-valve-reportedly-works-on-fixing-peeker-s-advantage-and-holding-angles-in-cs2)
- [What CS2 Needs in 2026 (Dot Esports)](https://dotesports.com/counter-strike-2026-wishlist)
- [CS2 Wishlist 2026 (bo3.gg)](https://bo3.gg/articles/cs2-wishlist-2026)
- [VALORANT's 128-Tick Servers (Riot Tech Blog)](https://technology.riotgames.com/node/112)
- [Riot's crusade against peeker's advantage (PC Gamer)](https://pcgamer.com/valorant-peekers-advantage-interview)
- [Spectre Divide Shuts Down: Why it Failed (Prima Games)](https://primagames.com/news/spectre-divide-shuts-down-why-it-failed)
- [Tactical FPS Spectre Divide is shutting down (PCGamesN)](https://www.pcgamesn.com/spectre-divide/shutting-down)
- [NVIDIA Reflex SDK](https://developer.nvidia.com/reflex)
- [AMD Radeon Anti-Lag 2 SDK (GPUOpen)](https://gpuopen.com/learn/integrating-amd-radeon-anti-lag-2-sdk-in-your-game/)
