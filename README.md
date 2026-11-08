# Hitreg Lab

**A UE5 C++ first-person test range for studying how shots feel and how they register.** It has:
- data-driven weapons;
- controller aim assist;
- client-predicted firing with server-side rewind lag compensation;
- a peeker's-advantage meter;
- a kill cam.

Every system is measured, not just built.

![Lag compensation at 150 ms: rewound (green) vs current (red) hitboxes](docs/media/hitreg_lagcomp.gif)

*Unreal Engine 5.8 · C++ · 13 automation tests · dedicated-server measurements · system walkthrough: [docs/WALKTHROUGH.md](docs/WALKTHROUGH.md)*

## Results

**Lag compensation.**
- The setup: a client aims perfectly at the strafing target *as it draws it* and fires single taps.
- The measure: the share of on-screen hits the server confirmed ([data](docs/data/hitreg_bench.csv)).
- With compensation on, every on-screen hit registered up to 150 ms of round trip.
- Without it, 68% registered even at 0 ms (remote players are drawn ~2 ticks in the past), and only 23–30% at real pings.
- At 250 ms the 200 ms rewind cap applies, deliberately, and registration drops to 34%.

![Hit registration with and without lag compensation](docs/media/hitreg_registration.png)

**Peeker's advantage.**
- The setup: a scripted peeker strafes past a corner while a holder watches it.
- The measure: the time between when the peeker's screen first shows the holder and when the holder's screen first shows the peeker.
- 160 trials over a dedicated server and two clients ([data](docs/data/peek_sweep.csv)).
- Lag compensation doesn't remove this advantage. Tick rate and interpolation delay shrink it, but round-trip time dominates.

![Peeker's advantage vs tick rate and latency](docs/media/peek_advantage.png)

| | 20 Hz | 30 Hz | 60 Hz | 128 Hz | 60 Hz, 1-tick interp |
|---|---|---|---|---|---|
| 0 ms RTT | 175 ms | 127 ms | 76 ms | 45 ms | 61 ms |
| 50 ms | 254 | 200 | 136 | 104 | 121 |
| 100 ms | 283 | 256 | 189 | 158 | 172 |
| 150 ms | 365 | 297 | 247 | 198 | 219 |

**Other measurements** (from the automation tests and the HUD):
- **Server lag-compensation trace:** ~10–200 µs per shot. The analytic hitbox tests are cheap; the world line trace dominates.
- **Clock sync:** ~4 ms error with 40 ms of jitter each way. The engine's own GameState clock was off by 40–70 ms in the same runs.
- **Kill cam recording:** ~670 bytes/s per player.
- **Aim assist (bot-thumb benchmark, 50 seeded trials):** median time to put the crosshair on the head went from 1408 ms (off) to 1308 ms (full), and tracking rose from 11% to 16%. A modest, honest effect. The point is that it's measured.

## Systems

| System | Where | Notes |
|---|---|---|
| Weapons | `Weapons/` | `UHLWeaponDefinition` data assets: fire mode, RPM, falloff, spread bloom, recoil patterns, ADS. Firing is frame-rate independent (`FHLFireClock`). Projectile launcher. |
| Shot pipeline | `UHLWeaponComponent` | The client predicts the hit marker and tracer. `Server_Fire` carries the client's *view time*. The server validates fire rate (token bucket), origin and timestamps, then confirms or retracts the predicted marker. |
| Lag compensation | `Net/HLLagCompensationSubsystem` | Every character's pose is recorded each server frame. Shots are traced against hitboxes rewound to the shooter's view time, capped at 200 ms. Hit tests are analytic (ray-sphere and ray-OBB in `HLHitboxMath`), so nothing has to be teleported. |
| Snapshot interpolation | `AHLCharacter` | Remote players are drawn from timestamped snapshots at `server time - RTT/2 - N ticks`. That render time is exactly what the client reports, so the server rewinds to what the client saw (`HitregLab.Net.Rewind`: within 2 cm). |
| Server clock | `Net/HLServerClockComponent` | NTP-style pings; offset from the lowest-RTT sample, slewed 1 ms per frame. |
| Aim assist | `Aim/` | Radial dead zone and response curve, then slowdown in a bubble sized in cm, then rotational pull (only while the player is giving input), then bullet magnetism. The Aim Assist Tuner editor tab edits it live and runs the benchmark. |
| Peek meter | `Measure/` | Server-orchestrated trials; each client reports first sight in synced server time. `-PeekSweep=N -PeekFull` runs unattended. |
| Hit-reg bench | `Measure/HLHitregBenchComponent` | `-HitregBench=N` on a client: perfect aim at the drawn target, per RTT, with compensation on and off. |
| Kill cam | `Replay/` | A client-side 8 s ring at 30 Hz, quantized to 16 bytes per pose. Played back from the killer's eyes with scrub, pause and slow motion. F8 saves a versioned `.hlreplay` for bug reproduction. |

## Two bugs the measurements caught

1. **The movement component only sends a client's moves to the server when the actor replicates movement.** I had turned replicated movement off to draw remote players from my own snapshots. The peek test showed the peeker moving on its own screen but never on the server. The fix: keep it on, and make simulated proxies ignore the engine's replicated position.
2. **Remote players were rendered at `ServerNow - interp`,** which is *ahead* of the newest snapshot a client can have, since snapshots arrive RTT/2 late. The rewind test caught it once its client buffer only held snapshots that would actually have arrived.

## Running it

- **Open:** `HitregLab.uproject` in UE 5.8. Maps: `Lvl_Range` (strafing dummies), `Lvl_Arena` (fight bots), `Lvl_Peek`.
- **Keys:**
  - WASD, mouse or gamepad; LMB/RT fire, RMB/LT ADS, R reload, 1–4 weapons.
  - F1 net overlay, F2 lag compensation, F3 simulated RTT, F4 tick rate, F5 interpolation ticks.
  - F6 aim assist mode, F7 rewind view, F8 save kill cam, F9 aim benchmark, F10 add bot.
- **Measurements:** two clients and a dedicated server (see `Tools/record_clips.ps1` for the command lines):
  - `-server -PeekSweep=8 -PeekFull -QuitAfterSweep`
  - `-game 127.0.0.1 -HitregBench=30 -QuitAfterBench`
- **Charts:** `python Tools/plot_peek.py` and `python Tools/plot_hitreg.py`.
- **Tests:** `UnrealEditor-Cmd.exe HitregLab.uproject -ExecCmds="Automation RunTests HitregLab; Quit" -unattended -nullrhi` (13 tests).
- **Content:** the weapon and aim assist assets and the maps are created by `Tools/Content/hl_content.py` (run in the editor). Level geometry is built from code by `AHLLevelBuilder`.

## References

- Riot Games, "Peeking into VALORANT's Netcode"
- Activision, "Fighting Latency on Call of Duty Black Ops III" (GDC 2016)
- Insomniac Games, "Techniques for Building Aim Assist in Console Shooters" (GDC 2013)
- Blizzard, "Replay Technology in Overwatch: Kill Cam, Gameplay, and Highlights" (GDC 2017)
- Gabriel Gambetta, "Fast-Paced Multiplayer"

## Credits

Directed by Mohammad Mohammad, implemented with Claude Code as an AI coding assistant. Placeholder art is built from engine primitives.
