# 02 — Game Design & Ideation

The goal: **shooting, movement and positioning are the game.** Everything else is minimal, readable and stays out of the way.

Working title in these docs: **Project CROSSHAIR** (placeholder).

## Design pillars

1. **Skill is legible.** When you win or lose a duel you can see why: crosshair placement, the counter-strafe, the angle you took, the information you had. No randomness the player can't control or learn.
2. **What you see is what the server sees.** Visuals, hitboxes and audio are faithful to the simulation. Cosmetics never change silhouettes or hitboxes.
3. **Positioning beats reflexes over a round.** The map, utility and economy reward taking smart space, holding angles and trading. Raw aim alone isn't enough.
4. **Consistency over realism.** Every jump, strafe-stop, spray and grenade lineup gives the same result every time.
5. **Lightweight.** Every feature has a frame-time and bandwidth budget. If it can't fit, it doesn't ship.

## Core loop (familiar on purpose)

- **5v5, attack/defend, bomb objective, round-based economy, MR12.** We aren't innovating the format. The research shows that "twist" games fail; trust, feel and tech are where we win.
- **Additional modes**, all on the same core: Deathmatch, Retakes, 2v2 Wingman, Aim/Practice, plus community modes (surf/KZ) through the SDK.
- Possible differentiators to test later (not launch blockers): ranked Retakes queue, structured 1v1 "duel ladder" warmups, built-in demo review with hit-reg overlays.

## Movement model

A Quake/Source-lineage kinematic controller, tuned to feel like CS:GO. **No rigid-body physics on players.**

| Mechanic | Design | Rationale |
|---|---|---|
| Ground movement | Accelerate/friction model (`accel`, `friction`, `stopspeed`), max speed set by the equipped weapon | Familiar, learnable, and makes counter-strafing work |
| Counter-strafing | Tapping the opposite key applies friction plus counter-acceleration. Accuracy recovers when speed is ≤ ~34% of weapon max speed | This is the CS skill check players expect |
| Walk (shift) / crouch | Fixed fractions of max speed; silent walk | Sound = information |
| Jump | Fixed height and gravity. Landing penalty to accuracy and speed. **No bunny-hop chaining** in competitive (configurable for community servers) | Consistency; keeps positioning more valuable than movement tech |
| Air strafe | Source-style air acceleration (low cap) | Preserves the skill ceiling for jump-peeks and surf/KZ modes |
| Stairs / ladders / step-up | Deterministic step height, ladder volumes | No "sticky" geometry |
| Tick quantization | **Movement inputs apply at 128 Hz tick boundaries** | Same input gives the same velocity every time (see `04-netcode-and-hitreg.md`) |
| Tagging | Being hit slows you (tunable per weapon) | Rewards first hit; punishes wide swings |

**Tuning method:** build a "movement lab" map plus telemetry. Record traces such as time to accurate after a counter-strafe, peek distance per 100 ms, and jump-peek exposure time. Compare them with recordings of CS:GO/CS2 behaviour measured by players. Blind test with experienced CS players (see `06-build-process.md` Phase 0).

## Shooting model

| Mechanic | Design |
|---|---|
| Hit detection | Hitscan rays against per-bone capsule/OBB hitboxes (~19 per player). The head is separate and slightly generous but visually honest. |
| First-shot accuracy | Rifles are **perfectly accurate on the first shot** when standing still or counter-strafed. Pistols are near-perfect. |
| Spray | **Fixed, learnable recoil pattern per weapon** plus small random spread that grows with sustained fire. Recoil recovers on a fixed curve. |
| Randomness | Spread uses a **deterministic seed** shared by client and server (derived from the command number). The client's predicted impacts match the server's, so no "random" desync. |
| Movement inaccuracy | Inaccuracy scales with speed; jumping and landing add large penalties. |
| Damage | Per-hitgroup multipliers, armor, and range falloff. Headshot is one-tap for rifles against helmets where it makes sense (tunable). |
| Wallbangs | Material-based penetration (thickness × material power), consistent and readable. Map surfaces are tagged in the editor. |
| Tagging / flinch | No aim punch that moves the crosshair randomly; only a slight, consistent view-kick. |
| Feedback | Instant predicted client effects (tracer, impact, sound). **Damage and kills are server-confirmed only**, with an optional "server-confirmed impact" debug overlay. |

### Weapon roster at launch (small, deep)

About 12–14 weapons spread across pistols (4), SMGs (2), rifles (3: an AK-style, an M4-style and a cheap one), AWP-style sniper (1), scout-style sniper (1), shotgun (1) and LMG (optional). Each has a distinct pattern, price and role. More weapons means more balance surface and less mastery, so add slowly.

## Positioning and maps

- **3 launch maps**, built to classic three-lane competitive principles: clear angles, known common positions, timing parity from spawns to choke points, multiple ways into each site, and rotate times that make information matter.
- **Original maps and art.** Never recreate Valve's maps (`de_dust2` etc. are Valve IP). Mechanics aren't copyrightable, but level layouts as expressed, names, art and sounds are protected.
- **Readability rules:** player models contrast strongly with environments (silhouette and value checks for every area); no foliage or clutter that hides heads; one consistent lighting direction per zone; no dark corners where there shouldn't be.
- **Visibility tooling:** every map ships with a precomputed **potentially visible set (PVS)** and a finer **"can-see" grid** used by server-side anti-wallhack culling.

## Utility

- Smoke, flash, HE, molotov/incendiary, decoy.
- **Smokes:** a cheap volumetric (low-res voxel flood fill, rendered at reduced resolution, about ≤0.15 ms GPU budget) that can react to HE grenades. Smoke occlusion **must be computed on the server too** so anti-wallhack culling respects it.
- **Deterministic grenade physics.** A custom integrator, not a general physics engine. Lineups are 100% repeatable. A "jump-throw" is a first-class input (throw on jump at a fixed tick offset), so players don't need binds.
- **Flash:** a clear rule set based on view angle and distance, with no "half-flashed by a wall" weirdness.

## Economy

- CS-like economy with tuning hooks: kill rewards per weapon class, loss-bonus ladder, plant bonus.
- Address the MR12 complaint with a slightly **softer loss-bonus ladder and lower pistol-round leverage**. These are server config values, so we can A/B them in playtests.

## Audio (gameplay-critical)

- **Steam Audio** (free, Apache-2.0) HRTF plus occlusion/propagation. Footsteps, reloads and utility must give reliable direction and distance.
- **Audio is simulation-driven.** The server decides which sounds a player is allowed to hear (also an anti-cheat point: don't send footsteps the player couldn't hear).
- Audio runs on its own thread, and the mix has a strict voice budget.

## Visual style

- Clean, stylized-realistic. Baked lighting. **No motion blur, no film grain, no chromatic aberration, no depth of field.** Optional bloom off by default.
- Low settings must look *competitive*, not broken. Most pros will play on the lowest settings.
- Character models: two factions with distinct silhouettes and a team colour accent visible at range.
- **Cosmetics never change hitboxes, silhouettes or visibility.** Weapon skins and gloves only, at first.

## Settings CS players will look for on day 1

- Console (`~`), `autoexec.cfg`, binds, aliases (with safe restrictions), `m_yaw 0.022` sensitivity parity, raw input
- Crosshair editor with share codes; viewmodel FOV and offsets; HUD scale
- Resolution scaling plus 4:3 stretched support; uncapped FPS; `fps_max`
- Reflex / Anti-Lag 2 toggles; net graph showing ping, loss, choke, server frame time and tick
- Practice commands on local and private servers (infinite ammo, grenade trajectories, `noclip`, bots)
- Demo recording on server and client, demo viewer with timeline and hitbox overlay

## Monetization (keep trust)

- **Free-to-play.** CS2 is free, so a paid challenger is fighting uphill.
- **Direct-purchase cosmetics and a battle pass. No loot boxes and no tradable gambling economy.** Loot boxes are increasingly regulated, and gambling scandals are a known reputational risk around CS skins.
- **Prime-like trust gate for ranked:** phone/Steam account age plus AC, and optionally a small one-time purchase. This raises the cost of ban evasion.

## Non-goals

- No hero abilities, no destructible environments, no large maps or vehicles, no battle royale.
- No physics-driven gameplay objects other than grenades.
- No dynamic time of day, no dynamic global illumination, no ray tracing.
