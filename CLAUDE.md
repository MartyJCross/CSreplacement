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
- `prototype/`: **Crisp** (the game, formerly "Feel Lab"), a C++20 / SDL3 / OpenGL 3.3 game: offline modes with bots and online play with friends. `prototype/README.md` documents the game as it is now: every control, system and config key. **Keep it up to date.** `prototype/CHANGELOG.md` has each version's notes (newest first): add one for every version you hand over.

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
  README.md, CHANGELOG.md the game as it is now; what changed per version
  src/
    vecmath.h             Vec3/Mat4, Z-up, Source-style angles (pitch + = down, yaw + = left)
    world.*               AABB world, traces (+ broadphase grid, walked row by row along a trace: forSweep); buildLab(); the town maps (Dust2, Harbor) from named areas
                          on a 32u grid: buildTown(), townGrid(), spawns, retake sites, CT spots, prefire routes, peek
                          spots, TownTactics (bot plays), doors, facades; KZ course constants; inferno_hand.h = Inferno's hand-built areas
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
                          economy, bomb), KZ, HUD, menus, map loading, replays/killcam, stats, online glue (~8k lines)
    render.*, gl.*        OpenGL 3.3: everything is an instanced box (optionally yaw-rotated: yawBox) or a ModelDraw
                          (boxes under a matrix); HUD is one batch (TrueType font atlas); muzzle light uniform; static
                          sun shadow map; post pass (the window's picture copied, then FXAA, grade, vignette,
                          outlines from the alpha channel's distance code); tiny GL loader (+ fences, FBOs)
    fx.*                  cosmetic only: first-person weapons/knives (weaponDraws), the inventory showcase, tracers,
                          particles, debris (helmets); DrawList (model draws that keep their buffers between frames)
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
  third_party/            stb_vorbis.c, stb_image.h, stb_truetype.h (public domain / MIT; built as crisp_third_party)
  tools/                  gen_font.py, make_zip.ps1, prep_assets.py (builds assets/ from ../asset-downloads),
                          sound_bands.py (measures dumped sounds: bands, length, ringing, 3D cues)
.github/workflows/prototype.yml   CI: Windows build + tests + artifact; Linux sim tests (GCC)
../asset-downloads/       (outside the repo) the downloaded CC0 packs + SOURCES.md; prep_assets.py reads them
```

## 4. Golden rules (do not break these)

1. **The simulation is deterministic and fixed at 128 ticks.** Movement, weapons and hit detection live in the shared sim code and run per tick (`kTickDt`). Never use frame time in gameplay logic. Cosmetics (`fx.*`, audio, camera, replays' playback) may use frame time.
2. **The player's spray is deterministic; spread only where the owner asked for it.** Shots go to crosshair + fixed
   recoil pattern, plus (v0.23.3, owner) spread that is always on: moving above `accurateSpeedFrac`, airborne, noscoped
   (`currentInaccuracy`; the `spread_movement` option was removed), and a small standing spread on the cheaper guns
   only (`standingSpread`: USP, Berettas, Galil, MAC-10, UMP; the AK, M4A1-S, AWP, SSG 08 and Deagle are exact when
   still). The SSG 08 is exact at a jump's apex (v0.23.4: `kApexSpeed` 40, `WeaponState::vertSpeed`). All of it is seeded by `shotCounter` (reproducible). `spread_spray` (random spray spread) stays an option,
   off. Shotgun pellets spread at random (v0.14, `pelletOffset`). Bots use `rnd(g)` in `main.cpp`.
3. **What you see is what you hit.** Shots test against the dummy positions and eye that were rendered on the frame you clicked (`lastDummyRenderPos`, `lastRenderEye`). Cosmetic parts of enemies must sit inside their hitboxes (and scale with them: `modelScale()`). Weapons held by enemies are the only exception, like CS.
4. **Performance budget.** No per-frame heap allocations in hot paths (reuse vectors: `static`/member buffers;
   per-frame model draws go in a `DrawList`), no runtime shader compilation, minimal draw calls (the world is about 4 instanced draws). New visuals should reuse the box renderer.
5. **Only CC0 / royalty-free assets, credited, with a fallback; never copyrighted content.** Recordings, music and textures live in `prototype/assets/` (built by `tools/prep_assets.py` from `../asset-downloads`, described in its `SOURCES.md`) and each is listed in `assets/CREDITS.md`. The game must still run without them: missing sounds fall back to the synthesized ones in `audio.cpp`, missing textures to the procedural surfaces in the box shader, missing music to silence. Never use Valve/CS assets. **Owner exception (personal testing only):** the Dust map deliberately copies Dust2's layout, scale and callouts (shown as "DUST"). It is built from code (no extracted map files). **Inferno** (v0.29, owner: "build Inferno from CS2, as accurate and
   detailed as possible") is under the same exception: its layout, heights and callouts were measured from CS2's radar
   and the community nav mesh (awpy-data) as references, and the area table `inferno_hand.h` was placed by hand
   against them (v0.30); no Valve file ships. Revisit both before anything is shared publicly; Harbor is
   Crisp's own map and is free to share.
6. **Keep it portable to MSVC.** C++20, standard library and SDL3 only. No GCC/Clang-only extensions. Code must compile warning-clean with `-Wall -Wextra -pedantic` (Linux/GCC: CI runs it; check locally, §10) and `/W4` (MSVC).
7. **Every gameplay change needs a test.** If you change movement or weapon numbers, update or add checks in `tests/sim_tests.cpp`, and keep the printed measurements meaningful. Most tests use `buildLab()`. Don't move the Lab's geometry that the tests rely on (spawn lane y≈0, back wall x=-512, stairs, crates, peek wall, spray wall). The town tests check bot spots, routes with time windows, sightlines, holes and bot trips (`testBotGoals`, `testDustRoutes`, `testDustScales`, `testDustSightlines`, `testMapGaps`, `testHarbor`, `testInferno`,
`testSpawnsOffMid`, `testCompWays`). If you change an area table, keep those passing and look at the printed run times.
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
`crisp --windowed 1280 720 --screenshot out.bmp --frames N [options]`. The map/mode come from `config.cfg` (`map 0|1|2|3`,
`mode`, `fullscreen 0`). Screenshot mode runs a fixed 240 FPS timestep (frame N = N/240 s of game time; the FPS
counter is meaningless). Convert BMP to PNG (Pillow) to look at it, and **look before claiming a visual change works**.
Options: `--spawn X Y YAW` (world coords: Dust is 90% of real Dust2 in the game!), `--weapon 1..14` (4 AWP, 5 grenade,
6 Berettas, 7 Deagle, 8 Nova, 9 MAC-10, 10 M4A1-S, 11 Galil, 12 SSG 08, 13 UMP-45, 14 XM1014), `--zoom 1|2`,
`--autofire START END` (seconds), `--inspect N`, `--buy N` (the wheel on category N), `--smoke --nade T
[--throw-frame N]`, `--bots`, `--menu N [--menu-row R]` (a `MenuScreen` index: 11 inventory, 12 case with
`--give-cases N` / `--give-knife-cases N`, 13 stats with `--career FILE`), `--skin KEY` (that skin equipped; with
`--menu 11` on the stage), `--inspect N --inspect-variant V` (a knife's inspect V), `--die N [--killer I]` (killcam when I is a bot),
`--replay-frame N`, `--ct`, `--host`, `--join ADDR`, `--light K` (K steps of 1/240 s a frame, capped at 30 FPS: easy on
the owner's PC; **use it for every automated run on their machine**, owner v0.31: "limit FPS as much as you can").

**Measuring:** `--bench 10` (bench.txt: avg/1% low, time per part, GPU via glFinish), `--bench-raw 10` (no glFinish:
frames queue like play; use it for latency-mode comparisons), `--dump-sounds DIR` (synthesized), `--dump-played-sounds
DIR` (what the game plays, recordings and derived sounds included), `--dump-spatial DIR` (a footstep from each
direction as stereo WAVs). Analyse sounds with `python prototype/tools/sound_bands.py "DIR/*.wav"` (`--spatial DIR`).

**Bot behaviour runs:** competitive with `--screenshot X.bmp --bots --light 16 --frames 3750` (≈4 min of game in ~2;
without `--screenshot` the game opens its menu with music on the owner's screen) writes `comp_log.txt` (rounds,
plays, group go times, retakes, throws, and three must-stay-0 counts: STUCK (a bot frozen 8 s), STALL (saw its enemy
2.5 s without firing), STACK (two teammates within 64 units, both moving, 2 s in a round)). The idle test player can't die, so **its side wins almost every round**: run once with
`--ct` too before judging balance. `comp_enemies 4` / `comp_mates 4` for a fair 4v4.

## 6. The GitHub process

1. **Branch:** `claude/festive-hypatia-q2q1fd` (the working branch) unless the owner names another. Remote
   `https://MartyJCross@github.com/MartyJCross/CSreplacement.git`. Don't push to other branches or create PRs unless asked.
2. **Before pushing:** build the game (warning-free), run `sim_tests` (all checks pass), run the GCC warning check
   (§10), re-read your diff.
3. **Docs:** update `prototype/README.md` (the current game), add the version's notes to `prototype/CHANGELOG.md`, and
   keep this file's §7 current.
4. **Commit messages:** a one-line summary, then bullets covering what changed and why; end with the attribution line
   the session asks for.
5. **Push**, then wait for the **"Crisp prototype"** workflow (windows job: build, tests, artifact; linux-tests job).
   No `gh` here: poll `https://api.github.com/repos/MartyJCross/CSreplacement/actions/runs?branch=claude/festive-hypatia-q2q1fd&per_page=1`
   with curl until the head commit's run is `completed` (a background `until` loop).
6. **If CI fails, fix it and push again.** Never hand over a red build.
7. **Report to the owner:** the run link, what changed (in their terms), what you verified, what they should try.
   Then make the zip (§2).

## 7. Current feature set

See `prototype/README.md` for full details. Each item lists where its code lives.

**Movement** (`MoveParams` in `movement.h`; v0.25: a scroll-wheel notch in the air waits `kWheelJumpBuffer` 0.15 s
for the ground (`Game::wheelJumpAt`), a jump within `hopGrace` 6 ticks of landing is a hop (`PlayerState::sinceLand`,
`landedKeep`) that keeps the landing speed less `hopPenalty` 0.25 of the landing slowdown; holding Space never hops
without `bhop 1`; run
speed `moveSpeedFor`: scoped AWP 100, SSG 08 230, crouched no scope slowdown; bots run at their gun's maxSpeed): gravity 1000, accelerate 6.48 (owner: 10% under 7.2), friction 5.6, air
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
  zoom eases in (`shownFov`), `Sfx::Zoom`; a noscope always scatters (`noscopeInaccuracy`).
- Refunds (v0.27.2, owner): `Comp::bought` (Purchase: loadout before/after, price), `compRefund` undoes the newest
  buy while the loadout still matches its after and the gun is unfired; R with the wheel open / the REFUND button
  (`kBuyRefund`); cleared at round start; `--refund-test N` prints a full buy then N refunds. Q (`lastWeapon`) only
  goes to a weapon you still have.
- Drops (v0.28): G in competitive drops the primary you hold (`dropPrimary`; `Game::noPickupUntil` 1 s), else
  quick-throws; Z -> NEED A DROP (`kCallTDrop` / `kCallCtDrop`, 8 calls a side): the richest teammate bot with the
  money buys `fullBuyRifle` and drops it between itself and you. `--comp-money N` (tests: everyone's starting money).
- Buy wheel (B; `drawBuyWheel`, `buySlotAt`, `buyPick`, `BuyCategory`: pistols, shotguns, SMGs, rifles, snipers, gear,
  grenades; full buys on 8 / 9: `fullBuy`). Competitive: in spawn during buy time for money; elsewhere free.
- Armor `armoredDamage(dmg, group, armor, helmet, ratio)`; material wallbangs (wood ×0.5, metal ×1.5); **collats**
  (traceShot: guns with penetration go through bodies, `kBodyThickness` 8, up to 3; `ShotResult::collat`,
  `collatResults` turns them into ordinary hits); fire timing `takeShotTiming` (tested).
- Semi-auto clicks are buffered (`TriggerBuffer`, `kClickBuffer` 0.25 s, v0.23.2): a click while the gun cycles fires
  when it's ready, so spam-clicking reaches the real fire rate (owner: firing slowly for accuracy is the skill).
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

**Cosmetics** (`fx.cpp`): knives (default, butterfly, karambit, M9, talon, bowie, kukri); keyframed gun inspects;
knife animations (v0.27: `KnifePose`, `knifeInspect` three a knife cycled by F (`inspectVariant_`), `knifeDraw`,
`knifeFidget` idle tricks (`knife_fidget`); the knife has its own transform `Anim::knifeOff/knifeSpinX/knifeSpinZ` so
tosses leave the hand); spray feedback; grenade models; particles; helmet debris. **Skins and cases** (`items.*`,
main.cpp inventory block): v0.27 part by part: every gun part has a `Part::region` (items.h `SkinRegion`: body, top,
barrel, muzzle, fore, stock, grip, mag, scope, accent; knives blade, handle by colour in `partBox`); a `SkinDef` is up
to 4 `SkinLayer`s (pattern, 3 colours, gloss, scale) and a 12-char region string (layer digit or '-' plain); the box
shader gets them as uniform arrays (`setPaint`) and reads the region from the painted box's alpha (56 + region);
26 patterns (`Pattern`, render.cpp `skin()`). A case per `case_kills` (25) kills, a knife case per `kKillsPerKnifeCase`
(100) kills, in modes 1/2/3/5; INVENTORY (`kMenuInventory`: case, knife case, then a row per weapon) and the case reel
(`kMenuCase`, `CaseOpening::knife`); `all_skins` unlocks everything. Save keys are stable (the M4A1-S keeps
"M4A4_..." keys; keep every old key when redesigning). Automated runs never save inventory.txt. v0.28 (owner): every
skin factory new (`paintFor` sets wear 0; the float only seeds the pattern; rolls under 0.07); StatTrak
(`Item::st/kills`, `Equipped` = `Item`, `kStatTrakOdds` 0.1, "st KILLS" at the end of an inventory line;
`countCaseKill(g, weapon)` counts your kills by the weapon's name; `PaintParams::stKills` -> fx.cpp `addKillCounter`, a
seven-segment counter on `gunBody(w)`'s left side; `--stattrak N` with `--skin`).
**Knife attacks** (v0.28, combat.cpp `knifeAttack`, tested in `testKnife`): CS:GO's numbers (slash 40/25/90 at 48
units, stab 65/180 at 32, kevlar x0.85, `knifeRefire`), rays fanned 8 degrees round the aim, backstab when the victim
faces away (dot > 0.475); main.cpp handles the hit like a bullet's (Mouse 1 slash, Mouse 2 stab: `stabLatch`,
`knifeComboUntil`); fx.cpp `knifeSwing` (forehand / backhand alternate, `ViewModel::knifeAttack`); Sfx KnifeSwing,
KnifeHit, KnifeWall; `--stab N`.

**Maps** (Play screen MAP; config `map` 0 Lab, 1 Dust, 2 Harbor, 3 Inferno; `Game::mapId` is 1 for every town map;
`setTownMap(map - 1)`):
- The Lab: range, spray wall, crates, stairs, peek wall, KZ bhop course.
- **Town maps** (world.cpp): named floor areas on a 32u grid (`DustArea` tables `kDustAreas`, `kHarborAreas`;
  `TownDef kTowns`: extents, scalable, wall shades, `separate` pairs). `setTownMap(0|1)` picks one; every `town*`
  function answers for it (townGrid, townPoint (real map coords → world, on the floor), townCallout, townSpawn,
  townTeamSpawns, townRetakeSites, townCtSpots, townPrefireRoutes, townPeekSpots, townTactics, buildTown). Walls,
  roofs (tunnels/doorways), trims, windows, awnings and doors are generated. `separate` keeps a wall between area
  pairs whose gap is under a cell at some sizes (Dust: CT mid/CT spawn vs short, CT mid vs catwalk).
- **Dust** (the Dust2 copy): always 90% of real Dust2 in the game (`kDustSize` in main.cpp; owner v0.25.2 removed the
  setting; `setDustScale` still serves the tests' sizes and an online host's); props `anchored()` to area
  edges so they stay against walls at every size. Tests use `dpt(x, y)` (real-Dust2 coords, scaled).
- **Harbor** (Crisp's own map, its own size): docks → raised A site, market/mid/mid doors, roofed warehouse → A short,
  roofed underpass and alley → B with a back platform. `testHarbor` checks its 99 spots, 70 bot trips, spawn
  visibility, holes and lane times.
- **Inferno** (v0.30, rebuilt by hand after the owner rejected the generated v0.29: "a mess"; `setTownMap(2)`,
  `inferno_hand.h`: `kInfernoAreas` ~180 hand-placed areas at CS2's real size and coordinates, `TownDef::cell` 16,
  "WALL" areas carve pillars / inner walls, `kInfernoDecks` (`TownDeck`: the T apps bridge, bottom mid's floor over the
  underpass: solid walkways, the grid keeps the street), `kInfernoSmallProps` with heights). How it was built (redo it
  this way): scratchpad tools render CS2's radar + the community nav mesh (awpy-data navs.zip, measured at 8 u, grown 16
  u to the walls) under the hand table, a cell-by-cell comparison (green right height, red wrong, blue missing, magenta
  extra; ~77% match, the rest one-cell edges), height profiles along strips; spots snapped onto the game's own nav
  (`sim_tests` writes `inferno_nav.txt` when a file `CRISP_DUMP` exists). Nav on a 16 u grid: a ramp cell may rise 26
  (`MapGrid::slope`, both cells ramps), ledge drops two cells on (`NavGrid::canDrop`), the clear check needs ~92 of
  headroom (roofs at least 100 over walkways). Its look (`g_town == 2`): terracotta caps and eaves, stone footing,
  shutters + flower boxes, cobbles (`kIStreet` paved), B's brick (`kIBrick`), `minWallTop` 520. Ways APPS (gathers in
  close apps, through BALCONY) / SHORT / BANANA; B has one way, so no split / mid control there. `testInferno`: holes,
  slivers / lone cells, spots, trips, spawn sight, rays, lane times and player walks that must not catch (0.75 s).
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
- Bot utility (v0.28): `Comp::Throw::afterPlant`; execute molotov onto `townCtSpots(kCtA/kCtB)[0]` (siteUtility, group
  0, a rifle); retake flashes (first two CTs sent, a rifle, target bomb + 150 z, `botThrow(..., maxErr 350)`); a molotov
  on the bomb when the Ts hear a defuse; a bot's defuse stops > 60 units off the bomb. comp_log names every throw.
- T plays (v0.26; `CompPlay`: execute, rush, split, fake, default, mid control) over **ways** onto the sites
  (`townTactics().ways`, `TownWay`: stage, smoke, flash, the area its route crosses, `fromMid`; Dust long, short,
  tunnels, lowers, mid to B; Harbor docks, short, underpass, alley; `waysTo`). The round's T groups (`Comp::stages`,
  `stageOf`, `groupWay`) each go at `groupGoAt` so a split's groups hit together (the longer way first, up to 8 s),
  each throwing its way's utility (`Comp::throws` + `botThrow` solving throws with `predictGrenade`); called on the
  radio (`teamRadio`). CT setups by role (`townCtSpots`: mid, short, long, A, B), pushes (`Comp::pushers`: long, mid
  or B), rotations (`rotated`), urgency (`BotBrain::urgent`), trading (alerts within 600).
- No stacking (v0.26): `keepGap` (bots.cpp, `BotSenses::mates`, `kBotGap` 110: wait up to 1 s behind a teammate just
  ahead), `BotBrain::via` (`sendBot(..., via)`), `spreadEntries` (a site's `RetakeSite::entries` spread over the bots;
  a taken one costs 1500 more). CT retakes (v0.26.1): each CT goes straight through its way in (`Comp::retakeEntry`,
  via), the ones with a shorter trip holding where they are until `retakeAt` (at most 4 s); no meeting point (one walked
  back to spawn and stood there). After a plant a CT saves only if it can't make it or it's 1 vs 4. Rotations come by
  different ways. Ways are picked weighted (`pick` in startCompRound: not from mid 3, from mid 1; owner: "7/10 they
  all went mid"); automated runs log "Ts through mid before the execute". Tests: `testBotSpacing`, `testRetakeSpread`.
- Preaim / checking spots (v0.26, v0.31): walking (and in a short hold) bots check the visible angles one after
  another (`preaim`, `BotSenses::angles` from `Game::angles[side]`, built in loadMap: the known holds first
  (`keyAngles`), then `NavGrid::hidingSpots` corners and spots behind crates), head height, ~0.5 s on a hold, 0.3 s on a
  corner (`BotBrain::checkedSpot` ring, 6 s memory), else round the route's next corner; teammates split them through
  `SpotClaim` (`Game::looked[side]`, `BotSenses::looked`). An enemy spotted within 10 degrees of the aim
  (`BotBrain::sightOff`) gets 65% of the reaction time (`testBotPreaim`, `testBotChecks`).
- Real aim (v0.31, bots.cpp; `testBotAim` prints an AK duel per skill, range and movement, old vs new): the bot's
  crosshair (d.yaw/d.pitch) is moved by `steer` (a spring: `BotSkill::flick` stiffness, damping 0.7, `lead` follows the
  target's angular speed) towards `seenAt` (perception lag, half of `lagTicks`) + `aimErr` (wanders, 3x..1x by
  `focus`/`focusTime`); reaction in the sim (`react`, `seenFor`, noticing = 35% of it); `botReady` gates fire,
  `botShotDir` = crosshair + recoil not pulled down (`control`) + standing spread, `botNextShot` sprays close / bursts /
  taps. main.cpp uses it in the town modes 1-4 (`realAim`); practice's peek bots and the Lab keep the old model.
  v0.32 (owner: "over tuned... hard to even counter" up close): reactions as a person's (expert 0.21-0.36, floor
  0.18, pre-aimed x0.8), a re-peek after 0.2 s out of sight = 70% of a reaction (floor 0.15), `BotBrain::crossing`
  (fast angular movement) cuts `lead` (x40/(40+c)) and grows the aim error; `testBotPeek` prints peek / re-peek times.
- Spawns (v0.32, owner): `spawnSpots` (bots.cpp; `Game::spawnSpots`, built in loadMap: the five `townTeamSpawns` + up to
  ten found in the spawn areas, checked out of mid's / the other side's sight and a 520-unit run from mid), shuffled
  per round in startCompRound (you too; `netSpawn` indexes it). `assignNearest` puts Ts in the play's groups and CTs on
  the setup's spots by where they spawned (`testSpawnGroups`; `testSpawnsOffMid` checks every spot).
  Bots never freeze (`pathTo` snaps unreachable goals, `nearestRoamable`; `backHome`); automated runs log STUCK.
- Bot skill (`bots.h BotSkill`, `skillAt` blends levels): mate/enemy skill, `skill_variance` (0/1/2: ±0.5 or ±1 level
  per bot per match, `botSkillOf`). Reaction restarts only on losing sight; slower after a quiet spell (`surprise`).
- Bot cover (`findCover`): hidden spot + peek spot; fights alternate peek/cover (`testBotCover`).
- Retakes (`townRetakeSites`), prefire (`townPrefireRoutes`, `startPrefire`, frozen bots, timed).

**Players** (main.cpp, the dummies loop): models with stepping legs, vest, pouches, gloves; CT helmet + goggles, T
balaclava + beanie; every piece inside its hitbox; hits flash per part. **Model size** (`model_scale`, default 102 (v0.23.1: owner asked 15% smaller than 120) per
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
killcam (`killcam`) starts from `hurtPlayer` when a bot kills you; a click skips it. Until it starts (or you respawn) the
death view (`Game::deathView`, `deathEye`) holds the camera where you fell: no glimpse of the respawn. The viewer (pause menu) freezes
the sim. `loadMap` clears it all.

**Stats** (`stats.*`; `g_career`, `recordMatch`, `recordDeathmatch`; `kMenuStats`, `drawStatsPanel`): finished offline
DM/competitive matches vs bots → stats.txt; rating = Elo vs the bot level (easy 700 .. expert 1450), ranks RECRUIT ..
GENERAL; career ADR counts competitive only.

**Menus** (`MenuScreen`, `menuRows`, `drawMenu`, `menuUse`, `menuBack`; v0.25 restructure): main menu (PLAY, PLAY
ONLINE, INVENTORY, STATS, SETTINGS, QUIT); PLAY = the modes (`kPlayModes`, `kModeAbout` line under the menu) ->
`kMenuSetup` (map, size, the mode's options, START; `kActPickMode`); `kMenuOnline` (name, HOST A GAME -> lobby, address,
JOIN, port); SETTINGS pages mouse, crosshair, weapons, video, sound (`kMenuSound`), gameplay, controls; pause menu
(RESUME, NEW GAME, replay, inventory, settings, main menu). New screens are appended to the enum (`--menu N` numbers:
15 setup, 16 online, 17 sound). No testing rows in the menus (owner); dev readouts only in practice; `show_fps`. SETTINGS -> RESET TO DEFAULTS
(`kActResetSettings`, press twice within 3 s) = a fresh `Config` keeping name, address, port, all_skins.
Every change saves config.cfg.

**v0.23 premium pass** (details in §8's checklist): sun shadows (render.cpp `buildShadows`, `kSunDir`), post pass
(`setPost`/`endScene`/`setGrade`; v0.25.4: the scene is drawn on the window, then copied for the post pass), TrueType HUD font
(`loadFont`, assets/fonts, OFL), main-menu camera (`menuCamera`), fades (`drawFadeIn`), ambience + room echo (audio.cpp
`ambienceBed`, `Reverb`, `setAmbience`/`setRoom`), stone footsteps (`Sfx::FootstepStone`), viewmodel spring sway /
lean, graphics presets (`applyGraphicsPreset`).

**Performance & latency**: depth pre-pass, per-frame frustum cull + front-to-back sort, per-face lighting. Nav routes
are A* (`NavGrid::findPath`, octile estimate). The mixer only locks to take new voices. v0.21 check on the owner's
laptop (1080p, MSAA 2, DM, 7 bots): ~200 FPS, GPU-bound (gpu ~2.8 ms, sim ~0.15 ms), MSAA 0 gives ~275; the mixer
~0.6% of a core (bench.txt reports it: `--bench` plays audio); muzzle light and model scale cost nothing measurable. Low latency
mode (`low_latency` 1: a GL fence after each swap, waited on before reading input; ~17% FPS for 1-2 frames less lag on
the owner's laptop). Traces walk the broadphase along the trace (`World::forSweep`, v0.25.3): a long ray on Dust
~2.3 us (was ~4.6; brute force ~8; `testDustBroadphase` prints it). Measure with `--bench 10` / `--bench-raw 10`
before and after rendering changes. **The owner's AMD iGPU (driver 27.20.14028) is slow at offscreen rendering**
(v0.25.4, measured at 1080p, DM on Dust): drawing the scene into an FBO cost 2.6 -> 4.7 ms of GPU (MSAA 4: 3.0 ->
8.9 ms), whatever the colour/depth format, and depth blits cost 2-4.5 ms. So the scene stays on the window and the
post pass copies the colour (alpha carries the outline code): their settings (stylized, post, MSAA 4, sharp shadows)
went 90 -> 137 FPS. The post pass still costs ~1.6 ms (copy + shader); the depth pre-pass gains nothing measurable
there. Not tried yet (needs a bench): `drawModel`/`uploadDynamic` orphan the whole
4096-box dynamic buffer (196 KB) on every model draw; orphaning only what's drawn might help the integrated GPU.

**Online** (stages 1-3 done): listen host relays; every client simulates itself and sends its state each tick
(`NetState`); remote players are `dummies[netId]` played back 6 ticks behind; the shooter decides hits and the victim
applies them (trusting clients: friends only). Ids: players 0..7 (host 0), host's bots 8..17 (`isBot`, `netHost`,
`netClient`, `netToLocal`). Grenades sent (`sendNade`), each game hurts only its own player (host: its bots).
Competitive: host runs `compTick`/bots/`netRoster`, sends `NetRound`, `NetMatch` (8 Hz), `NetBot` (64 Hz); clients run
`compClientTick`. Sounds are heard from `Game::earPos/earYaw` (the camera: your eyes or the spectated
player's; set each frame). Spectating draws `Game::specVm` (their weapon, shots via `shotAt`, reloads, throws from
`nades` owner); dead free-fly: no gun, Space/Ctrl up/down, your body frozen while spectating. The welcome carries map size and (game byte: bit 0 game, bits 1+ map) the town map.
`kNetProtocol` (11, net.h) must match. Test with two copies in separate folders: `crisp --host` and `crisp --join 127.0.0.1`.

**v0.22 polish** (main.cpp unless noted): `matchCues` (Sfx RoundStart/BombPlanted/RoundWin/RoundLose), round banner +
MVP (`Comp::mvp/mvpWhy/plantedBy/defusedBy`), match-end screen (phase 3, 15 s; `--rounds N` ends matches sooner for
tests), CS top bar (`kTeamColor`), kill feed icons (`FeedEntry` killer/victim/weapon, `drawGunIcon` from `gunModel`,
`drawSkull`; v0.23.6: hand-drawn silhouettes on a 10-unit grid, `gunModel` detailed), kill toast, LowAmmo/LastRound sounds, side rifles (`buyEntries(cat, side)`, `buySide`, `fullBuyRifle`),
2 flashes in full buys, drops (`Game::Drop`, `dropGun`, `pickUp`, `dropsTick`, E = `useLatch`; net kDrop/kPickup;
`--drop W`), calls (`playerCall`, Z, 8 a side: `kCallsPerSide`; TAKE MID, PUSH MID since v0.26.1, NEED A DROP v0.28; `--call N AT`), bullet holes (`bulletHole`, `makeBulletHole`: alpha 16, shrink
in the VS by `uTime`), contact shadows (world.cpp: decor with `kMatShade`, alpha 30..37; blobs `makeBlobShadow` 46..49;
`layOnRamp`, sheets in the VS), animation (Dummy `shotAt/reloadUntil/mag/busy/air/landedAt`; bots reload; net flags
kNetReload/kNetDuck), lobby (`kMenuLobby`, `LobbyState g_lobby`, net kLobby/kTeamPick/kStart/kBye, `kNetLobbyOpen`
in the welcome; reconnect; `netNotice`; `--host-lobby F`). kNetProtocol 10 (11 since v0.32: spawn spot indices).

## 8. Roadmap

**Done in v0.32** (owner, 2026-10-12): bots fairer up close (human reactions, re-peeks), random spawns, plays by
spawn.
**Done in v0.31** (owner, 2026-10-12: bots "actually aim + look towards hotspots of the maps as they move"; chatter
not wanted): real aim, checking spots, `--light`. Offered, not chosen: personalities, body language (crouch spray,
shift-walk, flash stagger).
**Done in v0.30** (owner, 2026-10-12: "build it by hand", with a plan of how to make it good): Inferno rebuilt by
hand on a 16 u grid with decks, ramp collision cap (`kRampPlanes` 7), nav ledge drops, staggered group sends.
**Done in v0.29**: Inferno (CS2's) generated from measurements; rejected by the owner (spikes, gaps, apps unreachable).

**Done in v0.28** (owner, 2026-10-11): knife attacks, bot utility, drops (G + NEED A DROP), StatTrak, every skin
factory new (v0.27.2 before it: refunds, the butterfly redone side on, a Q fix). Known: an occasional STUCK in
`--comp-money 16000` T runs (a T in UPPER TUNNELS, round 2, bomb dropped) that predates v0.28; not tracked down yet.

**Done in v0.26** (owner, 2026-10-11: "double down on the bot AI"): doorways one cell wider on both maps; the
competitive MAP row's labels (showed THE LAB / DUST for Dust / Harbor); mid open, real splits, short / lowers / mid-to-B
plays, mid control; no stacking (gaps, spread retakes and rotations); preaim and quicker pre-aimed shots.

**Done in v0.24** (owner, 2026-10-11: spawns away from mid; "clean stylized" art direction chosen from three
offered; more detail on the guns, especially the viewmodel). Pieces, each committed and pushed:
- [x] 1. Spawns off mid: walls between T spawn and mid with a dog-leg "MID PATH" (Dust, Harbor), CT spawn behind a wall
      with side gaps (Dust) / split CT ramp (Harbor), spawn points moved; DM never spawns in mid (`pickDmSpawn`).
      `testSpawnsOffMid`: no spawn seen from mid at any size, shortest run to mid > 500 real units (528)
- [x] 2. Clean stylized look (`art_style`, `Renderer::setStyle`, `uStyle` in the box FS, `uOutline` in kPostFS):
      outlines (v0.25.4: 1 px, just outside silhouettes, from `edgeCode` = log2 distance in the frame's alpha; the
      gun writes 0 via `uNoEdge`; no creases; was 2 px black from a depth copy that cost the owner ~0.5-4.5 ms),
      textures at 18%, rich shade (`sunLit` stylized branch), wall height gradient, saturation x1.18 / contrast x1.25
- [x] 3. Viewmodel detail (fx.cpp): `addGripHand` / `addSupportHand` build fingers, trigger finger, thumb, cuff over
      each glove block; detail parts (kHole ports/vents/serrations, kGlass lenses, ribs, rails) in each gun's table
- [x] 4. Player models + held guns detail (main.cpp dummies loop: soles, pockets, holster, straps, radio, team
      patches, elbow pads, helmet rim + NVG mount, beanie fold, all inside the hitboxes; held guns: v0.23.6 gunModel)

**Done in v0.23: the "premium pass"** (owner asked for all of it, 2026-10-09; done in pieces, each committed and
pushed; light testing, no benches while they used the PC: **the FPS cost of shadows + post pass is not measured yet on
their laptop**; next session with their OK, run `--bench 10` with presets LOW/MEDIUM/HIGH):
- [x] 1. Sun shadows (`Renderer::buildShadows`: static depth map from `kSunDir` at map load, `sunVis`/`sunLit` in the
      box FS, config `shadows` 0/1/2 + menu row) and bevelled prop edges (wood/metal faces, box FS)
- [x] 2. Post pass (`setPost`, `endScene`, `setGrade` per map in loadMap; kPostFS): the scene on the window, copied
      (MSAA resolved) into `postTex_`, then FXAA (when the window has no MSAA) + colour grade + vignette; config
      `post_fx` (live since v0.25.4; MSAA still needs a restart: it's the window's pixel format)
- [x] 3. Real font (`Renderer::loadFont`, `g_ttf` in render.cpp): Rajdhani Bold (OFL, assets/fonts) baked by
      stb_truetype at scales 1,2,3,4,6,8,12 into a 2048 atlas; capitals as tall as the bitmap font's; lines with
      double spaces (padded columns) keep fixed cells; bitmap font.h is the fallback
- [x] 4. Living main menu (`menuCamera`: high dolly shots over the CT spots, 11 s each, through black via
      `g_menuFade`; light veil + letterbox in drawMenu) and fades in from black (`g_fadeFrom`, `drawFadeIn`) on map
      load and competitive round start
- [x] 5. Ambience (`ambienceBed` synthesized loops levelled to -33 dB RMS: Dust wind + birds, Harbor waves + gulls +
      wind; `setAmbience`, config `ambience_volume`) and room echo (`Audio::Reverb`, Freeverb-lite on voices with
      `wet`; UI cues dry via `dryCue`; `setRoom` from main: roofed cell = tunnel echo, street = short slap)
- [x] 6. Footsteps by surface (`stepSoundAt`): sand (Footstep) on the towns' ground, Sfx::FootstepStone (Fantozzi
      Stone recordings) on the sites' paving and the Lab's concrete, wood and metal as before
- [x] 7. Viewmodel polish (fx.cpp ViewModel::update/build): sway on a spring (slight overshoot), strafe lean
      (`ViewModelInput::sideSpeed`, `lean_`), dip on takeoff, roll in the walk bob, idle breathing; shot kick untouched
- [x] 8. Graphics presets (`graphics_preset`, `kPresets`, `applyGraphicsPreset` in settingsChanged): LOW (no shadows,
      FXAA), MEDIUM (shadows, FXAA), HIGH (sharp shadows, MSAA 4), CUSTOM when a setting is changed by hand
- [x] 9. Docs: README section, CLAUDE.md §7, final report + CI link + zip

Done in v0.21: bots use the whole arsenal, distinct gun sounds, smarter competitive bots (CT utility, post-plant,
saving, fire avoidance), the performance check (A* routes, mixer lock).
Offered, not chosen yet: defuse kits (10 s defuse without), bots dropping guns for each other, server-authoritative
netcode with lag compensation (`docs/04`), practice tools (grenade lineups, spray-pattern overlay, CS-style console),
trade-ups, stickers, ladders, net_graph; for Inferno: a crouch-only underpass, real stair steps, bots on the decks.

## 9. The owner: who they are, what they want, the history

- Casual, direct; writes numbered lists of changes, often with typos ("lap" = map?). When something is ambiguous, pick
  the most plausible reading, do it, and **say your interpretation in the report** so they can correct it.
- They test by playing: feel, sound and look are their verdicts. Say clearly what only they can judge.
- They get frustrated when the same thing misses twice ("man..."): **research first, measure, then change**, and
  explain the why in one or two sentences.
- **Spawns are their own area, out of mid's sight** (v0.24; `testSpawnsOffMid`). Don't put a spawn point, or an
  opening from spawn, where mid can see it.
- **Mid** (history): they asked ~10 times that bots stop fighting in mid straight out of spawn; v0.22.1 banned bots
  from mid before the execute. Once v0.24 walled the spawns off from mid, the owner reopened it (2026-10-11, v0.26):
  "they can go instantly with there now being no line of sight; short and lowers can be a play". Mid, short and lowers
  plays are fine from round start; what must hold is the spawns staying out of mid's sight (`testSpawnsOffMid`).
- **No stacking** (v0.26): bots shouldn't walk on each other's backs or all retake through one door (STACK must stay 0).
- Bots must shoot when they see someone: automated runs log "STALL" (a bot that saw its enemy 2.5 s without firing;
  must stay 0).
- Standing preferences: CS:GO feel above all; no tagging slowdown; guns only when they ask; no gimmicks (rejected
  "import CS crosshair codes"); the exact Dust2 copy is fine for personal testing; bhop off by default (they play with
  it on); reloads 20% faster than CS; model size 102% (was 120%, owner shrank it 15% in v0.23.1); muzzle light on walls only at 0.45; killcam skips on a click
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
  include `bhop 1`, `model_scale 102`, `mode 1`, `map 1` (Dust's size is fixed at 90% now).
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
