# 07 — Multiplayer: where we are and how to get there

**Update:** a first version is built: online deathmatch for 2-8 friends (see prototype/README.md, "Online"). It's the quick "trust each other" model (each PC simulates itself; the shooter decides hits), hosted from the game over ENet. The rest of this note is the original investigation and still describes the road to a proper server-authoritative version.

The original investigation follows. The design target is already written up in [04 — Netcode & hit registration](04-netcode-and-hitreg.md). This note covers the practical path from today's Crisp code to playing against a friend.

## What we already have that helps

- **A deterministic 128-tick simulation.** `playerMove` (movement) and `fireBullet` (shots) live in the shared sim library, use no frame time and no randomness for your bullets. A server and a client running the same inputs get the same result, which client-side prediction needs.
- **"What you see is what you hit" is already the rule.** Shots test against the positions and facings drawn on the frame you clicked (`lastDummyRenderPos`, `shownYaw`). Online, that becomes lag compensation: the server rewinds the other players to the time you saw them.
- **Hitboxes that turn with the player** (`rayHitsDummy`) and one damage path (`recordDamage`, `hurtPlayer`, `hurtBot`). Both carry straight over to networked players.
- **Bots that fight bots** (competitive). That's most of the "N players with teams" logic a server needs.
- **Tiny bandwidth.** 10 players × ~40 bytes of state at 64–128 Hz is a few KB/s per client.

## What has to change first (the refactor)

1. **Split the game state out of `main.cpp`.** Today `Game` mixes the simulation (players, weapons, rules, bomb, economy) with rendering, audio and the HUD. A server needs the simulation without any of that. Move it into the sim library (`game.h/.cpp`) and keep `main.cpp` as the client (input, prediction, rendering, sound).
2. **Make "the player" one of N players.** You are special-cased everywhere (`g.player`, `g.hp`, `g.comp.money`) next to the bots (`dummies`). A networked game needs a `Player` struct per slot (movement state, weapons, HP, armor, money, team), where a slot is controlled by a human (inputs over the network) or a bot (`BotBrain`).
3. **Inputs as user commands.** Each tick the client sends what it pressed, its view angles and a shot timestamp. The simulation then only ever reads usercmds, never the keyboard. That's also what makes demos/replays possible.

Effort: about 2 sessions. Nothing visible changes for you, and the existing tests keep everything honest.

## The network library

| Option | Good | Bad |
|---|---|---|
| **ENet** (recommended to start) | One small C library, builds with the existing CMake (FetchContent), reliable + unreliable channels over UDP, used by many shipped games | No encryption, no NAT traversal |
| **Valve GameNetworkingSockets** | What `docs/03` picked: encryption, reliability, and on Steam the free relay network (SDR) that solves NAT | Heavier to build (protobuf, OpenSSL/libsodium), more to set up in CI |
| Steamworks networking | Matchmaking, lobbies, relays, friends invites | Needs a Steam app ($100 Steam Direct fee) |

Start with ENet for the LAN prototype; move to GameNetworkingSockets/Steam only if the game goes onto Steam.

## How you'd actually play with a friend

1. **Same network (LAN):** works straight away.
2. **Over the internet:**
   - **ZeroTier (already installed on this PC)** or Tailscale puts you and your friend on a virtual LAN. No port forwarding and no server needed: the easiest way to test early.
   - **Port forwarding** UDP on the host's router: works, but is fiddly for non-technical friends.
   - **A rented dedicated server** (a small Linux VPS, about $5–10 a month, runs a 10-player 128-tick server easily) for always-on play.
   - **Steam relays**, if the game is ever on Steam.

## Step by step

| Step | What | Rough size |
|---|---|---|
| 1 | Refactor: game state into the sim lib, N players, usercmds | 2 sessions |
| 2 | 1v1 listen server (one player hosts) with ENet: connect, send usercmds, server simulates, snapshots back; interpolate the other player | 2 sessions |
| 3 | Client-side prediction + reconciliation of your own movement (the sim is deterministic, so this is mostly bookkeeping) | 1 session |
| 4 | Lag-compensated hits: a server history ring of hitbox poses, rewind to the shooter's view time (`docs/04`); test harness: two simulated clients with fake latency/jitter/loss, measuring "hit on screen but missed on server" | 2 sessions |
| 5 | Modes over the network: deathmatch, then competitive 5v5 (humans fill slots, bots fill the rest), scoreboard, chat | 2–3 sessions |
| 6 | Headless dedicated server build (Linux), a simple server browser or "connect IP" | 1–2 sessions |

Roughly **10–12 sessions** to a solid 5v5 over the internet, with a playable 1v1 over ZeroTier after about **5**.

## Risks and things to decide

- **Hit-reg trust is the whole point of the project** (see 04). The bot-duel test harness in step 4 isn't optional; it's how we prove "what you see is what you hit" online.
- **Cheating:** a server-authoritative design stops speed and teleport hacks. Wallhacks and aimbots need later work (server-side visibility culling, which `docs/04` describes).
- **The Dust2 copy is Valve's map layout.** It's fine for you and a friend, but anything public (a server list, a Steam page) needs original maps first.
- **Tick rate:** 128 Hz is the target; 64 Hz halves the bandwidth and CPU if hosting gets expensive.

## What was built (v0.8 to v0.10)

The owner asked for the easiest, most performant way first, so the shipped design is lighter than the plan above:
a **listen host that relays**, and every game simulates its own player (no prediction or reconciliation needed;
zero lag for yourself). The shooter's game decides hits against what it showed; the victim's game applies them.
That trusts every PC, which is fine between friends; the server-authoritative steps above are still the way to
go if Crisp ever goes public.

- **Stage 1 (v0.9):** UPnP port opening, the address to share on the HUD, player names, bot names.
- **Stage 2 (v0.10):** grenades (the throw is sent; every game flies it identically and hurts only its own
  player), crouching (hitboxes squat with the model), the weapon each player holds, a protocol version check.
- **Stage 3 (v0.10):** online competitive: the host runs the match and the bots, joined games run their own
  player, money and armor; players split between the sides or all against bots; bots fill the places.

Bandwidth per player: about 9 KB/s up with packet headers (128 states a second, 29 bytes each), the same down
per other player, plus in competitive about 8 KB/s down for the bots (64 Hz, 11 bytes a bot) and a small match
message 8 times a second.
