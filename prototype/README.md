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
| Space / mouse wheel | Jump (one jump per press; no auto-hop) |
| Ctrl | Crouch (crouch in the air = crouch-jump) |
| Shift | Walk |
| Mouse 1 | Fire |
| R | Reload |
| 1 / 2 / 3 | Rifle / pistol (semi-auto) / knife (knife = faster movement) |
| V | Noclip (fly where you look; Shift = slow) |
| F6 | Reset to spawn |
| F5 | Reload `config.cfg` live (sensitivity, crosshair, viewmodel, volume, spread) |
| C | Clear bullet decals |
| F3 | Aim drill: killed range dummies respawn at random spots, and the HUD shows time-to-kill (last + average) |
| F1 | Toggle help |
| Alt+Enter | Toggle fullscreen |
| Esc | Pause (Q to quit while paused) |

## The map

- **Ahead (the range):** static dummies at about 13 / 26 / 39 / 52 m, and two strafing dummies. One stops at each end like a counter-strafer; one runs straight through.
- **Left:**
  - crates of 32, 64 and 80 units (the 64 needs a crouch-jump; the 80 is out of reach)
  - stairs to a platform
  - a wall with a doorway, with dummies behind it to practise jiggle-peeking
- **Right:** a dark spray wall. Stand on the small marker and spray. Bullet decals go **yellow → red** through the magazine, so you can see the pattern.

## Sound

- **You are silent below 135 u/s.** Shift-walk (~112) and crouching make no footsteps; running does, like CS.
- **The two strafing dummies on the range and the one behind the doorway** make footsteps that are panned and attenuated by distance (audible to ~1600 units). Close your eyes and point at them.
- **Volume** is `volume` in `config.cfg` (0..1).
- **Sounds are synthesized at startup.** To listen to them as files, run `feellab.exe --dump-sounds <folder>` and it writes WAVs.

## What the HUD tells you

- **FPS / 1% LOW / MS:** average FPS, the 1% low (99th percentile frame time) and average frame time, updated every 0.5 s.
- **SPEED / SPREAD / ACCURATE:** horizontal speed, your current bullet spread in degrees, and `ACCURATE` (green) when you're slow enough for a perfect first shot (≤ 34% of rifle speed, ~73 u/s).
- **LAST STOP:** how long your last stop took from full speed to accurate. It's labelled COUNTER-STRAFE if you tapped the opposite key, RELEASE if you just let go. Expect about **78 ms** counter-strafing vs **~200 ms** releasing.
- **Hit log (top right):** hitgroup, damage, kill, distance in metres.

## No random spread

**There is no random bullet spread by default.** Every bullet goes exactly to your crosshair plus the weapon's fixed recoil pattern, so sprays are 100% repeatable. Movement still matters: the recoil pattern and the view kick are the same either way, but nothing randomizes your shots.

For a CS-like comparison, set `spread_spray 1` (spread grows during a spray) and/or `spread_movement 1` (inaccurate while moving or in the air) in `config.cfg`, then press F5.

## Tuning numbers (CS:GO-like defaults)

| | Value |
|---|---|
| Rifle / knife speed | 215 / 250 u/s |
| Walk / crouch | 52% / 34% of max |
| Jump | 57 units apex, ~750 ms airtime |
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
| `src/world.*` | Grey-box map (axis-aligned boxes) and swept-box / ray traces |
| `src/combat.*` | Rifle, recoil pattern, deterministic spread, dummies and hitboxes, hit detection |
| `src/render.*`, `src/gl.*` | OpenGL 3.3: every 3D object is an instanced box; the HUD is a single batch |
| `src/fx.*` | First-person weapon models and animation, tracers, particles (cosmetic only) |
| `src/audio.*` | SDL3 audio mixer and procedural sound synthesis |
| `src/main.cpp` | Window, raw input, 128 Hz fixed-step loop with interpolation, HUD |
| `tests/sim_tests.cpp` | Headless checks: speeds, jump height, counter-strafe timing, collision, stairs, crates, determinism |

Shots are tested against exactly what was on your screen the frame you clicked: the interpolated dummy positions and camera. That's the single-player version of "what you see is what you hit".
