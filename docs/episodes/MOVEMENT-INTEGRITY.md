# Movement integrity for training episodes

## Batch movement preflight (current implementation)

The movement guard now uses native Detour `moveAlongSurface`, matching the core's
walking path traversal, instead of maintaining a second raycast/funnel policy.
Navigation height search follows the core's five-unit mesh approximation window;
physical floor checks still validate the actual standing surface independently.
Low static rays may touch a confirmed floor step within the tile's native climb
limit. Upper-body collisions, dynamic obstacles, non-floor contacts, missing
navigation data and unreachable endpoints remain blocked.

The offline preflight loads Ragefire navigation and collision data read-only and
checks shared production helpers along 13 complete reachable walking routes with
two representative body sizes. It passed 16,592 sampled segments with zero body,
floor or corridor failures. The former foot-ray rule rejected 129 of these
segments. Negative controls still reject walls, dynamic obstacles, excessive
steps and endpoints outside the navigation surface.

This is static walking coverage, not an observed dungeon clear. It excludes live
bot decisions, spawned obstacles, swimming, airborne movement, full client physics
and complete recorded spline trajectories. The scripted native-bot route remains
a baseline; learned exploration and reinforcement learning are separate work.

Future worldserver builds run this preflight before publication when the RFC
fixture exists. A failure prevents selecting/publishing the new executable; a
missing fixture is recorded as skipped. The report includes source/tile hashes.
No server starts and no database is accessed by the preflight.


The harness now requires the companion core spline-validation extension.
This is a server-side guard for the cloned party and its owned units, not a
change to the neural network or an assertion that a dungeon clear has been tested.

## Enforcement

- A small optional callback in `MoveSplineInit::Launch` runs after native spline
  argument validation and before initialization/broadcast. It sees follow,
  chase, route, ordinary point movement and server spline-based charges/jumps.
  The experiment callback only acts on the registered training roster and owned
  units in Ragefire. Other characters and ordinary NPCs retain native behavior.
- Both collision-height and line-of-sight calculations must be enabled, static
  collision tiles must be loaded, and a navigation mesh/query must be available.
  The episode checks the five arrival positions before emitting a run start.
- Ordinary walking must have an exactly normal native navigation result.
  `NORMAL | NOT_USING_PATH`, incomplete/no-path/shortcut results are rejected.
  A native partial route remains usable if the actual spline ends at a reachable
  point and its own start-to-end validation is complete.
- The guard simulates the actual spline using the core's `MoveSpline`, including
  its curved, falling and parabolic calculations. Samples are bounded to 4,096,
  with time steps between 5 and 50 ms based on velocity. This is sampled
  validation, not an exact continuous swept-capsule solver.
- Static and dynamic collision rays check feet/head bands and center/side
  offsets based on the unit's radius. Walking samples must stay near navigation
  polygons, on connected navigation corridors, and near the local floor. A
  local downward floor query avoids selecting a different floor above a tunnel.
  Native liquid status and water navigation polygons support swimming samples.
  Airborne splines are collision-checked along their simulated arc and require a
  valid landing surface, rather than forcing airborne points onto the ground.
- Non-charge player walking cannot exceed the unit's native walk/run/swim speed
  allowance. Native charge state and server-computed airborne curves have
  separate treatment. Flying, cyclic splines, transports and animation splines
  are conservatively unsupported in this experiment.
- A unit-update watchdog audits live positions approximately every 200 ms. It
  compares movement with the approved spline/time window, a travel-distance
  budget and collision rays, and checks local navigation/floor support outside
  approved airborne motion. It covers players and owned units such as pets.
- The existing player teleport hook requires a short-lived one-use permit for
  setup entry at the configured Ragefire entrance. Ordinary exits are allowed
  only from the actual native exit-trigger volume and to its native destination.
  That preserves the existing departure outcome; no learning reward or manual
  neural-weight change is applied here. Administrative clone cleanup clears
  registration before native logout/deletion. Other teleports are rejected.

## Failure semantics and limitations

The first violation records its reason/member slot, stops the affected unit's
movement and blocks further registered splines. The world-thread episode manager
emits `movement_violation` and ends with `invalid_movement_environment`. Such a
run is not a successful clear and should not be used as valid performance/RL
evidence. This version does not turn a geometry failure into a learned penalty.
The future controller must distinguish an ordinary unreachable action request
from a server shortcut/data-integrity failure before shaping rewards.

The guard has bounded tolerances and sampling. It depends on correct extracted
navigation/collision data; it cannot establish that those files perfectly match
the client. Thin obstacles, unusual models, narrow passages, moving geometry and
multi-level corners require actual map testing. Checks can reject legitimate
native requests conservatively; inspect the reason rather than weakening the
checks globally. Packet-driven displacement without an approved spline,
non-setup spell teleports, flying and transport movement are not certified by
this first version and can invalidate the experiment. Native spline-based
jump/fall support does not prove all knockback implementations work in game.

The native combat/route baseline remains in place. Learned route discovery,
exit reward shaping, bot transition recording and policy optimization are still
separate work. Existing human-only recordings do not automatically record bots.

## Evidence

The actual harness, movement guard, core callback source and modified native
spline launcher pass MSVC syntax-only checks with the existing project headers.
Three isolated executable suites pass with assertions enabled: clone-data
invariants; navigation-flag/time/displacement/curve rules; and the actual core
callback registry (no-validator behavior, registration ownership and rejection).
Formatting and scoped codestyle checks pass.

No full worldserver link, configuration run, server start or in-game collision
test was performed. After the requested build, test an ordinary route, following,
combat chasing, charge, spline jump/fall, the real exit, a blocked wall and
missing navigation/collision data before collecting trusted training episodes.

## Build

The core patch changes one existing `.cpp` and adds a narrow header/implementation;
no widely included existing header changed. A configure is needed to collect the
new source and module loader. Use the existing workspace wrapper:

Use the normal configure/build commands for your target AzerothCore checkout. The old workspace-specific wrapper is not part of this module.

Configure must succeed before Build. The wrapper preserves its resource limits,
live logs, source manifests and disk guard. No server is started by these commands.


Entry audit sequencing correction (2026-10-24): initial position auditing could
run before setup's Ready checkpoint. Scoped actors now reject ordinary spline
launches until their owner passes entry validation; observation auditing starts
only after that checkpoint. The explicitly permitted setup teleport remains
separate. Owner readiness resets on each roster/setup entry and cleanup.
Collision/navigation rules and tolerances are unchanged. Rejected initial and
observed surfaces now include XYZ, hover, nav result/height and static floor in
the violation reason. These diagnostics contain no account linkage.

Read-only offline probes using the generated RFC tiles and native Detour/VMAP
code verified the configured entrance: nearest nav Z -17.4951, static floor
-17.8379 at configured Z -17.84. This validates that point, not the failed actor's
actual unlogged position or an entire live run. The previous receipt occurred
before episode start (instance/elapsed zero), so live behavior must be retested
with the corrected sequence and detailed diagnostics. No server build/start or
navigation generation was initiated during investigation.


Shared-edge corridor correction (2026-10-24): native Detour probe over generated
RFC data reproduced four clear raycasts where nearest endpoint polygon identity
differed from the ray's final polygon, but closest endpoint on that final polygon
was identical in XYZ. Corridor validation now accepts that geometric endpoint
within 1 mm horizontally and 5 cm vertically after requiring a successful clear
ray with no visited-buffer truncation. It does not accept wall hits or distant/
different-floor endpoints. Assertions-enabled probe evaluated 26,125 clear rays,
resolved all four reproduced identity mismatches, and retained 239 wall-hit
rejections. Actual-source syntax, five isolated suites and scoped style passed.
The user's failed spline was not logged in full, so this is a proven guard bug,
not a confirmed complete explanation of that live failure. Failures now identify
body/ground/corridor check and segment endpoints/time for precise reproduction.
No build, generation or server start performed; live retest required. The baseline
still chooses fixed boss destinations using native pathfinding, not RL exploration.


Exact first-route failure reproduced and corrected (2026-10-24): the reported
segment passes static/dynamic body and standing-surface checks, while native
Detour raycast stops at a shared polygon vertex. Its native findPath connects
the endpoints using two adjacent ground polygons and its funnel inserts the
same vertex on the original straight segment. The production corridor helper
now permits this vertex case only when a complete non-off-mesh corridor exists
and every ordered funnel corner remains within 1 mm of the original segment.
It does not substitute a detour around obstacles. Truncated/partial paths and
off-mesh polygons are rejected. Existing floor, sampled collision, endpoint and
actual-position checks remain enabled.

Read-only replay uses the actual generated RFC tiles and the shared production
helper; it accepts the exact logged failure. A local scan accepts 26,127 segments
and rejects 237; only two formerly blocked raycasts receive vertex recovery.
Native regression source lives with offline episode tests, enabled by optional
WAI_NAVIGATION_FIXTURE pointing at a generated mmaps fixture root. No database,
server, generation or full build is used for this regression. Live run still
requires user rebuild/retest. This remains a native scripted-route baseline.

## Optional movement guard (default off)

`Wai.Episodes.MovementIntegrity.Enable = 0` disables the experimental spline,
position and teleport integrity guards, including the initial navigation check.
It leaves native bot pathfinding, episode setup, boss/death logging and ordinary
episode termination active. Set it to `1` before starting a new episode to opt in.
The start event records `movement_integrity_enabled`. Runs with the guard off
are useful for observation, but do not establish collision integrity.

A rebuild is required to introduce this setting. Existing configurations need no
new assignment because its default is off. Native route driving remains scripted.
