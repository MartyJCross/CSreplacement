# Crisp: single-player prototype

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

1. On GitHub, open **Actions → Crisp prototype**, click the latest green run, and download **Crisp-windows-x64** at the bottom of the page.
2. Unzip it anywhere and run `crisp.exe`. No installer is needed. Keep the `assets` folder next to it (real sounds and textures); without it the game still runs, with synthesized sounds and plain surfaces.
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
| 1 / 2 / 3 / 4 | Like CS: primary (rifle or sniper) / pistol (semi-auto) / knife (faster movement) / grenade. **Press 4 again** to cycle smoke → flash → HE → molotov |
| Q | Switch back to your previous weapon |
| F | Inspect your weapon |
| E | Plant (T, on a site) / defuse (CT, at the bomb). Hold it |
| B | Buy menu. Competitive: buy with money, in your spawn during buy time. Other modes: pick your primary (1 rifle, 2 sniper) for free |
| Mouse 2 | Sniper scope: 40 → 15 FOV → off. Stays scoped when you fire. With a grenade out: hold for an underhand lob, thrown on release |
| Tab | Scoreboard (deathmatch, retakes, competitive) |
| G | Quick-throw the current grenade without switching (deterministic bounces, so lineups repeat) |
| V | Noclip (fly where you look; Shift = slow) |
| C | Clear bullet decals |
| Alt+Enter | Toggle fullscreen |
| Esc | **Menu.** The game opens on the **main menu** (Play, Settings, Controls, Quit). In a game, Esc pauses: Resume, Change mode or map, Reset position (practice) / Restart route (prefire), Settings, Controls, Main menu, Quit. **Play** picks the mode and its options (map, bots, minutes, Dust size); START loads it fresh. **Settings** has pages: Mouse + view, Crosshair + HUD, Weapons + skins, Video + sound, Gameplay. Hover or ↑/↓ to select, click or Enter to press, ←/→, wheel or right click to change a setting (Shift = ×5); saved to `config.cfg`. Esc goes back |

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

## Dust2 (Play: any mode, or practice on DUST2)

Dust2 at **60% of real size** by default (`dust_scale`, Play screen DUST SIZE, 50–100%). Everything scales evenly, heights too, so slopes stay walkable; crates, headroom and doorways (at least 96 units wide) keep their real size. The area you're in shows at the top of the screen (LONG A, CATWALK, B SITE, ...).

- **Every route:** T spawn, outside long, long doors, long A with the blue container and the pit, the A ramp, A site with goose; top mid, mid, xbox (jump on it to reach catwalk), catwalk, short; mid doors, CT mid, CT spawn, the CT ramp, mid to B, B doors and **B window** (see and shoot through it, can't climb through); outside, upper and lower tunnels, the tunnel exit, B site with the back plat, car and boxes. **Arches** over top mid, short and CT mid.
- **Real slopes:** the ramps (A ramp, CT ramp, short stairs, top mid, mid, the tunnel stairs...) are smooth slopes, not steps. You slide up and down them like in Source, and jumping onto one doesn't snag.
- **Real surfaces** (CC0 textures from Poly Haven): sand underfoot, paving on the sites, sandstone and plaster walls, plank crates and doors, a painted ribbed container, metal barrels and cars. They're detail maps over the map's own colours, mipmapped and filtered so they don't shimmer.
- **Mid doors** are mostly shut, like Dust2's: a gap in the middle you can walk and shoot through (the leaves are wood, so they're wallbangable).
- **Proportions like the real map:** lanes are as narrow as Dust2's (mid, catwalk, the tunnels and long were far too wide before), so it plays tight rather than open.
- **Scenery:** buildings of different heights with stone-block walls, a trim along their foot and top, windows with painted shutters, doors under cloth awnings and wooden beam ends; flagstone and sand floors, plank crates, a ribbed metal container and barrels. None of the scenery on walls blocks you or your bullets (it's all thin and flat against the wall or above head height).
- **Cover everywhere:** crates, barrels and cars in every area (long, the pit, both sites, mid, the tunnels, both spawns), mostly against walls so the lanes keep their width.
- **Materials:** crates and doors are wood, the container, cars and barrels metal, the rest stone. You hear it in your footsteps (and the bots'), and bullets go through wood twice as easily as stone; metal is the hardest.
- **It's built from memory,** so it isn't a perfect copy. The headless tests run a simulated player along the main routes and print the run times.

In practice mode the 4 bots peek from cover: the long corner, mid doors, the B and A default boxes, B doors and short. They hold the angle for a random time, react in 0.25–0.55 s with slight random aim error (your own shots stay fully deterministic), and respawn 2–4 s after you kill them.

## Radar

Top-left on Dust (`radar`, Settings → Crosshair + HUD): the whole map north-up, shaded by height; you as an arrow; enemies as **red dots while spotted** (you can see them, a teammate can, or they just shot); teammates in **blue**; the bomb (red when planted, orange when dropped).

## Grenades

**4** takes out a grenade; **4 again** cycles smoke → flashbang → HE → molotov. Like CS, pressing pulls the pin and the grenade leaves your hand **when you let go**: hold Mouse 1 for a full throw, Mouse 2 for an underhand lob, both for a medium throw (the trajectory preview shows the one you're holding), then you switch back. While you hold it your hand winds up (back for a throw, low for a lob). Opening the menu keeps it in your hand. **G** quick-throws the current one. Competitive: you only have what you bought (1 smoke, 2 flashes, 1 HE, 1 molotov); everywhere else they're unlimited.

They fly like CS:GO: thrown at 675 u/s (a lob is 30% of that) about 10° above where you aim, plus 1.25× your running velocity, under grenade gravity (320 u/s²), bouncing off walls: a throw 45° up carries about 1,470 units before its first bounce, a level throw about 750.

**Trajectory preview** (`nade_preview`, Settings → Gameplay): while you hold a grenade, dots show exactly where a full throw would go and a cross where it goes off (smoke: where it stops). It uses the same code as the real grenade, so it's never off. Default: everywhere but competitive (`nade_preview 2` = always, `0` = off).

- **Smoke:** pops once it has stopped rolling (1.5 s at the earliest), lasts 15 s, blocks sight (yours and the bots'). Same throw = same landing spot.
- **Flashbang:** pops 1.8 s after the throw (a bit longer than CS, so you can turn away). Whites you out if you can see it, fully if you're looking at it, less if you're turned away; your ears ring. Bots facing it go blind for a few seconds.
- **HE:** goes off 1.8 s after the throw: up to 98 damage, falling off to nothing at 350 units; walls stop it. It hurts you too.
- **Molotov:** bursts where it lands into a 7 s fire, 10 damage every 0.25 s to anyone in it. A smoke puts it out, and a molotov thrown into a smoke fizzles.

## Performance

`crisp.exe --bench 10` runs 10 s at real speed and writes `bench.txt` (avg FPS, 1% low, time per frame part, GPU). On the owner's Ryzen 7 4800H (integrated Radeon), 1080p deathmatch at 4x MSAA went from 126 to ~190 FPS average and 62 to ~130 1% low with:
- a depth pre-pass, so every pixel is shaded once (`depth_prepass`)
- world boxes culled to the view and drawn nearest-first each frame
- lighting worked out per face instead of per pixel
- `msaa 0` gives ~275 FPS if you'd rather have frames than smooth edges

## Competitive 5v5 (Play: MODE)

You and 4 bots against 5 bots on Dust2, **MR12**: first to 13 wins, sides swap after 12 rounds (with a fresh economy), and a new match starts after the result.

- **Rounds:** 5 s freeze time (buy, can't move), 1:55 to play, 40 s bomb fuse.
- **Win** by killing the other side, planting and letting it blow (T), defusing (CT) or running the clock out without a plant (CT).
- **Money, like CS:** start with $800; win $3,250 ($3,500 for a bomb win); loss bonus $1,400 rising by $500 per loss in a row up to $3,400; Ts get $800 more for a plant; $300 per kill ($100 with the sniper).
- **Buy menu (B):** in your spawn during the first 20 s of a round. 1 rifle $2,700 · 2 sniper $4,750 · 3 kevlar $650 · 4 kevlar + helmet $1,000 · 5 smoke $300 · 6 flashbang $200 · 7 HE $300 · 8 molotov $400 · 9 defuse kit $400 (CT). Kevlar takes body and arm hits to 77.5%, a helmet does the same for the head. If you survive, you keep what you had.
- **The bomb:** a random T carries it. Hold **E** on either site for 3.2 s to plant. If the carrier dies it drops; walk over it to pick it up (T bots go and fetch it). You can see it: a C4 brick with a keypad, wires and a light that blinks with the beeps once it's planted. CTs defuse with **E**: 5 s with a kit, 10 without. You can't move while planting or defusing.
- **Spawns:** both teams spread over their spawn, you at the front, and nobody is in sight of the other team's spawn when the round starts (the long mid-doors sightline is kept clear; tested).
- **Names:** the bots have ordinary first names (a different set every match), in the kill feed, scoreboard, radio and when you spectate them.
- **The bots:** Ts gather at a staging point (long, short / catwalk or B tunnels), then execute together after 14 s at the latest, with a smoke and a flash for the site (each bot aims by trying throws with the real grenade flight): the carrier goes for the plant (it doesn't hide, chase or stop for anyone far away: it fights its way through and gets straight back on its way), the rest take the site's angles. With 45 s left and no plant, every T commits to the site. You choose the teams (Play: TEAMMATES 0-4, ENEMIES 1-5) and how good the bots are (TEAMMATE SKILL, ENEMY SKILL: easy, normal, hard, expert; hard is the default for enemies). Skill sets reaction time (0.45-0.85 s easy down to 0.12-0.27 s expert), aim error, fire rate, how far behind your movement they aim and how often they go for the head (never on normal, 40% on expert); the other modes use BOT SKILL. CTs play a default setup (one mid, one short, one long, two B; now and then one heavier on A or B), each from one of a few spots so it's never quite the same, mostly holding their angle and stepping back behind cover now and then; once Ts show up on a site, the far CTs rotate, and after a plant they all retake, in a hurry. Bots use cover: in a fight they shoot, duck behind the nearest box or corner, and peek again (hurt, they stay hidden longer); holding a site they hold the angle from beside cover and step back into it now and then. In the open with nothing nearby they jiggle (a quick strafe, stop, shoot). Teammates trade: when one is in a fight, the others nearby look that way. A bot that's been holding an empty angle for a while reacts a little slower to the first enemy (up to 0.2 s), so a good peek is rewarded. They fight each other, not just you, buy rifles and armor when they can afford them, and play pistol rounds with pistols. Your bullets pass through your teammates (no friendly fire).
- **When you die** you watch through a living teammate's eyes until the next round: **Mouse 1 / Mouse 2** next / previous teammate, **Space** to fly free (and back). If the one you watch dies, it moves on to the next. The dead can't shoot.
- **Teammate radio:** your side's bots call out in the top-right feed (green): ENEMY SPOTTED: LONG A, GOING B, THEY'RE ON A, ROTATING, PLANTING THE BOMB, DEFUSING.
- **Tab** shows both teams with money for yours; the radar shows your teammates and anything they spot.

## Deathmatch (Play: MODE)

- **The match:** 5 minutes (`dm_minutes`) against 10 bots (`dm_bots`, up to 16); results screen, then a new match.
- **Spawns are anywhere on the map**, away from the bots and out of their sight, with every gun reloaded and 1 s of protection. Bots respawn 2–4 s after you kill them, out of your sight.
- **A kill gives you +40 HP** (up to 100) **and 10 rounds** in your magazine (up to a full mag).
- **The bots** roam the whole map along the real routes, see in a 150° cone (catch them from behind), hear your running (~1,100 units) and shots (~2,200), and stop to shoot after a 0.25–0.55 s reaction once they've turned to face you. Half the time they roam towards your part of the map. In a fight they use cover when there's some close by (shoot, duck behind it, peek again), else they jiggle (strafe a step, stop, shoot). They spawn out of sight of where you are and of where you'll be a moment later.

## Retakes (Play: MODE)

- **Each round** the bomb is already planted on a random site, held by 4 bots (`rt_bots`, 1–6) facing the way you'll come, holding from beside cover. You start at a random entry (A: CT spawn, long or catwalk; B: tunnels, mid to B or CT mid); spots you can see from your entry are used last, so nobody's in view when the round starts.
- **Defuse it** (hold E for 5 s, you have a kit) before the 40 s fuse runs out. Killing everyone isn't enough, like CS. Lose if it blows or you die. Kills heal +40 HP.

## Online (Play: MODE -> ONLINE)

Deathmatch or competitive on Dust with friends, up to 8 players. One of you hosts from the game; the others join.

1. **Your name:** Play -> MODE: ONLINE -> **YOUR NAME** (select it and type). It shows in the kill feed and on the scoreboard.
2. **Host:** pick the **GAME** (`net_game`): DEATHMATCH (players only) or COMPETITIVE (below), then **HOST A GAME**. The first time, Windows asks whether Crisp may use the network: allow it. The game then asks your router to open the port by itself (UPnP) and shows, at the top of the screen, **the address to give your friends** ("FRIENDS JOIN: ..."), or what's in the way if it can't:
   - *"your router sits behind another one"*: your ISP's box is a router too. Forward UDP 27015 on that one to your router's address (shown), or put it in bridge mode, or use ZeroTier;
   - *"carrier-grade NAT"*: your provider shares one address between customers; nobody can reach you directly: use ZeroTier;
   - *"no router answered"*: UPnP is off on your router: turn it on, forward UDP 27015 by hand, or use ZeroTier.
   The port is closed again when you stop hosting.
3. **Join:** type the address in **JOIN ADDRESS** (an IP or a name, `address:port` if it isn't 27015), then **JOIN**. The host's Dust size is used.
4. **Same house:** use the "SAME HOUSE" address the host sees. **ZeroTier / Tailscale** (everyone on the same virtual network) always works, whatever the routers do.

How it works: everyone's own movement and shooting run on their own PC exactly like offline (128 ticks, zero lag for you), and each PC sends a ~30-byte update every tick (about 8 KB/s each way per player). Other players are shown about 47 ms behind their newest update, smoothly. **Your game decides your hits** against what it showed you, and the victim's game applies them: what you see is what you hit, at any ping. That trusts everyone's PC, so it's for friends (no anti-cheat). Kills give +40 HP and 10 rounds, like offline deathmatch; Tab shows the scores; the menu doesn't pause an online game (you just stand still). Everyone has to run the same version of Crisp (a different one is told so when it tries to join).

- **Crouching** shows on other players, and their hitboxes crouch with them (the head drops to the crouched eye height).
- **What they hold** shows: rifle, pistol, sniper, knife or a grenade.
- **Grenades:** a throw is sent to everyone and every game flies it the same way (the flight is exact), so smokes, flashes, HEs and molotovs land in the same place for all. Each game works out what it does to its own player: your flash blinds you if you looked at it, an HE or a fire hurts you and counts for whoever threw it.

### Online competitive

The offline competitive match (MR12, economy, buy menu, the bomb) with friends in it. The host's game runs the match and the bots; everyone else's game shows them and runs only its own player.

- **PLAYERS** (`net_teams`): **SPLIT** puts players on both sides, evenly (you can play against each other), **ALL ON ONE SIDE VS BOTS** puts everyone on the host's side.
- **Team sizes:** TEAMMATES (the host's side, not counting the host) and ENEMIES, like offline (`comp_mates`, `comp_enemies`). Players take places first; **bots fill the rest**. Bots on the host's side use YOUR TEAM'S BOTS skill, the others OTHER TEAM'S BOTS.
- **Joining mid-match:** you watch until the next round, then you're in (split: on the side with fewer players).
- **Money** is yours: kill rewards, the round's pay and the loss bonus come to your own game, like CS. The scoreboard shows only your own money.
- **The bomb:** anyone on T can carry it (the host's game hands it out, like CS); you plant or defuse with E as offline, and your game tells the host. Bots plant, defuse and fetch a dropped bomb as offline.
- **Bots hear you:** your footsteps and shots reach the host's bots like the host's own.
- Leaving the match: Esc -> MAIN MENU. If the host leaves, everyone goes back to the menu's Lab.


## Prefire (Play: MODE)

- **Pick a route** (A long, B tunnels, mid, A short). Bots stand at the usual defender angles along it, facing the way you come: the long corner, pit, goose, behind the default boxes, B doors, back plat, mid doors and so on. They never move, but they turn on you and shoot (BOTS SHOOT BACK on the Play screen, `prefire_bots_shoot`, on by default).
- **Every route starts out of the bots' sight** (tested), and they hold fire until your clock starts.
- **The clock** starts when you move or shoot and stops when the last one dies: your time, accuracy and headshots, and the best time per route this session. Die and the route starts over; Esc → RESTART ROUTE any time.

## Kill feed, damage report, scoreboard

- **Kill feed** top-right: who killed whom, with what, headshot / wallbang.
- **Damage report** bottom-left when you die: damage given and taken this life, per player and hits.
- **Tab scoreboard:** kills, assists (41+ damage on someone a teammate finished), deaths, ADR (damage per round; per life in deathmatch), HS% (headshot kills) and MVPs.

## Players: facing, arms, anti-aliasing

- **Bots turn to face you** when they fight. Their hitboxes turn with them. Head-on you see about 27 units of shoulders; side-on, about 21.
- **Arms holding the rifle** are part of the model and hitbox, and count as chest, like CS. The rifle itself isn't hittable.
- **Competitive colours:** Ts in tan, CTs in blue; a green marker floats over your teammates.
- **Anti-aliasing** (`msaa 4`, Settings → Video + sound, applies on restart) smooths edges so far-away players stop shimmering.

## Hit feedback

- **A subtle tick** plays when your bullet connects (a little lower and louder on a kill): `hitsound`.
- **Small ticks around the crosshair** flash for an instant: white for a body hit, red for the head, slightly bigger on a kill: `hitmarker`.
- **Damage direction:** when a bot hits you, a red arc round the crosshair points at where it came from (fades over a second), like CS.
- **A helmet "tink"** when your headshot is stopped by a helmet instead of killing.

## Inspect, knives and finishes

- **F inspects.** Guns swing up to show the left side, roll over to the right side, tip down to look at the mag, then settle back, with a little breathing sway. Firing, reloading or switching cancels it.
- **Knives** (Settings → Weapons + skins): **butterfly** (fade blade; flips open on draw, two aerials on inspect), **karambit** (spins round the finger ring), **M9 bayonet** (tossed end over end and caught), **talon** (big claw that spins).
- **Gun finishes** (Settings → Weapons + skins): factory, crimson, arctic, jungle, gold.

## Spray feedback (all cosmetic: your bullets are unchanged)

- **Every shot has weight:** the gun snaps back and up, a big muzzle flash, and a quick thump of the view (it widens ~1.4% for 45 ms, out from the crosshair). The shot sound has a sharp crack and a short low-mid thump (no extra sub-bass).
- **The gun's kick builds through a spray:** it climbs, shakes and rolls harder the longer you hold Mouse 1.
- **A slight camera roll on each shot** that grows through the spray. It turns around the crosshair, so where you aim doesn't move. Turn the roll and the thump off with `view_shake 0` (Settings → Mouse + view).
- **Shell casings** fly out to the right.
- **Far impacts are drawn bigger** (dust puffs up to 4x, bullet marks up to 3x), so you can read where a spray lands at range.

## Sound

- **Real recordings** (all CC0, see `assets/CREDITS.md`): rifle, pistol and sniper shots cut from *The Free Firearm Sound Library* (an AK-47, a 1911 / Walther, bolt-action rifles) with their natural echo; distant shots; footsteps on sand and wood; bullet impacts on stone, wood and metal; menu clicks. Each is levelled to the sound it replaces, so the mix stays as it was. Everything else (hits, explosions, flashbang, bomb, reloads) is still synthesized.

- **Every sound has 2–6 variants.** Each play picks a different one, with a little random pitch and volume, so repeats never sound identical.
- **Guns are loud**, yours loudest (the mix has a soft limiter, so it never crackles).
- **Gunshots** have a mid "bark", a short boom (sub-bass is cut so they don't sound like the bass is turned up) and a slapback echo off the walls. Rifle, pistol and sniper each have their own sound.
- **Bots shooting from more than ~1,400 units away** sound distant: muffled, no crack, mostly echo.
- **Bullets you hear:** your shots hitting the world chip stone, thunk into wood or ping off metal; a bot's bullet that just misses your head whizzes past your ear.
- **Footsteps** are boot-on-grit (heel, toe scuff, crunch), and landing has a bit of gear rattle.
- **You are silent below 135 u/s.** Shift-walk (~112) and crouching make no footsteps; running does, like CS. Deathmatch bots hear your footsteps.
- **Positional footsteps:** moving dummies and bots make footsteps that are panned and attenuated by distance. Close your eyes and point at them.
- **Volume** is `volume` in `config.cfg` (0..1).
- **Synthesized sounds** are made at startup (and stand in for any recording that's missing). To listen to them as files, run `crisp.exe --dump-sounds <folder>` and it writes every variant as a WAV.


## Bots, smokes, wallbangs, KZ

- **Bots shoot back (Play: practice).** Any dummy that can see you reacts after a random 0.25–0.55 s and fires every 0.22–0.38 s, with about 0.8° of random aim error. They aim at where you were 0.2 s ago, so strafing and counter-strafe peeks dodge them, while standing still in the open gets you killed. Smokes block their vision.
- **Grenades:** see Grenades above (smoke, flash, HE, molotov).
- **Wallbangs:**
  - rifle and sniper bullets go through thin walls (rifle 24 units, sniper 40) and lose damage; the pistol can't
  - wood counts half as thick, metal one and a half times (Dust's doors are wood: shoot through them)
  - the hit log and kill feed show `WALLBANG`
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

For a CS-like comparison, set `spread_spray 1` (spread grows during a spray) and/or `spread_movement 1` (inaccurate while moving or in the air) in `config.cfg`, then use Settings → RELOAD CONFIG.CFG.

## Tuning numbers (CS:GO-like defaults)

| | Value |
|---|---|
| Rifle / knife speed | 215 / 250 u/s |
| Walk / crouch | 52% / 34% of max |
| Jump | 57 units apex, ~675 ms airtime (gravity 1000: snappier than CS:GO) |
| Friction / accelerate / air accelerate | 5.6 / 6.48 / 12 (95% of run speed from a standstill in ~320 ms) |
| Rifle | 600 RPM, 30 rounds, 36 dmg (×4 head) |
| Pistol | semi-auto, 400 RPM max, 12 rounds, 35 dmg (×4 head), strong climbing kick |
| Fire rate | exact: a held spray keeps its cadence, and however you tap, two shots are never closer than one interval |
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
build/Release/crisp.exe        # Windows (Visual Studio generator)
build/sim_tests                  # headless movement/weapon tests (any OS)
```

## Code map

| File | What |
|---|---|
| `src/movement.*` | Kinematic player movement: friction/accel, air strafing, step-up, crouch-jump, stamina |
| `src/world.*` | Maps made of boxes and ramps (the Lab, Dust2 from a grid of named areas, materials), swept-box / ray traces incl. sloped ramps, broadphase, retake/team spawn data |
| `src/nav.*` | Bot navigation on the Dust grid: walkable / roamable cells, shortest routes, following a route |
| `src/bots.*` | Bot brains: spawning anywhere, roaming or walking to a goal, holding angles, sight cone, choosing a target among enemies, hearing, chasing (tested headless) |
| `src/combat.*` | Weapons, recoil patterns, deterministic spread, dummies and turned hitboxes, armor, material wallbangs, hit detection |
| `src/render.*`, `src/gl.*` | OpenGL 3.3: every 3D object is an instanced box (optionally turned or a ramp); depth pre-pass, view culling, nearest-first; the HUD is a single batch |
| `src/fx.*` | First-person weapon models and animation, tracers, particles (cosmetic only) |
| `src/audio.*` | SDL3 audio mixer and procedural sound synthesis |
| `src/main.cpp` | Window, input, 128 Hz fixed-step loop, the modes (practice, deathmatch, retakes, competitive), grenades, bomb, economy, combat record, radar, HUD, Esc menu |
| `tests/sim_tests.cpp` | Headless checks: speeds, jump height, counter-strafe timing, collision, stairs, ramps, crates, determinism, material wallbangs, Dust routes and run times at every size, spawns, bot roaming and sight, turned hitboxes |

Shots are tested against exactly what was on your screen the frame you clicked: the interpolated dummy positions, facings and camera. That's the single-player version of "what you see is what you hit".
