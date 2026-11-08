# Hitreg Lab: system walkthrough (interview prep)

Read this until you can explain each section out loud, without notes. Every system lists:
- what it does;
- where the code is;
- the one or two decisions an interviewer will poke at.

## The shot pipeline, end to end

1. **Input.** `AHLCharacter::InputFirePressed` sets the trigger on `UHLWeaponComponent`.
2. **Fire timing.** `UHLWeaponComponent::TickComponent` asks `FHLFireClock::Advance` which shots fall inside this frame, at their exact scheduled times. Firing is therefore frame-rate independent: 600 RPM gives the same shot count at 30 and 144 fps (`HitregLab.Weapons.FireRate`).
3. **Building the shot.** `FireShot` does four things:
   - takes the camera ray;
   - bends it with bullet magnetism if you're on a gamepad (`UHLAimAssistComponent::BendShot`);
   - applies spread with a seeded RNG (`FHLSpreadModel::SampleCone`, seed = player id × shot sequence);
   - applies recoil and view kick locally.
4. **Prediction.** The client runs the same hit test the server will run, against the poses it is *drawing* (`PredictHit`). It shows a *predicted* hit marker and tracer immediately.
5. **The request.** `Server_Fire(FHLShotRequest)` carries:
   - origin and direction (spread already included);
   - `ViewTime`: the server time this client's screen was showing other players at;
   - `FireTime`, a sequence number and the weapon index.
6. **Server validation** (`ServerValidate`):
   - weapon, alive and ammo checks;
   - fire rate: a token bucket (`FHLRateLimiter`) refilled at the gun's rate +10%, holding up to 3 tokens so packets that arrive bunched together pass but a modified client can't out-fire the gun (`HitregLab.Net.FireRateCheat`);
   - origin within 2 m of where the server has your eyes;
   - timestamps that aren't absurd.
7. **Lag-compensated trace.** `UHLLagCompensationSubsystem::TraceShot`:
   - world geometry first (line trace, characters ignored);
   - then every other character's hitboxes, tested analytically at the **rewound pose** for `ViewTime`, clamped to `MaxRewindMs` (200).
8. **Result.**
   - Damage is applied: distance falloff × hit zone multiplier × headshot multiplier.
   - `Client_ShotResult` confirms or **retracts** the predicted marker.
   - A multicast draws the tracer for everyone else.
   - The HUD counts predicted, confirmed, retracted and rejected shots.

## Why remote players are drawn from my own snapshots

- **The setup.**
  - Every server tick, each character writes `FHLNetSnapshot {ServerTime, location, yaw, pitch}` (replicated, skip owner).
  - Clients push snapshots into an `FHLPoseHistory` and draw simulated proxies at `RenderTime = ServerNow - RTT/2 - InterpTicks / TickRate` (`AHLCharacter::GetRemoteRenderTime`).
- **Why not the engine's smoothing?**
  - It hides *when* a client is drawing someone, so the server can't rewind to exactly that moment.
  - With a known render time, the client reports it as `ViewTime`, and the server rewinds to the same instant. `HitregLab.Net.Rewind` checks that the rewound pose matches the drawn pose to within 2 cm.
- **Bug found in testing.** At first I rendered at `ServerNow - interp`. That's *ahead* of the newest snapshot a client can have (it arrives RTT/2 late), so the buffer just held the newest sample. Subtracting RTT/2 fixed it. The test now builds its client buffer only from snapshots that would have arrived.
- **Another bug found in testing.** I had turned off the engine's replicated movement. But `UCharacterMovementComponent` only sends a client's moves to the server when replicated movement is on (`CharacterMovementComponent.cpp`, the `IsReplicatingMovement()` check before `SavedMoves.Push`). The peek test caught it: the peeker moved on its own screen but never on the server.
  - The fix: keep it on, and make simulated proxies ignore `OnRep_ReplicatedMovement` and `PostNetReceiveLocationAndRotation`.

## The server clock

- **The problem.** `AGameStateBase::GetServerWorldTimeSeconds` is refreshed only every few seconds and isn't corrected for round trip.
- **The fix.** `UHLServerClockComponent` pings: the client sends its time, the server replies with its own, and the client measures the RTT. `FHLClockSync` keeps 8 samples and uses the **lowest-RTT** one: the less time a ping spent in flight, the less the "reply took half the round trip" assumption can be wrong.
- **No jumps.** The offset is slewed at 1 ms per frame, so remote players don't jump when a new sample arrives.
- **Measured.** Under 10 ms error with 40 ms of jitter each way (`HitregLab.Net.ClockSync`, it measured ~4 ms).

## Hitboxes

- **Shape.** Characters are yaw-only rigid bodies: a head sphere, a torso box and a legs box (`AHLCharacter::MakeDefaultHitboxes`). The meshes are built from the same table, so what you see is exactly what you hit.
- **Hit tests** are pure maths in `HLHitboxMath`:
  - ray-sphere (quadratic);
  - ray-oriented-box (slab test in the box's local space).
- **Why analytic, not physics traces?** It is deterministic, it doesn't need physics bodies moved back in time, and it is cheap (~10 µs per shot measured on the server). Rewinding means testing maths at an old pose, not teleporting colliders.

## Lag compensation: what it does and doesn't do

- **What it does.** It makes a hit register where the *shooter saw* the target. Without it, at 100 ms ping you have to lead every moving target.
- **What it doesn't do.** It doesn't remove **peeker's advantage**; it moves the advantage toward the shooter.
  - The holder sees the peeker late because of RTT/2 (the peeker's moves reach the server), plus up to one server tick, plus the holder's interpolation delay, plus the other RTT/2.
  - The peeker sees the holder as soon as their own *predicted* position clears the corner.
- **The rewind cap is a trade-off.** At 200 ms, a 250 ms player gets partial compensation, and nobody can be shot more than 200 ms after leaving a line of fire. Real games pick numbers like this; Riot's VALORANT netcode post explains theirs.

## The peeker's-advantage meter

- **How a trial runs** (`UHLPeekMeterSubsystem` on the server, `UHLPeekComponent` on each client):
  1. The server places the peeker behind the wall and the holder aiming at the corner.
  2. The peeker's client strafes out at full speed.
  3. Each client reports, in synced server time, when **its own screen** first has line of sight to the other player's head.
  4. Advantage = holder's time − peeker's time.
- **Unattended runs.** `-PeekSweep=N -PeekFull` on a dedicated server sweeps RTT × tick rate × interpolation delay and writes `Saved/Measure/peek_*.csv`.
- **Results.** See the README table.

## Aim assist

Based on Insomniac's "Techniques for Building Aim Assist in Console Shooters" (GDC 2013). All the maths is pure (`HLAimAssist`).

- **Stick curve.**
  - A *radial* dead zone keeps diagonals diagonal.
  - Then a response exponent: 2 gives fine control near the centre.
  - Holding the stick at the edge ramps in an extra turn speed.
- **Target selection.** Each target has a "bubble" defined in **centimetres**, so it shrinks on screen with distance and assist doesn't grow stronger the further away a target is. The best target is the one closest in bubble radii, not in raw degrees.
- **Slowdown (friction).** The look rate is multiplied by `SlowdownMinScale` at the target's centre and smoothstepped back to 1 at the edge of the bubble.
- **Pull (rotational magnetism).** The camera gets a fraction of the target's angular velocity relative to you, plus a small centring term. It is **only** active while you're giving look or move input: no input, no rotation. That is the line between assist and aimbot (`HitregLab.Aim.NoInputNoPull`).
- **Bullet magnetism.** Shots within 0.9° of a head or torso centre are bent onto it. Per-weapon scale: the sniper gets half, the launcher none.
- **The bot-thumb benchmark** (`HLAimBench`):
  - a scripted thumb with a 200 ms reaction delay, tremor noise and stick saturation;
  - it uses an efference copy: it knows how far it has turned since the stale frame it is reacting to;
  - the target strafes at running speed 20 m away;
  - metrics: time to hold the head for 100 ms, overshoot and tracking.
  - Measured: assist cut median time-to-acquire from 1408 to 1308 ms and raised tracking from 11% to 16%.
  - My first thumb model had no efference copy and *never* acquired in either mode. Good to mention as an example of validating the benchmark itself.

## Kill cam

- **Recording.** Each client records what it *displayed* (`UHLReplayComponent`): every character at 30 Hz plus tracers, in an 8 s ring, quantized to 16 bytes per pose (mm positions, 16-bit angles). That is ~670 bytes/s per player (`HitregLab.Replay.Size`).
- **Playback.**
  - On death, the last ~5 s become a clip played from the killer's eyes with local ghost actors.
  - Slow motion around the kill; scrub, pause and speed controls.
  - Real characters are hidden locally during playback.
- **Why client-side?** No bandwidth and no server memory. The trade-off: it shows the victim's view of the killer, which is already delayed by interpolation, not the killer's actual screen. A server-recorded kill cam (like Overwatch's, see the GDC 2017 talk) shows the truth, at the cost of shipping the recording.
- **Bug repro.**
  - F8 / `HLSaveReplay` writes a versioned file; `HLPlayReplay <file>` plays it back.
  - A file from a newer format version is refused, not misread.
  - Round trip: save, load, then identical hash and identical frames (`HitregLab.Replay.RoundTrip`).

## Questions to rehearse

1. Why doesn't lag compensation fix peeker's advantage? What would reduce it?
   - Higher tick rate.
   - Lower interpolation delay (at the cost of jitter tolerance).
   - Lower RTT.
2. What's the exploit if the rewind cap is too high, and what if it's too low?
3. Why the lowest-RTT ping sample for clock sync?
4. Why does the server re-validate the fire rate when the client already enforces it?
5. Why is pull gated on player input?
6. How would the kill cam change with dedicated servers and 100 players?
7. What does rollback netcode do differently from this?
