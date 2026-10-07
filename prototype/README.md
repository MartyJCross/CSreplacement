# Feel Lab: single-player prototype

This is a deliberately tiny prototype for testing **movement and shooting feel**. It has:

- 128-tick fixed simulation
- uncapped FPS
- raw mouse input
- one rifle with a fixed spray pattern
- target dummies on a grey-box test map
- **sound:**
  - gunshots, footsteps, landing, reload, dry-fire
  - body-hit thwack and a headshot "dink"
  - strafing dummies make **positional footsteps**, so you can test hearing direction
- **first-person weapon:**
  - a rifle and a knife
  - recoil kick, sway, walk bob, landing dip
  - reload and draw animations
- **effects:** muzzle flash, tracers, impact debris, blood, and dummies that collapse on death

Everything is generated in code: there are no asset files, and the whole download is about 1.5 MB. It has no networking and no real art. See `../docs/06-build-process.md` (Phase 0) for why this comes first.

## Download and play (Windows)

1. On GitHub, open **Actions → Feel Lab prototype**, click the latest green run, and download **FeelLab-windows-x64** at the bottom of the page.
2. Unzip it anywhere and run `feellab.exe`. No installer is needed.
3. On first launch a `config.cfg` is created next to the exe. Edit it to set your sensitivity, crosshair, resolution and so on, then restart.

If Windows SmartScreen warns about an unknown app, click **More info → Run anyway**. The exe isn't code-signed.

**Laptops:** the exe asks the driver for the discrete GPU. If FPS is low, check that Windows is using the high-performance GPU (*Settings → System → Display → Graphics*) and that the laptop is plugged in.

## Controls

| Key | Action |
|---|---|
| WASD | Move |
| Space / mouse wheel | Jump. **Hold Space to bunny hop** (auto-jump on landing) |
| Ctrl | Crouch (crouch in the air = crouch-jump) |
| Shift | Walk |
| Mouse 1 | Fire |
| R | Reload |
| 1 / 2 / 3 / 4 | Like CS: primary (rifle, or sniper if you picked it with B) / pistol (semi-auto) / knife (faster movement) / smoke grenade |
| Q | Switch back to your previous weapon |
| B | Buy menu: pick your primary, 1 = rifle, 2 = sniper. You're holding it straight away |
| Mouse 2 | Sniper scope: 40 → 15 FOV → off. Stays scoped when you fire. With the smoke out: underhand lob |
| Tab | Scoreboard (deathmatch) |
| V | Noclip (fly where you look; Shift = slow) |
| F6 | Reset to spawn |
| F5 | Reload `config.cfg` live (sensitivity, crosshair, viewmodel, volume, spread) |
| C | Clear bullet decals |
| G | Quick smoke without switching (deterministic bounces, so lineups repeat; jump-throws carry your momentum). With the smoke out (4), Mouse 1 throws and Mouse 2 lobs, then you switch back to your previous weapon |
| F4 | Bots shoot back (toggle). You get HP, a damage flash and death/respawn |
| F3 | Aim drill: killed range dummies respawn at random spots, and the HUD shows time-to-kill (last + average) |
| F7 | Deathmatch on Dust2 (toggle) |
| F8 | Switch map: Feel Lab / Dust2 (real scale) |
| F1 | Toggle help |
| Alt+Enter | Toggle fullscreen |
| Esc | Pause + **settings menu**: ↑/↓ select, ←/→ or mouse wheel change (Shift = ×5). Changes apply instantly and save to `config.cfg`. Q quits |

## The map

- **Ahead (the range):** static dummies at about 13 / 26 / 39 / 52 m, and two strafing dummies. One stops at each end like a counter-strafer; one runs straight through.
- **Left:**
  - crates of 32, 64 and 80 units (the 64 needs a crouch-jump; the 80 is out of reach)
  - stairs to a platform
  - a wall with a doorway, with dummies behind it to practise jiggle-peeking
- **Right:** a dark spray wall. Stand on the small marker and spray. Bullet decals go **yellow → red** through the magazine, so you can see the pattern.

## What the HUD tells you

- **FPS / 1% LOW / MS:** average FPS, the 1% low (99th percentile frame time) and average frame time, updated every 0.5 s.
- **SPEED / SPREAD / ACCURATE:** horizontal speed, your current bullet spread in degrees, and `ACCURATE` (green) when you're slow enough for a perfect first shot (≤ 34% of rifle speed, ~73 u/s).
- **LAST STOP:** how long your last stop took from full speed to accurate. It's labelled COUNTER-STRAFE if you tapped the opposite key, RELEASE if you just let go. Expect about **62 ms** counter-strafing vs **~195 ms** releasing.
- **Hit log (top right):** hitgroup, damage, kill, distance in metres.

## Dust map (F8)

**F8** switches between the Feel Lab and **Dust2 at real scale** (about 4,200 x 4,250 units, the same units as CS: 250 u/s with the knife, a 72-unit-tall player). Your choice is saved (`map 1`). The area you're in shows at the top of the screen (LONG A, CATWALK, B SITE, ...).

- **Every route:**
  - T spawn, outside long, long doors (roofed), long A with the blue container and the pit, the A ramp, A site with goose
  - top mid, mid, xbox (jump on it to reach catwalk), catwalk, short stairs and short
  - mid doors, CT mid, CT spawn, the CT ramp to A, mid to B and B doors
  - outside tunnels, upper tunnels (roofed) and the tunnel exit, lower tunnels into mid, B site with the back plat, car and boxes
- **Real height levels:**
  - T spawn is high ground and mid drops down towards CT
  - catwalk is a ledge above mid
  - A site and short sit up high
  - the pit and lower tunnels dip down
- **Ramps are small steps for now** (at most 16 units each). Walking feels like a ramp because the camera eases over each step (`view_smooth_steps`), but a jump into a ramp can catch on a step edge. Real slopes are on the to-do list.
- **It's built from memory,** so it isn't a perfect copy. The headless tests run a simulated player along the main routes and print the run times, for example T spawn to B site and CT spawn to A site. Compare them with what you remember from CS.

On Dust the 4 bots play the angles:
- They hide behind cover and peek out after a random wait. Their spots:
  - the long corner behind the blue container
  - through mid doors
  - the B default box (towards tunnels)
  - the A default box (towards long)
  - through B doors
  - short (down the catwalk stairs)
- They hold the angle for a random time, then fall back.
- They react in 0.25–0.55 s with slight random aim error. Your own shots stay fully deterministic.
- They see and shoot up to 4,000 units, so long A and mid are real sightlines.
- They respawn 2–4 s after you kill them, at a free spot.
- You can hear their footsteps when they peek.

**Size:** Dust is 60% of real Dust2 by default (`dust_scale`, Esc menu: DUST SIZE, 50-100%). Everything scales evenly, heights too, so slopes stay walkable; crates, headroom and doorways (at least 96 units wide) keep their real size.

## Performance

`feellab.exe --bench 10` runs 10 s at real speed and writes `bench.txt` (avg FPS, 1% low, time per frame part, GPU). On the owner's Ryzen 7 4800H (integrated Radeon), 1080p deathmatch at 4x MSAA went from 126 to ~190 FPS average and 62 to ~130 1% low with:
- a depth pre-pass, so every pixel is shaded once (`depth_prepass`)
- world boxes culled to the view and drawn nearest-first each frame
- lighting worked out per face instead of per pixel
- `msaa 0` gives ~275 FPS if you'd rather have frames than smooth edges

## Deathmatch (F7)

**F7** starts deathmatch on Dust2 (and back to practice). It's saved, so the game reopens in deathmatch.

- **The match:**
  - it lasts 5 minutes (`dm_minutes`) against 10 bots (`dm_bots`, up to 16); both are in the Esc menu and apply from the next match
  - your kills, deaths and the clock show under the area name; **Tab** shows the scoreboard with HS % and accuracy
  - at the end you get the results screen, and a new match starts by itself
- **Spawns are anywhere on the map** (any open spot you can walk to and back from):
  - you respawn 1.2 s after dying, away from the bots and out of their sight, with every gun reloaded and 1 s of protection
  - bots respawn 2–4 s after you kill them, somewhere you can't see
- **A kill gives you +40 HP** (up to 100), whenever bots are shooting back
- **The bots:**
  - they roam the whole map: each one picks a random spot anywhere, walks the real route there (doors, tunnels, ramps), holds an angle briefly, and moves on
  - they see in a 150° cone in front of them, so you can catch one from behind. A bot you shoot turns on you
  - they hear your running footsteps (about 1,100 units) and your gunshots (about 2,200), and come to check
  - they stop to shoot, like CS bots, after a 0.25–0.55 s reaction, and they have to turn to face you first

## Players: facing, arms, anti-aliasing

- **Bots turn to face you** when they fight, and face where they're walking otherwise. Their hitboxes turn with them. Head-on you see about 27 units of shoulders; side-on, about 21.
- **Arms holding the rifle** are part of the model and hitbox, and count as chest, like CS. The rifle itself isn't hittable, also like CS.
- **Anti-aliasing** (`msaa 4`, Esc menu, applies on restart) smooths edges so far-away players stop shimmering and are easier to pick out.

## Spray feedback (all cosmetic: your bullets are unchanged)

- **The gun's kick builds through a spray:** it climbs, shakes and rolls harder the longer you hold Mouse 1.
- **A slight camera roll on each shot** that grows through the spray. It turns around the crosshair, so where you aim doesn't move. Turn it off with `view_shake 0` (Esc menu: SPRAY CAMERA SHAKE).
- **Shell casings** fly out to the right.
- **Far impacts are drawn bigger** (dust puffs up to 4x, bullet marks up to 3x), so you can read where a spray lands at range.

## Sound

- **Every sound has 2–6 variants.** Each play picks a different one, with a little random pitch and volume, so repeats never sound identical.
- **Gunshots** have a mid "bark", a short boom (sub-bass is cut so they don't sound like the bass is turned up) and a slapback echo off the walls. Rifle, pistol and sniper each have their own sound.
- **Bots shooting from more than ~1,400 units away** sound distant: muffled, no crack, mostly echo.
- **Footsteps** are boot-on-grit (heel, toe scuff, crunch), and landing has a bit of gear rattle.
- **You are silent below 135 u/s.** Shift-walk (~112) and crouching make no footsteps; running does, like CS. Deathmatch bots hear your footsteps.
- **Positional footsteps:** moving dummies and bots make footsteps that are panned and attenuated by distance. Close your eyes and point at them.
- **Volume** is `volume` in `config.cfg` (0..1).
- **Sounds are synthesized at startup.** To listen to them as files, run `feellab.exe --dump-sounds <folder>` and it writes every variant as a WAV.


## Bots, smokes, wallbangs, KZ

- **Bots shoot back (F4).** Any dummy that can see you reacts after a random 0.25–0.55 s and fires every 0.22–0.38 s, with about 0.8° of random aim error. They aim at where you were 0.2 s ago, so strafing and counter-strafe peeks dodge them, while standing still in the open gets you killed. Smokes block their vision.
- **Smokes (G):**
  - they pop 1.6 s after the throw and last 15 s
  - same throw = same landing spot every time
- **Wallbangs:**
  - rifle and sniper bullets go through thin walls (rifle 24 units, sniper 40) and lose damage; the pistol can't
  - the hit log shows `WALLBANG`
  - try the peek wall: the dummies behind the solid part can be shot through it
- **KZ course:** start on the green pad behind the spray wall and hop the blue pads over the lava to the yellow pad. The gaps get wider, so the last ones need bhop speed. The timer runs at the top of the screen and keeps your best time. Touching lava sends you back to the start.

## Bunny hopping

`bhop 1` is on by default:
- Hold Space (or spam the wheel) and you jump the instant you land, so ground friction never touches you.
- There's no stamina slowdown.
- Air-strafe to gain speed: hold A while turning left (or D while turning right), without W.

Perfect hops keep 250 u/s, and good strafes build to 500+. Set `bhop 0` for CS-style anti-bhop: one jump per press, and landing slows you down.

## Sniper

- **One-shot body kill:** 115 damage (it's a leg shot that doesn't kill).
- **Bolt action:** 1.46 s between shots, 5 rounds.
- **Movement:** half speed while scoped.
- **No crosshair unscoped,** like CS, so noscopes are a skill rather than luck. There's still no random spread.
- **Scoped sensitivity** is scaled so flicks feel the same as unscoped (`zoom_sensitivity_ratio`, default 1).

## Zero-lag camera

`camera_extrapolate 1` is on by default. The simulation runs at 128 ticks, so a normal interpolated camera shows your position up to one tick (7.8 ms) in the past. This one draws you where you are *now*: the latest tick plus the time since, with collision and step-up checks. Movement keys show up on screen with no added delay. Set it to 0 to compare.

## Smooth stairs

`view_smooth_steps 1` is on by default. Walking up or down a step moves you up to 18 units in one tick. With this on, the camera eases over the step in about 50 ms instead of jumping. It's purely visual: your movement, hitbox and timing are unchanged, and your shots come from exactly where the camera is. Set it to 0 to compare.

## No random spread

**There is no random bullet spread by default.** Every bullet goes exactly to your crosshair plus the weapon's fixed recoil pattern, so sprays are 100% repeatable. Movement still matters: the recoil pattern and the view kick are the same either way, but nothing randomizes your shots.

For a CS-like comparison, set `spread_spray 1` (spread grows during a spray) and/or `spread_movement 1` (inaccurate while moving or in the air) in `config.cfg`, then press F5.

## Tuning numbers (CS:GO-like defaults)

| | Value |
|---|---|
| Rifle / knife speed | 215 / 250 u/s |
| Walk / crouch | 52% / 34% of max |
| Jump | 57 units apex, ~675 ms airtime (gravity 1000: snappier than CS:GO) |
| Friction / accelerate / air accelerate | 5.2 / 5.5 / 12 |
| Rifle | 600 RPM, 30 rounds, 36 dmg (×4 head) |
| Pistol | semi-auto, 400 RPM max, 12 rounds, 35 dmg (×4 head), strong climbing kick |
| Spray reset | ~0.2 s after a tap, ~1.0 s after a full spray |

These live in `src/movement.h` (`MoveParams`) and `src/combat.cpp` (`kRifle`, `kRiflePattern`).

Weapon model settings in `config.cfg`, all CS-style:
- `viewmodel_fov` (default 68)
- `viewmodel_offset_x/y/z`
- `viewmodel_bob` (0 turns bob off)
- `show_viewmodel 0` hides the weapon

## Build it yourself

You need CMake 3.24+ and a C++20 compiler (Visual Studio 2022 on Windows). SDL3 is downloaded automatically.

```
cmake -S prototype -B build
cmake --build build --config Release
build/Release/feellab.exe        # Windows (Visual Studio generator)
build/sim_tests                  # headless movement/weapon tests (any OS)
```

## Code map

| File | What |
|---|---|
| `src/movement.*` | Kinematic player movement: friction/accel, air strafing, step-up, crouch-jump, stamina |
| `src/world.*` | Maps made of axis-aligned boxes (Feel Lab, Dust2 from a grid of named areas), swept-box / ray traces, broadphase |
| `src/nav.*` | Bot navigation on the Dust grid: walkable / roamable cells, shortest routes, following a route |
| `src/bots.*` | Deathmatch bot brains: spawning anywhere, roaming, sight cone, hearing, chasing (tested headless) |
| `src/combat.*` | Rifle, recoil pattern, deterministic spread, dummies and hitboxes, hit detection |
| `src/render.*`, `src/gl.*` | OpenGL 3.3: every 3D object is an instanced box; the HUD is a single batch |
| `src/fx.*` | First-person weapon models and animation, tracers, particles (cosmetic only) |
| `src/audio.*` | SDL3 audio mixer and procedural sound synthesis |
| `src/main.cpp` | Window, raw input, 128 Hz fixed-step loop with interpolation, bots (peek + deathmatch AI), HUD |
| `tests/sim_tests.cpp` | Headless checks: speeds, jump height, counter-strafe timing, collision, stairs, crates, determinism, Dust routes and run times, deathmatch spawns and bot walking, turned hitboxes |

Shots are tested against exactly what was on your screen the frame you clicked: the interpolated dummy positions, facings and camera. That's the single-player version of "what you see is what you hit".
