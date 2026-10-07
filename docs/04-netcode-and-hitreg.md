# 04 — Netcode & Hit Registration

This is the part of the game players will judge us on. The research is clear: **"what I saw is what I hit"** and **consistent movement** are the top requests, and CS2's problems come from places where the client's picture and the server's rewind don't agree.

## Goals (measurable)

| Metric | Target |
|---|---|
| Server tick rate | **128 Hz** fixed (7.8125 ms) |
| Server tick CPU time (10 players, worst case) | **p99 ≤ 2.0 ms** |
| Hit-reg agreement: client-predicted hit vs. server result, for shots with ≥ 1 cm margin | **≥ 99.9%** (ideally 99.99%) |
| Client interpolation delay | **~1 tick + adaptive jitter buffer** (typically 10–20 ms) |
| Maximum lag-compensation rewind | **200 ms** (server config) |
| Peeker's advantage window at 35 ms ping each way | **≤ 60 ms** (Riot's published bar for VALORANT) |
| Bandwidth (down, per client, 10 players) | ≤ 64 KB/s typical; ≤ 128 KB/s worst |

## Architecture overview

```
   CLIENT                                                SERVER (authoritative, 128 Hz)
 ┌─────────────────────────────┐                      ┌──────────────────────────────────┐
 │ Raw input (QPC timestamps)  │                      │ Receive usercmds (jitter buffer) │
 │   │                         │   usercmds @128 Hz   │   │                              │
 │   ▼                         │  (redundant last 3)  │   ▼                              │
 │ Build usercmd for tick N ───┼─────────────────────▶│ Simulate tick: movement (quant.) │
 │   │                         │                      │   │                              │
 │   ▼                         │                      │   ▼                              │
 │ Predict own movement        │                      │ Process shots in sub-tick order  │
 │ (re-sim unacked cmds)       │                      │   → lag-comp rewind to client's  │
 │   │                         │   snapshots @128 Hz  │     render time → ray vs hitbox  │
 │   ▼                         │  (delta vs. ack'd)   │   │                              │
 │ Interpolate others at       │◀─────────────────────┼── Visibility cull per client     │
 │ (server_time - interp)      │                      │   Delta-compress & send          │
 │   │                         │                      │   Store hitbox history ring      │
 │   ▼                         │                      └──────────────────────────────────┘
 │ Render @ uncapped FPS       │
 └─────────────────────────────┘
```

## The hybrid model: tick-quantized movement, sub-tick shots

This is the key design decision. It responds directly to the two biggest CS2 complaints.

| Thing | Timing model | Why |
|---|---|---|
| **Movement keys** (WASD, jump, crouch, walk) | **Snap to 128 Hz tick boundaries** | Same input gives the same velocity every time, so counter-strafe and jump muscle memory works. Inputs between ticks don't create "infinite" velocity variations (the core complaint about CS2 movement). At 128 Hz the maximum quantization error is 7.8 ms, below human discrimination for key timing. |
| **View angles** | Applied **every render frame** locally (zero-latency camera). Sent per tick as the latest value, and **per shot as the exact value at click time** | The camera must never lag the mouse |
| **Shots** (and grenade releases) | **Sub-tick timestamped**: the shot carries `(tick, fraction ∈ [0,1))`, the exact view angles at the click, and the client's **render/interp time** at that moment | Hit detection matches the exact frame the player saw, and fire-rate cadence is exact (a 600 RPM rifle doesn't jitter between 12 and 13 ticks) |

## Client side

### Input
- Read raw mouse input with `GetRawInputBuffer` (batched) each frame and accumulate per-frame deltas, each with a QPC timestamp.
- Mouse-to-view: `yaw -= dx * sensitivity * 0.022°` (CS parity). Use double precision for accumulation; send angles as quantized (e.g. 1/65536 of a turn).
- A button press records the QPC time, which is mapped to `(tick, fraction)` with the client's estimate of the server's tick clock.

### Clock sync
- The client keeps a smoothed estimate of server tick time (NTP-style on snapshot timestamps plus RTT/2). It runs its command clock slightly **ahead** of the server by `RTT/2 + small safety margin`, so commands arrive just before the server needs them.
- The server reports how far ahead or behind each command arrived. The client nudges its tick clock smoothly (time dilation of ±1–2%) and never jumps.

### Usercmd (sent at 128 Hz, unreliable, each packet repeats the last 2–3 commands to cover loss)
```
struct UserCmd {
  uint32 tick;                 // command tick
  uint16 buttons;              // movement buttons (tick-quantized)
  int16  forward, side;        // quantized
  uint16 yaw, pitch;           // view at end of tick (quantized)
  uint8  shot_count;           // 0..N shots in this tick
  struct Shot {
    uint8  sub_tick_frac;      // fraction of tick, 1/256 resolution (~30 µs at 128 Hz)
    uint16 yaw, pitch;         // exact view at click
    uint32 interp_tick;        // what the client was rendering for OTHER players:
    uint16 interp_frac;        //   tick + fraction of the interpolation time
    uint32 snapshot_ack;       // newest snapshot the client had
  } shots[];
  uint32 checksum_predicted;   // hash of predicted player state (desync detection)
};
```

### Prediction and reconciliation
- The local player's movement is predicted using **the exact same simulation code** as the server.
- On each snapshot: take the server state for the acked command, **re-simulate unacked commands**. If the corrected position differs from the predicted one, smooth only the *visual* error over about 50–100 ms; never smooth the simulation itself.
- **Quantize authoritative state on the server every tick** (position, velocity) to the same precision used in snapshots. Client and server then start each tick from bit-identical state, which removes most prediction mismatches caused by float differences between compilers and CPUs.

### Interpolation of other players
- Render other players at `t_render = server_time_estimate - interp_delay`.
- `interp_delay = 1 tick + jitter_buffer`, where the jitter buffer adapts to measured snapshot arrival jitter (clamped to 0–2 ticks). That gives about 8–24 ms, much smaller than CS:GO's default `cl_interp_ratio 2` at 64 tick (31 ms).
- No extrapolation of enemies (it lies about positions). If snapshots stall, hold the last pose, then show a "lag" indicator.

## The hitbox contract (the most important rule)

> **The hitbox the player sees must be computed by the same deterministic function, from the same inputs, on client and server.**

CS2's Animgraph rebuild and its desync reports show what happens when gameplay hitboxes are a by-product of a complex cosmetic animation system.

1. **Gameplay pose function:** `HitboxPose = f(position, view_yaw, view_pitch, crouch_amount, move_dir_blend, move_speed_blend, weapon_class, jump/land phase, plant/defuse state)`.
   - Every input is **replicated in snapshots** (quantized) and is part of the interpolated state.
   - `f` uses a small, fixed set of baked animation clips sampled at deterministic times, with **no IK, no physics and no random layers**.
   - Output: about 19 capsules/OBBs in world space.
2. **Rendered character = the same skeleton.** The visible mesh is skinned from the gameplay skeleton. Cosmetic extras (cloth, foot IK on slopes, hand IK, flinches, weapon sway) may **only** affect bones that have **no hitbox** or move them by less than a tight tolerance (for example < 0.5 cm). Automated tests enforce this.
3. **Interpolation is defined on inputs, not bones.** The client interpolates the pose *inputs* between snapshots and evaluates `f`. The server does exactly the same with its history.
4. **Debug parity tool:** `cl_showhitboxes 2` draws the client's hitboxes and the server's rewound hitboxes (sent back in debug mode) for every shot. Any mismatch is a bug.

## Server-side shot processing (lag compensation)

For each tick, the server processes all players' commands. For shots:

1. **Order** all shots in the tick by `sub_tick_frac`. If two players kill each other, the earlier shot wins. A design-tunable allows "same-tick trades" if testing prefers them.
2. **Validate the claimed render time** `t_r = interp_tick + interp_frac`:
   - `t_r` must be within `[now - min(rtt + interp + slack, MAX_REWIND), now]`.
   - `t_r` must be consistent with `snapshot_ack` (the client can't claim to have rendered a state it hadn't received).
   - If it's outside the window, **clamp** (or reject at extreme values) and log it for anti-cheat stats.
3. **Rewind:** for each potential target, read its stored pose-input history at the two ticks bracketing `t_r` and **interpolate exactly as the client did**, then evaluate `f` to get hitboxes.
   - History storage: 10 players × 128 ticks/s × 0.25 s ≈ 320 entries of about 40 bytes of pose inputs per player. Trivial.
4. **Shooter origin:** the shooter's eye position is the **shooter's own state interpolated at `sub_tick_frac`** between its previous and current tick. The client renders its own camera the same way.
5. **Spread:** deterministic `seed = hash(player_id, cmd_tick, shot_index)`. Spread and recoil offsets are identical on client and server.
6. **Trace:** broadphase (bounding sphere per player) → per-capsule ray test → world occlusion and penetration trace against the static BVH (world isn't rewound, because it's static; dynamic doors get history too if we add them).
7. **Apply damage** in the current tick. Emit a hit event to all relevant clients (shooter gets confirmation; victim gets direction and damage).
8. **Cap rewind** at `MAX_REWIND = 200 ms`. Players above that see their shots validated against a less-rewound world. **Lag is the lagger's problem, not the defender's.**

### "Shot behind the wall" mitigation
The victim already reached cover on their screen but still died because the shooter's view was old. Mitigations:
- Low interpolation delay and 128 Hz (above)
- Regional servers with ping targets ≤ 35 ms for most players
- Rewind cap
- Optional (to playtest): a **"victim's recent cover" grace check**. If the victim was occluded from the shooter's eye at the server's *current* time *and* the rewind exceeded X ms, reject. Overwatch does a similar sanity check. This trades a small amount of shooter fairness for defender fairness, so tune it with data.

## Snapshots (server → client)

- Sent every tick (128 Hz). They are **delta-compressed against the client's last acked snapshot** and bit-packed with quantization:
  - position: 1/32 unit precision in map-bounded ranges (about 19–21 bits per axis)
  - angles: 16 bits; pose inputs: 4–8 bits each
- **Per-client visibility culling** (anti-wallhack): an enemy is included only if potentially visible from the client's eye (PVS → coarse visibility grid → a few rays against hitbox corners → smoke occlusion), **with a lookahead margin** based on both players' speed × latency so enemies never "pop in" late.
- Sounds (footsteps, reloads) are sent only if audible to that client.
- Rough size: 10 players × ~25 bytes delta ≈ 250 B/tick + header and events ≈ 300–500 B/tick → **~40–64 KB/s**. Fine on any broadband connection.

## Testing hit reg (this is how we earn trust)

1. **Golden determinism tests (CI):** replay recorded usercmd streams through movement and pose code on Windows/MSVC and Linux/clang. Assert identical (quantized) results.
2. **Bot duel harness (CI nightly):** headless clients run scripted peeks, strafes, jiggle-peeks and sprays against each other through a `tc netem` network with latency (10–150 ms), jitter (0–20 ms) and loss (0–5%). Each client logs *what it rendered* (hitboxes at render time + shot ray). The server logs its result. **Metric: hit-reg disagreement rate.** Fail the build if it regresses.
3. **Hitbox parity test:** sample random pose inputs, compare client-rendered skeleton vs. gameplay hitbox skeleton, and assert the tolerance.
4. **High-speed camera tests** (later, Riot-style): measure real peeker's advantage end-to-end on LAN and WAN setups.
5. **Production telemetry:** the client sends (sampled) its local hit prediction with each shot. The server compares results and exports `hitreg_disagreement_rate` per server, region and build to Grafana. **Consider publishing this publicly**, since no competitor does and CS players would notice.

## Server performance budget (per 7.8 ms tick, 10 players)

| Step | Budget |
|---|---|
| Receive + decode commands | 0.05 ms |
| Movement sim (10 players × 1–2 cmds) | 0.15 ms |
| Shots + lag comp (worst case: 10 players spraying) | 0.30 ms |
| Grenades / utility / smoke volumes | 0.15 ms |
| Visibility culling (10 × 9 pairs) | 0.30 ms |
| Snapshot build + delta + send (10 clients) | 0.40 ms |
| Game rules, economy, logging, demo write | 0.15 ms |
| **Total p99** | **≤ 2 ms** (leaves headroom; 3–4 matches per physical core is possible, but start with 1) |

## Spectating, demos and replays

- The server records the **full tick stream** (pre-culling) to a demo file. It's used for anti-cheat review, hit-reg debugging, highlights and a GOTV-style spectator relay (with a delay).
- Client demos record what the client received (post-culling) for personal review.
