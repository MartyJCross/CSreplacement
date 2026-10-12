# Crisp changelog

What changed in each version handed over, newest first. `README.md` describes the game as it is now; older
history (before v0.22) is in the git log.

## v0.30: Inferno, rebuilt by hand

- **Inferno rebuilt by hand** (owner: the generated one was "a mess", spikes and gaps, apps you couldn't get into):
  ~180 deliberate areas on a 16-unit grid (rooms, streets, stairs as ramps, doorways at their real width, thin walls),
  each placed and given its height against CS2's radar and measurements; walkways over streets (the T apps bridge,
  bottom mid over the underpass); hand-placed props (banana's logs, sandbags and car, the B boxes and statue, the
  coffin, library's shelves, the cart, barrels, benches); B's square in red brick.
- **Ramps:** a steep ramp could block you at its top edge (Inferno's balcony): fixed in the collision for every map.
- **Bots:** they drop off ledges (the balcony into pit), climb stairs as slopes, and a group sets off in file, a third
  of a second apart, instead of all at once (two walked on each other at the top of banana).
- Tests: no holes, slivers or stray cells; the player walks every main route without catching on anything.

## v0.29: Inferno

- **A new map: Inferno**, CS2's, as accurate as possible: its real size, coordinates and heights, measured from the
  game's radar and the community's nav mesh, with its callouts (T apps, banana, car, B site with the fountain,
  construction, CT, speedway, library, arch, A long, A site, pit, graveyard, balcony, close apps, CT apps, boiler,
  short, top mid, mid, second mid, underpass...). Plaster and terracotta, shutters and flower boxes, cobbled streets.
- Every mode plays on it: practice, deathmatch, retakes, competitive (Ts hit A through apps or short, B up banana; CT
  setups, retakes, utility), prefire (banana, apartments, mid) and online (pick it in the lobby).
- Bots climb stairs as slopes (up to 26 units a step on a ramp).

## v0.28: knife attacks, drops, bot utility, StatTrak, factory new

- **Knife attacks** (CS:GO's numbers): Mouse 1 slashes (40, 25 for a slash right after another, 90 from behind,
  48 units; forehand and backhand take turns), Mouse 2 stabs (65, 180 from behind, 32 units). Kevlar takes 15% off.
  New swing and stab animations, and sounds for the swish, the blade going in and the blade on a wall.
- **Drops:** G in competitive drops the primary you're holding (otherwise it still quick-throws a grenade). Z → 8
  NEED A DROP: in buy time the richest teammate bot that can afford it buys your side's rifle and drops it by you.
- **Bot utility:** the Ts' execute on a full buy adds a molotov onto the site's anchor spot; the first two CTs into a
  retake flash over the site; the Ts molly the bomb when they hear a defuse (a defusing bot stops when it steps out).
- **StatTrak:** one case item in ten counts your kills with it, on a six-digit counter on the gun's side.
- **Every skin factory new:** no more scratches (your skins look new; the float only shifts the pattern).

## v0.27.2: refunds, the butterfly redone, a Q fix

- **Refunds:** in competitive, with the buy wheel open, **R** (or the REFUND button under the full buys) sells back
  your newest buy this round for what you paid, while it's untouched; press again for the one before.
- **The butterfly's animations redone:** the draw, the three inspects and the idle tricks now play side on, the knife
  across the screen, so the handles and blade swing round in front of you (they turned towards and away from you, a
  thin stick waving about); one movement at a time, slower, and the toss stays on screen.
- **Fix:** after picking up a gun in place of another, Q could switch to the gun you'd dropped. Q now only goes to a
  weapon you still have.

## v0.27.1: T bots stay on the site after a plant

- A T bot that was in a fight when the bomb went down walked back to spawn afterwards and stood there: the plant
  skipped bots mid-fight, so their spot to come back to stayed their spawn. Now every T bot is sent to the site's
  holds (one still fighting keeps fighting, then goes there).

## v0.27: skins reimagined, knife animations, knife cases

- **Skins are painted part by part:** body, slide / dust cover, barrel, muzzle or suppressor, handguard, stock, grip,
  mag, scope and the small parts each take their own paint (or keep their metal, wood or polymer); knives paint blade
  and handle. 14 new paints: a printed look (sweeping ink lines on pearl, gold edges), lotus flowers outlined in gold,
  halftone, crystal facets, seigaiha waves, holographic, printed decals, damascus, a starry galaxy, scales, splatter,
  racing stripes, gradients, mecha panels. Every skin is redesigned (same names, your inventory keeps them) and there
  are new ones: LOTUS GARDEN, PRESSWORK, NEBULA, KOI, KOI POND, KUMICHO, SPLASH, PRISM, AURORA, MECHA, STREET ART,
  MOMENTUM, LOTUS; knives get galaxy dopplers and six new finishes (LOTUS, PRESSWORK, DAMASCUS, NEBULA, SCALEWORK,
  PRISM). The inventory stage has a light backdrop.
- **Knife animations:** a flourish on every draw, three inspects a knife (F plays the next one): aerials, fans and
  tosses for the butterfly, finger spins and flicks for the karambit and talon, tosses, twirls and hand rolls for
  the straight knives; tossed knives leave the hand. Idle tricks every few seconds (Settings -> Weapons + skins ->
  KNIFE TRICKS WHEN IDLE).
- **Knife cases:** one every 100 kills, knives only.

## v0.26.1: bot and replay fixes

- **The Ts don't all go mid any more:** most rounds come through long or tunnels (each way through mid is picked a
  third as often), the default play puts one group in mid instead of two. Automated matches: one round in eleven sent
  everyone through mid (was about seven in ten).
- **Replays show your gun and grenades:** through anyone's eyes you see their weapon in first person (yours with your
  skins), and grenades in the air and molotov fires are recorded and shown. The grenade preview no longer shows in a
  replay.
- **CTs retake straight away:** no walking back to a meeting point (a CT went back to spawn and stood there); each
  goes through its own way in, the ones with a shorter trip holding a few seconds so they arrive together. A CT with
  a bomb to defuse only saves when it's alone against four.
- **TAKE MID** is a call (Z, T side): two take mid, the rest wait on the site's other way, then both hit together.
  CTs get **PUSH MID**. Seven calls a side now (1-7).
- **Spectated bots aim smoothly:** they ease onto where they look instead of snapping, and keep the angle they're
  checking instead of flicking between two.

## v0.26: smarter bots, wider doorways

- **Harbor is back in competitive's MAP list** (it showed THE LAB / DUST, and picking "DUST" loaded Harbor).
- **Every doorway is one grid cell (32 units) wider** on Dust and Harbor: long doors, B doors, mid doors (its gap 44 ->
  76 units), the spawn passages. The spawns are still out of mid's sight.
- **Mid is open:** bots play mid from the start (the spawns are walled off from it): T plays through **short, lowers
  and mid to B**, a **mid control** play, CTs contesting mid from the doors, the CT mid push.
- **Real split executes:** two ways onto the same site at once (long and short, tunnels and mid to B, docks and
  short...), timed so both groups hit together, each with its own smoke and flash.
- **No stacking:** bots keep a gap behind a teammate ahead; CT retakes come from different sides (Dust A: CT and
  short), waiting out of sight and going in together; rotations come by different ways. Automated matches: 36
  stacked-up pairs in three matches before, 0 after.
- **Preaim:** walking bots keep their crosshair at head height on the angles enemies hold (or round the next
  corner), and shoot quicker when someone appears where they were already aiming.

## v0.25.4: lighter outlines, a much cheaper post pass

- **The stylized outlines are thinner and lighter:** one pixel, just outside an object's silhouette, a dark tint of
  what's behind rather than black (they were two pixels of near-black, and lined every crease too).
- **Post-processing costs far less:** on the owner's laptop (1080p, stylized, MSAA 4, sharp shadows) 90 -> 137 FPS.
  The scene used to be drawn into an offscreen picture, which this AMD driver does at about half speed (and copying
  its depth for the outlines cost more again); now it's drawn on the window as before and only copied for the
  effects, and the outlines read a distance code the world shader writes into the picture's alpha. The stylized look
  now costs the same as the classic one.
- **POST-PROCESSING applies at once** (no restart). ANTI-ALIASING still needs one: the samples are part of the window.

## v0.25.3: maintenance

- **Faster traces:** sight lines, bullets and sounds through walls test only the map cells the line crosses, not
  its whole bounding box: a long ray on Dust takes about half the time (~2.3 us, was ~4.6). Bots see, shoot and find
  cover with the same answers, just cheaper.
- **No allocations per frame** for the first-person gun, tracers, helmets, dead bodies, guns on the floor and the
  buy wheel (they reuse their memory frame to frame).
- **Docs:** the README describes the game as it is now (menus, bunny hops, spread, online lobby); the version notes
  moved here.

## v0.25.2

- **Dust is always 90% of real Dust2's size** (the setting is gone), and the FPS counter is on by default.

## v0.25: menus you can find your way round, scroll-wheel bhops

- **Menus:** the main menu is PLAY, PLAY ONLINE, INVENTORY, STATS, SETTINGS, QUIT. PLAY lists the modes
  (competitive, deathmatch, retakes, prefire, practice) with a line about the highlighted one; picking one opens its
  setup - the map (and Dust's size), the mode's own options, START. PLAY ONLINE is your name, HOST A GAME (you set the
  game up in your lobby), your friend's address and JOIN, and the port. SETTINGS: mouse + view, crosshair + HUD,
  weapons + skins, video, sound, gameplay, controls, and RESET TO DEFAULTS (press it twice: every setting back to
  its default, keeping your name, your friend's address, the port and unlocked skins). The pause menu offers NEW GAME.
- **Testing options gone from the menus:** reload config, random spray spread, player model size, zero-lag camera,
  smooth stairs, "all skins unlocked" (their config keys still work). Noclip (V) only in practice. The top-left speed /
  spread / shots readouts show in practice only; the FPS counter is Settings -> Video -> SHOW FPS.
- **Scroll-wheel bunny hops without auto-hop:** a jump within 47 ms of landing (6 ticks) is a hop and keeps your
  speed (less a small landing penalty); a wheel notch just before landing waits 0.15 s for the ground. Holding Space
  doesn't hop (that's the AUTO BUNNY HOP setting).

## v0.24: spawns off mid, the stylized look, detailed guns and players

- **Spawns in their own area:** on both maps the T spawn is walled off from mid, with a short dog-leg path
  ("MID PATH") to get there, and the CT spawn sits behind a wall out of the line through mid doors (Harbor's CT ramp
  splits round it). Nobody in mid can see any spawn point, and reaching mid from spawn is a run (at least ~530
  units, it was 170). Deathmatch never spawns you in mid.
- **The clean stylized look** (Settings → Video + sound → VISUAL STYLE, `art_style` 1, the default; 0 = classic):
  flat colour with only a hint of the surface textures, crisp dark outlines on every edge and silhouette (drawn by
  the post pass from the world's depth, fading with distance), shade that stays rich and warm instead of grey, walls
  that lighten as they rise, and a bolder colour grade. Needs post-processing on.
- **More detailed first-person guns:** gloved hands with fingers round the grip and up the handguard, a trigger
  finger on the guard, thumbs and wrist cuffs; ejection ports, selector levers, rivets, gas blocks, cleaning rod,
  front-sight ears, mag ribs and floor plates, rail teeth, vents, slide serrations, scope lenses and turrets,
  cheek rest, thumbhole, barrel flutes, recoil pads.
- **More detailed players** (all inside their hitboxes): boot soles, cargo pockets, a holster and pouch, vest straps,
  a radio, team patches on the shoulders, elbow pads, a helmet rim and night-vision mount (CT), a beanie fold (T).

## v0.23.7

- **Buy wheel icons:** half as big again, with panel lines in a second shade (ejection ports, handguard grooves,
  mag ribs, scope lenses, suppressor rings), and a slightly bigger wheel to fit them. The kill feed keeps clean
  silhouettes.

## v0.23.6

- **New gun icons** (kill feed, buy wheel): hand-drawn CS-style silhouettes for every weapon - stocks, grips, curved
  mags, scopes, suppressors, muzzle brakes.
- **More detailed guns in players' hands and on the floor:** stocks, grips, magazines, sights, rails, scope bells.
- **Scroll-wheel bhops without auto-hop:** a wheel notch in the air waits 0.1 s for the ground and jumps on the
  landing tick, and a hop on the landing tick takes only a quarter of the landing slowdown, so chains are easy.
  Space stays one jump per press.
- **Speeds:** scoped, the AWP runs at 100 and the SSG 08 keeps its 230 (CS:GO; both used to halve); crouching while
  scoped no longer stacks the scope slowdown (an AWP crouch-walks at 68, not 34). Bots now run at their gun's speed
  (knife 250, pistols 240, AWP 200) instead of always 215.

## v0.23.5

- **Spectating:** you hear the game from the player you're watching (where they are, the way they face), see
  their gun in first person (their shots kick it, their reloads play, a grenade shows as they throw it), and your
  own body stays put meanwhile.
- **Flying free when dead:** no gun; Space goes up and Ctrl down; Mouse 1/2 watches a teammate again (Space while
  watching starts flying, from their eyes).

## v0.23.4

- **Scout jump shots:** the SSG 08 is dead on at the top of a jump (scoped, vertical speed under 40 units/s: about
  the top 80 ms), like CS:GO's scout, even while moving sideways. Rising or falling it scatters like any gun in the air.

## v0.23.3

- **Moving costs accuracy, always:** running, jumping and noscoping scatter your shots (the option to turn that off
  is gone). Stop to shoot, like CS.
- **Cheaper guns scatter a little standing still** (USP, Dual Berettas, Galil, MAC-10, UMP-45); the AK-47, M4A1-S,
  AWP, SSG 08 and Deagle stay dead on.

## v0.23.2

- **No glimpse of your respawn when you die:** the camera stays where you fell and sinks to the floor until the
  killcam starts (or you respawn, with the killcam off).
- **Pistols fire as fast as you click:** a click made while the gun is still cycling used to be thrown away, so
  spamming felt slower than the gun really is. Now a click waits up to 0.25 s and fires the moment the gun is ready
  (still one shot per click, never faster than CS's rates: USP 0.15 s, Berettas 0.12 s, Deagle 0.225 s). Shooting
  slowly enough for the recoil to settle is the skill.
- **New defaults:** volume 0.8, ambience 0.3, bunny hop off. (An existing config.cfg keeps its own values.)

## v0.23.1

- **Characters 15% smaller:** the default model size goes from 120% to 102% (hitboxes, crouch, eye height and aim
  scale with it, as before).

## v0.23: the premium pass

- **Sun shadows.** Walls, roofs, awnings and crates cast real shadows from the sun (worked out once when the map
  loads, so they cost little); what's in shade is cooler and darker, and players walking into shade darken too.
  Settings → Video + sound → SUN SHADOWS (`shadows`: 0 off, 1 on, 2 sharp).
- **Post-processing** (`post_fx`, restart): each map has its own colour grade (Dust warm and sun-baked, Harbor cooler,
  the Lab neutral) and a soft vignette that leaves the middle of the screen alone. With `msaa 0` it smooths edges with
  FXAA, which is much cheaper than MSAA on integrated graphics. The HUD isn't touched.
- **A real font.** The HUD and menus use Rajdhani Bold (SIL Open Font License) instead of the 5x7 pixel font; the
  scoreboard's columns still line up. Without the font file the old one comes back.
- **A living main menu.** Behind the menu a camera drifts slowly over the map from one angle to the next. A map
  loading or a competitive round starting fades in from black.
- **Ambience and echo.** Dust has wind and distant birds, Harbor has waves, gulls and sea wind (`ambience_volume`,
  AMBIENCE row). Shots and steps echo in tunnels and roofed halls, with a short slap off the walls in the streets; UI
  sounds stay dry.
- **Footsteps by surface:** sand on the streets, a harder stone click on the bomb sites' paving and the Lab's
  concrete, wood on crates and doors, metal on the container.
- **Viewmodel polish:** the sway settles on a spring (a little weight), strafing leans the gun, jumping drops it a
  touch, the walk bob rolls slightly, and it breathes when you stand still. The shot kick is unchanged.
- **Graphics presets** (`graphics_preset`): LOW (no shadows, FXAA), MEDIUM (shadows, FXAA), HIGH (sharp shadows, 4x
  MSAA). Changing one of those settings by hand shows CUSTOM.

## v0.22.1: no mid fights, bots that shoot

- **Competitive bots don't take a mid fight from spawn.** No T plan stages in mid any more (A goes long in one or two
  waves, B through tunnels in one or two), the CT holding mid watches the way out of mid doors from beside them where
  nobody in mid can see him, CTs never push mid, routes go round mid when there's another way, and before the
  execute no bot goes looking for someone in mid. Tested (testNoMidFights: every plan and route on both maps, and
  that the mid CT can't be seen from mid) and measured: automated matches on both sides, no bot died in mid before
  the execute.
- **No more jiggling at each other without shooting.** A bot out of cover with an enemy in its sights stays out for
  a burst (it used to duck back on a timer, out of step with the other one), a re-peek on someone it just saw needs
  no new reaction time, and the gun's fire rate keeps counting while it moves. Measured: fights where a bot saw its
  enemy for 2.5 s without firing went from dozens a match to none.

## v0.22: polish

- **Round flow:** a CS-style bar at the top (the clock, or the bomb once it's planted; each side's score; a figure per
  player, dimmed when dead); a round-end banner in the winners' colour with the reason and the **MVP** (most kills, or
  the planter / defuser when the bomb decided it); 7 s before the next round; a **match-end screen** (VICTORY /
  DEFEAT, the score, your line, your rating change, the final scoreboard). Sounds: "go" when freeze time ends, an
  alarm when the bomb is planted, a sting for a round won or lost.
- **Kill feed with icons:** killer and victim in their team's colour, the gun as a small silhouette, a skull for a
  headshot; your kills and deaths have a red edge. **"KILLED NAME"** shows under the crosshair when you get one.
- **Your magazine talks:** a light tick on each of the last quarter's shots, a ping on the last round.
- **Money and armor** bottom left, like CS ($, HP, ARMOR, KIT, C4).
- **Rifles by side** in competitive: the AK-47 (and Galil) for T, the M4A1-S for CT; the full buy picks your side's
  rifle. **Full buys get two flashbangs.**
- **Weapon drops:** guns fall where people die (yours and the bots'). With no primary you pick one up by walking over
  it; **E** swaps for the gun you're looking at. Bots with only a pistol grab rifles they walk over. Deathmatch drops
  vanish after 20 s; online everyone sees the same guns on the floor.
- **Calls (Z):** tell your bots the plan. T: GO A, GO B, RUSH A, RUSH B, PLAY SLOW, SAVE. CT: STACK A, STACK B,
  DEFAULT SETUP, ROTATE A, ROTATE B, SAVE. A teammate answers ROGER.
- **A lived-in world:** bullet holes on every wall (a pale chip round a dark hole, by material; bots' too) that fade
  after ~25 s; soft contact shadows where walls meet the floor and round every prop; a soft shadow under every
  player; all following the ramps.
- **Player animation:** guns kick and flash when enemies fire, come down while they reload (bots now reload: a
  window to push), hands go down and they kneel while planting or defusing, legs tuck in the air, a dip on landing.
- **Online lobby:** HOST opens a lobby; friends who join land in it, pick their team (SWITCH TEAM), and the host sets
  the game, map and bots and presses START. A dropped connection reconnects by itself for 30 s; a failed join says
  why (no answer, different version, full) in a box at the top.
