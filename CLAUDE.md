# CLAUDE.md: Project Constitution

Read this first, every session. It's the operating manual for any AI working in this repo: what the game is, the
rules, how to build/verify/ship, everything that exists and where, who the owner is and what they want, and the
practical know-how for this machine. **Keep it current**: when you learn something a future session would need
(an owner preference, a trap, a tool), write it here.

## 1. What this project is

A CS:GO-like tactical shooter where **shooting, movement and positioning are the top focus**. Targets:
- extremely lightweight, aiming for 700+ FPS on high-end PCs
- trustworthy hit registration

There are two parts:
- `docs/`: research and plan (player research, design, tech stack, netcode, performance budget, build roadmap). Read `README.md`, then the docs relevant to your task.
- `prototype/`: **Crisp** (the game, formerly "Feel Lab"), a C++20 / SDL3 / OpenGL 3.3 game: offline modes with bots and online play with friends. `prototype/README.md` documents every control, system and config key. **Keep it up to date.**

**The motto is CRISPY. It must feel like CS:GO.** When in doubt, pick whatever is more responsive, more consistent and more CS-like.

## 2. The owner and how they test

- The owner plays on a **Windows laptop (Ryzen 7 4800H, integrated Radeon only: GPU/fill-rate bound)**.
- They get builds from **GitHub Actions** (every push touching `prototype/**` builds the artifact `Crisp-windows-x64`),
  from `Desktop\Crisp-v<version>.zip`, or by running **`build\Release\crisp.exe` directly** (they do this: see §10,
  that folder's `config.cfg` is theirs).
- **After every change you push, give them the run's link** (`https://github.com/MartyJCross/CSreplacement/actions/runs/<id>`; the artifact is at the bottom), and say clearly what changed and what they should test.
- **Versions:** `project(Crisp VERSION x.y.z)` in `prototype/CMakeLists.txt` is shown on the main menu and in the
  window title. Bump it for every build you hand over (patch for fixes, minor for features). For a local zip, build
  Release, then `powershell -ExecutionPolicy Bypass -File prototype/tools/make_zip.ps1` (writes `Desktop\Crisp-v<version>.zip`).
- Be honest about what you verified (tests, screenshots, measurements) versus what only they can judge: feel, sound, FPS on their hardware.
- More about them, their preferences and their history in §9.

## 3. Repository layout

```
docs/                     research + plan (markdown)
prototype/
  CMakeLists.txt          builds crisp_sim (lib), sim_tests (headless), crisp (game; CRISP_BUILD_GAME=ON)
  src/
    vecmath.h             Vec3/Mat4, Z-up, Source-style angles (pitch + = down, yaw + = left)
    world.*               AABB world, traces (+ broadphase); buildLab(); the town maps (Dust2, Harbor) from named areas
                          on a 32u grid: buildTown(), townGrid(), spawns, retake sites, CT spots, prefire routes, peek
                          spots, TownTactics (bot plays), doors, facades; KZ course constants
    movement.*            128-tick kinematic player movement (MoveParams = all movement tuning)
    combat.*              weapons (WeaponId: rifle = AK-47, pistol, knife, grenade, sniper = AWP, Berettas, Deagle, Nova,
                          MAC-10, M4A1-S, Galil, SSG 08, UMP-45, XM1014; weaponDef; armorRatio per gun; `scope`), recoil
                          patterns, shotgun pellets (firePellets, random per shot), wallbangs, collats, armor; dummies +
                          hitboxes (yaw, crouch, model scale; shots test in the dummy's model space using
                          shownYaw/shownCrouch); grenade throw/flight/prediction
    stats.*               your career: MatchRecord, Career (rating: Elo vs bot level, ranks), stats.txt
    replay.*              replays: Replay ring (64 Hz, 150 s) of ReplayFrame/ReplayAgent + ReplayShot; sample()
    items.*               skins (allSkins), rarities and CS odds, rollCase, Inventory (inventory.txt)
    nav.*                 NavGrid on the town grid: walkable/roamable cells, Dijkstra routes, followPath
    bots.*                bot brains (spawn anywhere, roam or walk to a goal, hold, view cone, pick a target,
                          hearing, chase, cover, skill); main.cpp only feeds them BotSenses, so tests run bots headless
    main.cpp              window/input, 128 Hz fixed-step loop, the modes, grenades, bot shooting, competitive (plays,
                          economy, bomb), KZ, HUD, menus, map loading, replays/killcam, stats, online glue (~6k lines)
    render.*, gl.*        OpenGL 3.3: everything is an instanced box (optionally yaw-rotated: yawBox) or a ModelDraw
                          (boxes under a matrix); HUD is one batch; muzzle light uniform; tiny GL loader (+ fences)
    fx.*                  cosmetic only: first-person weapons/knives (weaponDraws), the inventory showcase, tracers,
                          particles, debris (helmets)
    net.*                 online play: ENet host/join, relay through the host, state/fire/hit/death/name/grenade
                          messages; competitive: the host's bots, match state, round start/end, plant/defuse
    upnp.*                hosting: miniupnpc opens the router's port in a background thread, finds the addresses
    audio.*               SDL3 mixer: recordings from assets/ or synthesized fallbacks, variants, 3D (ITD, head shadow,
                          occlusion), the suppressed guns built from the AK recording, streamed menu music
    config.*              config.cfg load/save (the in-game menus write it)
    font.h                GENERATED by tools/gen_font.py: edit the script, not the header
  tests/sim_tests.cpp     headless tests (~all systems; see rule 7)
  assets/                 sounds/ (<sound>_<n>.wav|.ogg replace that sound's synthesized variants; m4_shot_N, the AR-15
                          base), music/menu.ogg, textures/ (512 px detail maps), CREDITS.md; copied next to the exe
  third_party/            stb_vorbis.c, stb_image.h (public domain / MIT; built as crisp_third_party, no warnings)
  tools/                  gen_font.py, make_zip.ps1, prep_assets.py (builds assets/ from ../asset-downloads),
                          sound_bands.py (measures dumped sounds: bands, length, ringing, 3D cues)
.github/workflows/prototype.yml   CI: Windows build + tests + artifact; Linux sim tests (GCC)
../asset-downloads/       (outside the repo) the downloaded CC0 packs + SOURCES.md; prep_assets.py reads them
```

## 4. Golden rules (do not break these)

1. **The simulation is deterministic and fixed at 128 ticks.** Movement, weapons and hit detection live in the shared sim code and run per tick (`kTickDt`). Never use frame time in gameplay logic. Cosmetics (`fx.*`, audio, camera, replays' playback) may use frame time.
2. **The player's bullets have no randomness by default.** Shots go to crosshair + fixed recoil pattern (`spread_spray` / `spread_movement` config toggles exist). Bots may use randomness (`rnd(g)` in `main.cpp`), but the player's shots must not. **Owner's exception (v0.14):** shotgun pellets spread at random (seeded by `shotCounter`, so a given shot is reproducible: `pelletOffset`).
3. **What you see is what you hit.** Shots test against the dummy positions and eye that were rendered on the frame you clicked (`lastDummyRenderPos`, `lastRenderEye`). Cosmetic parts of enemies must sit inside their hitboxes (and scale with them: `modelScale()`). Weapons held by enemies are the only exception, like CS.
4. **Performance budget.** No per-frame heap allocations in hot paths (reuse vectors: `static`/member buffers), no runtime shader compilation, minimal draw calls (the world is about 4 instanced draws). New visuals should reuse the box renderer.
5. **Only CC0 / royalty-free assets, credited, with a fallback; never copyrighted content.** Recordings, music and textures live in `prototype/assets/` (built by `tools/prep_assets.py` from `../asset-downloads`, described in its `SOURCES.md`) and each is listed in `assets/CREDITS.md`. The game must still run without them: missing sounds fall back to the synthesized ones in `audio.cpp`, missing textures to the procedural surfaces in the box shader, missing music to silence. Never use Valve/CS assets. **Owner exception (personal testing only):** the Dust map deliberately copies Dust2's layout, scale and callouts (shown as "DUST"). It is built from code (no extracted map files). Revisit before anything is shared publicly; Harbor is Crisp's own map and is free to share.
6. **Keep it portable to MSVC.** C++20, standard library and SDL3 only. No GCC/Clang-only extensions. Code must compile warning-clean with `-Wall -Wextra -pedantic` (Linux/GCC: CI runs it; check locally, §10) and `/W4` (MSVC).
7. **Every gameplay change needs a test.** If you change movement or weapon numbers, update or add checks in `tests/sim_tests.cpp`, and keep the printed measurements meaningful. Most tests use `buildLab()`. Don't move the Lab's geometry that the tests rely on (spawn lane y≈0, back wall x=-512, stairs, crates, peek wall, spray wall). The town tests check bot spots, routes with time windows, sightlines, holes and bot trips (`testBotGoals`, `testDustRoutes`, `testDustScales`, `testDustSightlines`, `testMapGaps`, `testHarbor`). If you change an area table, keep those passing and look at the printed run times.
8. **Settings belong in the config and the menu.** A new tunable gets a `Config` field, load + save in `config.cpp`, the default config text, and if player-facing a row in `menuRows()` (the right settings page) in `main.cpp`.

## 5. Build and test

**On Windows (this machine; see §10 for the exact cmake path):**
```
cmake -S prototype -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
build\Release\sim_tests.exe
build\Release\crisp.exe
```

**On Linux (CI / cloud containers):**
```
# Tests only (fast, no SDL):
cmake -S prototype -B build-tests -DCRISP_BUILD_GAME=OFF && cmake --build build-tests && ./build-tests/sim_tests
# Full game (needs X11 + GL dev headers, e.g. mesa-common-dev libgl-dev libx11-dev libxext-dev):
cmake -S prototype -B build -G Ninja -DSDL_WAYLAND=OFF -DSDL_X11_XCURSOR=OFF -DSDL_X11_XINPUT=OFF \
  -DSDL_X11_XRANDR=OFF -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XFIXES=OFF -DSDL_X11_XSHAPE=OFF \
  -DSDL_X11_XTEST=OFF -DSDL_X11_XSYNC=OFF -DSDL_X11_XDBE=OFF
cmake --build build
```

**Screenshots** (work on Windows with the real GPU; on Linux under Xvfb + Mesa):
`crisp --windowed 1280 720 --screenshot out.bmp --frames N [options]`. The map/mode come from `config.cfg` (`map 0|1|2`,
`mode`, `fullscreen 0`). Screenshot mode runs a fixed 240 FPS timestep (frame N = N/240 s of game time; the FPS
counter is meaningless). Convert BMP to PNG (Pillow) to look at it, and **look before claiming a visual change works**.
Options: `--spawn X Y YAW` (world coords: Dust is scaled by `dust_scale`!), `--weapon 1..14` (4 AWP, 5 grenade,
6 Berettas, 7 Deagle, 8 Nova, 9 MAC-10, 10 M4A1-S, 11 Galil, 12 SSG 08, 13 UMP-45, 14 XM1014), `--zoom 1|2`,
`--autofire START END` (seconds), `--inspect N`, `--buy N` (the wheel on category N), `--smoke --nade T
[--throw-frame N]`, `--bots`, `--menu N [--menu-row R]` (a `MenuScreen` index: 11 inventory, 12 case with
`--give-cases N`, 13 stats with `--career FILE`), `--die N [--killer I]` (killcam when I is a bot),
`--replay-frame N`, `--ct`, `--host`, `--join ADDR`.

**Measuring:** `--bench 10` (bench.txt: avg/1% low, time per part, GPU via glFinish), `--bench-raw 10` (no glFinish:
frames queue like play; use it for latency-mode comparisons), `--dump-sounds DIR` (synthesized), `--dump-played-sounds
DIR` (what the game plays, recordings and derived sounds included), `--dump-spatial DIR` (a footstep from each
direction as stereo WAVs). Analyse sounds with `python prototype/tools/sound_bands.py "DIR/*.wav"` (`--spatial DIR`).

**Bot behaviour runs:** competitive with `--bots --frames 60000` (≈4 min of game) writes `comp_log.txt` (rounds,
plays, throws, STUCK bots). The idle test player can't die, so **its side wins almost every round**: run once with
`--ct` too before judging balance. `comp_enemies 4` / `comp_mates 4` for a fair 4v4.

## 6. The GitHub process

1. **Branch:** `claude/festive-hypatia-q2q1fd` (the working branch) unless the owner names another. Remote
   `https://MartyJCross@github.com/MartyJCross/CSreplacement.git`. Don't push to other branches or create PRs unless asked.
2. **Before pushing:** build the game (warning-free), run `sim_tests` (all checks pass), run the GCC warning check
   (§10), re-read your diff.
3. **Commit messages:** a one-line summary, then bullets covering what changed and why; end with the attribution line
   the session asks for.
4. **Push**, then wait for the **"Crisp prototype"** workflow (windows job: build, tests, artifact; linux-tests job).
   No `gh` here: poll `https://api.github.com/repos/MartyJCross/CSreplacement/actions/runs?branch=claude/festive-hypatia-q2q1fd&per_page=1`
   with curl until the head commit's run is `completed` (a background `until` loop).
5. **If CI fails, fix it and push again.** Never hand over a red build.
6. **Report to the owner:** the run link, what changed (in their terms), what you verified, what they should try.
   Then make the zip (§2).

## 7. Current feature set

See `prototype/README.md` for full details. Each item lists where its code lives.

**Movement** (`MoveParams` in `movement.h`): gravity 1000, accelerate 6.48 (owner: 10% under 7.2), friction 5.6, air
accelerate 12; 57-unit jump, crouch-jump reaches 64-unit crates; bhop off by default (`bhop` 1 = hold-to-hop, no
stamina); real ramps (`Box::slope`/`lowZ` wedges, swept Quake-style). After any spawn a held Space must come up
before it jumps again (`Game::jumpNeedsRelease`); no jumps while input is blocked.

**Weapons** (`combat.cpp`): CS slots (1 primary, 2 pistol, 3 knife, 4 grenades, 4 again cycles; Q previous, F inspect,
G quick-throw). Guns: AK-47 (`kWRifle`), M4A1-S, Galil AR, AWP (`kWSniper`), SSG 08, Nova, XM1014, MAC-10, UMP-45,
pistol (suppressed USP-like), Dual Berettas, Deagle; knife; grenades.
- Rules the owner set: no "tagging" slowdown; guns only when they ask; the starting pistol must not one-tap a helmet;
  the Deagle must from any range; the AK and M4A1-S do (M4A1-S up close), the Galil doesn't; the AWP kills through
  kevlar with a body shot (`armorRatio` 0.975); all tested in `testNewGuns`.
- `Game::guns[kWeaponCount]` (by WeaponId; `weaponState(g, id)`). A new gun = WeaponDef + enum entry, a ViewWeapon +
  parts in fx.cpp (+ paintSpan, muzzle), a world model (the dummies loop `switch (d.weapon)`), a buy entry, skins + an
  inventory row (`kInvWeapons`), its sound (`gunshotPitch` + the shot branch), `items.h kSlots == kWeaponCount`.
- Snipers (`WeaponDef::scope`): a shot drops the scope and it comes back (`resumeZoom`), the viewmodel cycles the bolt,
  zoom eases in (`shownFov`), `Sfx::Zoom`; noscope spread only with `spread_movement`.
- Buy wheel (B; `drawBuyWheel`, `buySlotAt`, `buyPick`, `BuyCategory`: pistols, shotguns, SMGs, rifles, snipers, gear,
  grenades; full buys on 8 / 9: `fullBuy`). Competitive: in spawn during buy time for money; elsewhere free.
- Armor `armoredDamage(dmg, group, armor, helmet, ratio)`; material wallbangs (wood ×0.5, metal ×1.5); **collats**
  (traceShot: guns with penetration go through bodies, `kBodyThickness` 8, up to 3; `ShotResult::collat`,
  `collatResults` turns them into ordinary hits); fire timing `takeShotTiming` (tested).
- Reloads are 20% faster than CS (owner, v0.15); the reload animation and sound cues stretch to `reloadTime`.
- Shot "oomph" (cosmetic): viewmodel kick, muzzle flash, `fovPunch`, sounds. The owner likes the feel: change it only when asked.

**Sound** (`audio.cpp`): recordings (CC0 firearm library: AK, pistol, bolt rifles, 12-gauge shotguns, AR-15 base)
levelled to the synth they replace; everything else synthesized. **Every gun has its own recording** (v0.21:
`gunSound(id)` in main.cpp = Sfx + pitch + your gain, used by your shots, bots, online and replays via `gunshot3D`;
Sfx GalilShot (SKS), Mac10Shot (PPSh), UmpShot (Carl Gustav M45), SsgShot (Savage 10, Marlin), XmShot (CD, Mossberg),
DeagleShot (S&W 642, 1911), BerettasShot (PPQ, Bersa); without files `kGunFallbacks` in audio.cpp repitches a
shared sound). The **suppressed guns** (`suppressedFrom`, `kUspS`, `kM4A1S`)
are built from the top of the AK recording + gas fizzle + (M4A1-S) supersonic crack + noise clacks for the action:
<500 Hz ~1%, no tonal layers (see §9 for why). 3D audio (`spatialize`, `mixVoice`: ITD up to 0.65 ms, head shadow,
behind/below darker, above brighter, occlusion via `setOcclusion` with two rays: muffled, -10 dB). Menu music
(`loadMusic` on a thread, `setMusic` fades, `music_volume`; plays while `g_menu.root == kMenuMain`). Impacts by
material, helmet tink, near-miss whiz. The owner's ear: guns "too much bass" earlier; keep sub-bass cut.

**Cosmetics** (`fx.cpp`): knives (default, butterfly, karambit, M9, talon, bowie, kukri); keyframed inspects; spray
feedback; grenade models; particles; helmet debris. **Skins and cases** (`items.*`, main.cpp inventory block): painted
per pixel by the box shader (`makePainted`, `PaintParams`); a case per `case_kills` (25) kills in modes 1/2/3/5;
INVENTORY (`kMenuInventory`) and the case reel (`kMenuCase`); `all_skins` unlocks everything. Save keys are stable
(the M4A1-S keeps "M4A4_..." keys).

**Maps** (Play screen MAP; config `map` 0 Lab, 1 Dust, 2 Harbor; `Game::mapId` is 1 for both town maps):
- The Lab: range, spray wall, crates, stairs, peek wall, KZ bhop course.
- **Town maps** (world.cpp): named floor areas on a 32u grid (`DustArea` tables `kDustAreas`, `kHarborAreas`;
  `TownDef kTowns`: extents, scalable, wall shades, `separate` pairs). `setTownMap(0|1)` picks one; every `town*`
  function answers for it (townGrid, townPoint (real map coords → world, on the floor), townCallout, townSpawn,
  townTeamSpawns, townRetakeSites, townCtSpots, townPrefireRoutes, townPeekSpots, townTactics, buildTown). Walls,
  roofs (tunnels/doorways), trims, windows, awnings and doors are generated. `separate` keeps a wall between area
  pairs whose gap is under a cell at some sizes (Dust: CT mid/CT spawn vs short, CT mid vs catwalk).
- **Dust** (the Dust2 copy): scaled by `dust_scale` (60 default, the owner plays at 95); props `anchored()` to area
  edges so they stay against walls at every size. Tests use `dpt(x, y)` (real-Dust2 coords, scaled).
- **Harbor** (Crisp's own map, its own size): docks → raised A site, market/mid/mid doors, roofed warehouse → A short,
  roofed underpass and alley → B with a back platform. `testHarbor` checks its 99 spots, 70 bot trips, spawn
  visibility, holes and lane times.
- Doors (end of buildTown): frames + leaves on the doorway's cells (`cellsOf`, `frame`, `openLeaf`). Facade decor
  (`World::decor`) is drawn, never collided: keep it thin/flat or above head height. Surfaces by material alpha in the
  box shader (240 stone, 236 paving, 224 wood, 208 metal; the Lab keeps the 255 dev grid).

**Modes** (Play screen MODE, `mode`): 0 practice (peek bots), 1 deathmatch, 2 retakes, 3 competitive 5v5, 4 prefire,
online (Play → ONLINE; mode 5 DM or 3 with `Game::online`).
- Deathmatch: bots roam the map (`bots.cpp`), fight each other too (`dm_bot_fights`; `BotSenses::self/huntYou`), spawn
  out of sight (`pickDmSpawn`); a kill heals 40 and gives 10 rounds.
- Competitive (`Game::Comp`, `startCompRound`/`compTick`/`endCompRound`): MR12, CS economy, buy wheel, bomb
  carry/drop/plant/defuse, freeze time (`freeze_time` 15).
- **Bots use the whole arsenal** (v0.21; sim lib bots.cpp, tested in `testBotArsenal`): `teamBuyRound` (pistol, eco,
  force, full from the side's average money; force when there's no next round) and `botBuy` per role (0 = AWPer;
  T AK, CT M4A1-S; forces Galil/MAC-10/UMP/Nova/XM/scout; pistol rounds a Deagle and Berettas; kept guns stay and get
  swapped up on a full buy). `Comp::botGun` (WeaponId) is what each holds (`d.weapon`). Bot shots use the gun:
  `damageAt` (range falloff), armorRatio, `botShotGap` cadence, pellets, `gunSound`. Deathmatch bots get
  `deathmatchBotGun` each life; you count as kevlar + helmet outside competitive. comp_log.txt lists the buys.
- **Smarter comp bots** (v0.21, compTick): CT utility (`Comp::ctNade`: molotov on full buys, HE on forces, thrown
  with `botThrow` at Ts seen 350-1300 units away during an execute); post-plant (compPlanted sends every T bot to the
  site's holds, off the bomb; `defuseHeard`: they rush the bomb when a defuse starts); saving (`Comp::saving`: lost
  round = no time to defuse/plant or 1 vs 3+, a gun worth $1700+ -> back to spawn, never the defuser). Bots step out
  of fire (`BotSenses::fires`, `fireExit`, waits at the edge; `testBotFire`).
- T plays (`CompPlay`: execute, rush, split A, split B,
  fake, default) with staging points and execute utility from `townTactics()` (`Comp::throws` + `botThrow` solving
  throws with `predictGrenade`), called on the radio (`teamRadio`); CT setups by role (`townCtSpots`: mid, short, long,
  A, B), pushes (`Comp::pushers`), rotations (`rotated`), urgency (`BotBrain::urgent`), trading (alerts within 600).
  Bots never freeze (`pathTo` snaps unreachable goals, `nearestRoamable`; `backHome`); automated runs log STUCK.
- Bot skill (`bots.h BotSkill`, `skillAt` blends levels): mate/enemy skill, `skill_variance` (0/1/2: ±0.5 or ±1 level
  per bot per match, `botSkillOf`). Reaction restarts only on losing sight; slower after a quiet spell (`surprise`).
- Bot cover (`findCover`): hidden spot + peek spot; fights alternate peek/cover (`testBotCover`).
- Retakes (`townRetakeSites`), prefire (`townPrefireRoutes`, `startPrefire`, frozen bots, timed).

**Players** (main.cpp, the dummies loop): models with stepping legs, vest, pouches, gloves; CT helmet + goggles, T
balaclava + beanie; every piece inside its hitbox; hits flash per part. **Model size** (`model_scale`, default 120 per
the owner; `setModelScale`, `modelScale`, `dummyEyeZ` in combat): hitboxes, crouchZ, bot eyes/aim and the drawn model
scale together; your own hull doesn't (their heads sit above your eye line at 120%). Use `dummyEyeZ()` for any
character-height code, never a bare 64. Tests run at 1.0. The dead topple along `Dummy::hitDir` (`deadDraws`), a
headshot kill knocks the CT helmet off (`knockHelmet`); a kill flashes white and holds ~80 ms first (`killFlash`).

**Feedback & HUD**: kill feed, per-life damage report, Tab scoreboard (ADR only in round modes 2/3), radar with
spotting, hitmarker + tick, damage-direction arcs, competitive spectating (`spec`, `nextTeammate`) and radio. Muzzle
light (`muzzle_brightness` 0.45 default, walls only, `Renderer::setFlash`, shader `flashLit`; `muzzleLight()` on every
shot). Grenades: smoke, flash (`flashBang`), HE, molotov; CS:GO throw physics shared with the preview and bots.

**Replays & killcam** (`Game::replay`, `rv`, `recordReplay` every other tick, `replayShot`, `stepReplay`,
`startReplayViewer`): the renderer swaps the recorded dummies/smokes/fx in for the world pass and restores them. The
killcam (`killcam`) starts from `hurtPlayer` when a bot kills you; a click skips it. The viewer (pause menu) freezes
the sim. `loadMap` clears it all.

**Stats** (`stats.*`; `g_career`, `recordMatch`, `recordDeathmatch`; `kMenuStats`, `drawStatsPanel`): finished offline
DM/competitive matches vs bots → stats.txt; rating = Elo vs the bot level (easy 700 .. expert 1450), ranks RECRUIT ..
GENERAL; career ADR counts competitive only.

**Menus** (`MenuScreen`, `menuRows`, `drawMenu`, `menuUse`): main menu (PLAY, INVENTORY, STATS, SETTINGS, CONTROLS),
pause menu (+ WATCH REPLAY), Play screen (mode, map, its options), settings pages (mouse, crosshair, weapons, video +
sound, gameplay). Every change saves config.cfg.

**Performance & latency**: depth pre-pass, per-frame frustum cull + front-to-back sort, per-face lighting. Nav routes
are A* (`NavGrid::findPath`, octile estimate). The mixer only locks to take new voices. v0.21 check on the owner's
laptop (1080p, MSAA 2, DM, 7 bots): ~200 FPS, GPU-bound (gpu ~2.8 ms, sim ~0.15 ms), MSAA 0 gives ~275; the mixer
~0.6% of a core (bench.txt reports it: `--bench` plays audio); muzzle light and model scale cost nothing measurable. Low latency
mode (`low_latency` 1: a GL fence after each swap, waited on before reading input; ~17% FPS for 1-2 frames less lag on
the owner's laptop). Measure with `--bench 10` / `--bench-raw 10` before and after rendering changes.

**Online** (stages 1-3 done): listen host relays; every client simulates itself and sends its state each tick
(`NetState`); remote players are `dummies[netId]` played back 6 ticks behind; the shooter decides hits and the victim
applies them (trusting clients: friends only). Ids: players 0..7 (host 0), host's bots 8..17 (`isBot`, `netHost`,
`netClient`, `netToLocal`). Grenades sent (`sendNade`), each game hurts only its own player (host: its bots).
Competitive: host runs `compTick`/bots/`netRoster`, sends `NetRound`, `NetMatch` (8 Hz), `NetBot` (64 Hz); clients run
`compClientTick`. The welcome carries map size and (game byte: bit 0 game, bits 1+ map) the town map.
`kNetProtocol` (8) must match. Test with two copies in separate folders: `crisp --host` and `crisp --join 127.0.0.1`.

**v0.22 polish** (main.cpp unless noted): `matchCues` (Sfx RoundStart/BombPlanted/RoundWin/RoundLose), round banner +
MVP (`Comp::mvp/mvpWhy/plantedBy/defusedBy`), match-end screen (phase 3, 15 s; `--rounds N` ends matches sooner for
tests), CS top bar (`kTeamColor`), kill feed icons (`FeedEntry` killer/victim/weapon, `drawGunIcon` from `gunModel`,
`drawSkull`), kill toast, LowAmmo/LastRound sounds, side rifles (`buyEntries(cat, side)`, `buySide`, `fullBuyRifle`),
2 flashes in full buys, drops (`Game::Drop`, `dropGun`, `pickUp`, `dropsTick`, E = `useLatch`; net kDrop/kPickup;
`--drop W`), calls (`playerCall`, Z, `--call N AT`), bullet holes (`bulletHole`, `makeBulletHole`: alpha 16, shrink
in the VS by `uTime`), contact shadows (world.cpp: decor with `kMatShade`, alpha 30..37; blobs `makeBlobShadow` 46..49;
`layOnRamp`, sheets in the VS), animation (Dummy `shotAt/reloadUntil/mag/busy/air/landedAt`; bots reload; net flags
kNetReload/kNetDuck), lobby (`kMenuLobby`, `LobbyState g_lobby`, net kLobby/kTeamPick/kStart/kBye, `kNetLobbyOpen`
in the welcome; reconnect; `netNotice`; `--host-lobby F`). kNetProtocol 10.

## 8. Roadmap

**IN PROGRESS: v0.23 "premium pass"** (owner asked for all of it, 2026-10-09; done in pieces, each committed and pushed
so a cut-off session can carry on; light testing, no benches while they use the PC). Tick each piece off here:
- [x] 1. Sun shadows (`Renderer::buildShadows`: static depth map from `kSunDir` at map load, `sunVis`/`sunLit` in the
      box FS, config `shadows` 0/1/2 + menu row) and bevelled prop edges (wood/metal faces, box FS)
- [x] 2. Post pass (`setPost`, `endScene`, `setGrade` per map in loadMap; kPostFS): scene into an FBO (MSAA samples
      there, not on the window), then FXAA (when msaa 0) + colour grade + vignette; config `post_fx` (restart)
- [x] 3. Real font (`Renderer::loadFont`, `g_ttf` in render.cpp): Rajdhani Bold (OFL, assets/fonts) baked by
      stb_truetype at scales 1,2,3,4,6,8,12 into a 2048 atlas; capitals as tall as the bitmap font's; lines with
      double spaces (padded columns) keep fixed cells; bitmap font.h is the fallback
- [x] 4. Living main menu (`menuCamera`: high dolly shots over the CT spots, 11 s each, through black via
      `g_menuFade`; light veil + letterbox in drawMenu) and fades in from black (`g_fadeFrom`, `drawFadeIn`) on map
      load and competitive round start
- [ ] 5. Map ambience (per-map loops) + room echo (reverb in tunnels/roofed areas)
- [ ] 6. Footsteps by surface (stone/sand, wood, metal)
- [ ] 7. Viewmodel polish: smoother sway/bob, landing dip, strafe tilt
- [ ] 8. Graphics presets (LOW/MEDIUM/HIGH set shadows, post, msaa)
- [ ] 9. Docs: README section, CLAUDE.md §7, final report + CI link + zip

Done in v0.21: bots use the whole arsenal, distinct gun sounds, smarter competitive bots (CT utility, post-plant,
saving, fire avoidance), the performance check (A* routes, mixer lock).
Offered, not chosen yet: T molotovs on executes, bots dropping guns for teammates, CT retake utility, pick up / drop guns (G), server-authoritative netcode with lag compensation (`docs/04`),
practice tools (grenade lineups, spray-pattern overlay, CS-style console), a third map, ladders, net_graph.

## 9. The owner: who they are, what they want, the history

- Casual, direct; writes numbered lists of changes, often with typos ("lap" = map?). When something is ambiguous, pick
  the most plausible reading, do it, and **say your interpretation in the report** so they can correct it.
- They test by playing: feel, sound and look are their verdicts. Say clearly what only they can judge.
- They get frustrated when the same thing misses twice ("man..."): **research first, measure, then change**, and
  explain the why in one or two sentences.
- **NO MID FIGHTS FROM SPAWN** (asked many times; fixed for real in v0.22.1): competitive bots never stage in, push into,
  route through or chase into mid before the execute (`townMidArea`, `townMidCells` + `NavGrid::setAvoid`, T stages
  off mid, CT mid spots out of sight of mid). `testNoMidFights` and the automated-run log ("MID DEATH", must stay 0)
  guard it. Never add a plan, spot or push in mid again.
- Bots must shoot when they see someone: automated runs log "STALL" (a bot that saw its enemy 2.5 s without firing;
  must stay 0).
- Standing preferences: CS:GO feel above all; no tagging slowdown; guns only when they ask; no gimmicks (rejected
  "import CS crosshair codes"); the exact Dust2 copy is fine for personal testing; bhop off by default (they play with
  it on); reloads 20% faster than CS; model size 120%; muzzle light on walls only at 0.45; killcam skips on a click
  (Space made them jump on spawn); ADR only where there are rounds; random shotgun spread (their exception to rule 2);
  menu music only on the main menu; they loved the AK sound.
- **Suppressor sound history** (don't repeat it): v0.13 deep "thwump" ok-ish → v0.14 "make it a bit tinny, like
  Bond" → v0.15 "still not it" (we gave them a sound lab; they tuned it) → v0.16 "too thumpy, more whispy" → v0.18
  "doesn't sound like a gun, like tapping glass with metal" (tonal metal-ring + "zip" layers) → v0.19 removed rings
  but kept the AK body → "WAY less thumpy" → v0.20, from research: a suppressor removes the muzzle blast (<500 Hz)
  and leaves gas fizzle, the supersonic crack (rifles) and the action. Rules now: no body below 500 Hz, no pitched
  layers, a mid "pop", short. Measure with `tools/sound_bands.py` before handing over.
- Their network: double-NATed (a "Clark-Wifi" router behind the main router; public IP was 102.220.210.229). Online
  with their friend goes over **ZeroTier**: their personal network `88c5b1f339cbaf72` (their IP 10.30.83.82, range
  10.30.83.0/24, firewall rule "Crisp over ZeroTier" for UDP 27015). They also use ZeroTier **for work (network
  `abfd31bd470bd01a`): never touch that one**.
- Their email is for identification only (commit authorship); never send it anywhere.

## 10. Session know-how (this machine)

- **Shells:** Git Bash (POSIX) and PowerShell. Paths: the repo is
  `C:\Users\matth\Desktop\Personal\Development\cs replacement\CSreplacement` (note the space: quote it).
- **cmake** isn't on PATH: `"/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"`
  (VS 2026 Build Tools, MSVC 14.51; generator "Visual Studio 18 2026" -A x64; build dir `build/`, gitignored).
  Re-run the configure step after adding source files to CMakeLists.txt.
- **`build/Release/config.cfg` is the owner's own config** (they run `build\Release\crisp.exe`). Before screenshots or
  automated runs that need other settings, copy it to the scratchpad, edit, run, and **restore it**. Their settings
  include `dust_scale 95`, `bhop 1`, `model_scale 120`, `mode 1`, `map 1`.
- **Never edit C/C++ through a Bash heredoc**: the shell layer turns `\n` inside string literals into real newlines
  (broke builds many times). Write a Python edit script with the Write tool into the scratchpad and run it (assert
  each `old` text is present before replacing), or use the Edit tool. If a split literal slips through, rejoin it.
- **GCC warning check** (rule 6) via WSL Ubuntu (g++ 13): write a small script that runs
  `g++ -std=c++20 -Wall -Wextra -pedantic -ffp-contract=off -fsyntax-only -I. <file>` over the sim sources and
  sim_tests from `/mnt/c/.../prototype/src`, then `MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/<script>` (without
  MSYS_NO_PATHCONV Git Bash mangles `/mnt/...`; inline `bash -c` loses `$f`).
- **PowerShell scripts** need `-ExecutionPolicy Bypass`. `gh` isn't installed (CI via curl, §6).
- **Python** with numpy and Pillow is available (BMP → PNG, sound analysis). Downloaded files are untrusted: keep them
  in their own folder (`../asset-downloads/...`), never run anything from there.
- **Assets:** add a download to `../asset-downloads` + `SOURCES.md`, a step in `prep_assets.py` (+ the CREDITS text),
  run `python prototype/tools/prep_assets.py`; existing files come out byte-identical, so `git status` shows only the
  new ones. The firearm library (`sounds/opengameart/firearm-library/Prepared SFX Library/<gun>/`) has many unused guns
  (SKS, PPSh, Carl Gustav, Marlin, Model 1894, Mosin, Bersa, Ruger, S&W, Savage...) for distinct gun sounds.
- **Verifying by kind of change:** gameplay numbers → sim_tests; visuals → screenshots (look at them); sounds →
  `--dump-played-sounds` + `tools/sound_bands.py` (compare against the real recordings); 3D audio →
  `--dump-spatial` + `sound_bands.py --spatial`; maps → the town tests + screenshots at the owner's size; bots →
  `comp_log.txt` runs on both sides; performance → `--bench 10` / `--bench-raw 10`.
