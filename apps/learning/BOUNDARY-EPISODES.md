# Tolerant boundary termination

Implemented in source; offline validated, not yet verified in a live dungeon.

The normal strict spline gate remains optional and off by default. A separate read-only monitor
checks the five participants independently inside each party's dungeon instance. It does not supply
a route, boss coordinates or movement destinations.

Default settings (also used when absent from the active config):

```ini
Wai.Episodes.Boundary.Enable = 1
Wai.Episodes.Boundary.ToleranceYards = 3.0
Wai.Episodes.MovementIntegrity.Enable = 0
```

Three yards is approximately 2.74 metres. The tolerance can be configured from 2 to 10 yards. A
participant must remain beyond the tolerance on consecutive checks for at least three seconds
before the run ends. Checks occur about every two seconds, so detection is not instantaneous.
Returning inside, an uncertain reading, death, teleport or managed death recovery clears the
suspicion timer. Brief jumps and minor contacts do not automatically fail a run; prolonged airborne
positions can still be detected instead of skipping all flying/falling actors indefinitely.

## What the check knows

The monitor measures horizontal separation from walkable navigation polygons and vertical separation
from navigation/floor support. It prefers a polygon containing the actual position before searching
more widely, avoiding some neighbouring-slope selection errors. Nearby physical floor support allows
for simplified navigation heights. Supported swimming is accepted. Missing tiles or collision data
end with an infrastructure outcome and are not penalized as a policy mistake; uncertain queries are
logged with a cooldown rather than punished.

This is not an authoritative playable-area map. Exterior geometry with valid navigation/floor support
can pass these checks. Such a case requires a validated allowed-area/exclusion volume or another
reliable geometric constraint; no unverified dungeon-coordinate box has been invented here. It also
does not prove that every wall crossing was detected. First live validation should inspect the
reported escape coordinates and legitimate slopes before trusting the resulting training data.

## Outcome, learning and restart

A confirmed sustained excursion records `boundary_escape` with the participant, distance and tolerance,
then finishes once with `wandering_off_limits`. The learner treats this as a gameplay failure,
applies a -100 terminal penalty (ordinary failure is -50), retains time/death/progress rewards, and
gives no exploration bonus on that terminal transition. PPO/GAE propagates a discounted learning
signal through the recorded rollout; it does not identify a single causally responsible action with
certainty. Prior model weights are loaded normally and updated, not reset.

Once training and checkpoint saving succeed, normal automatic repeat creates a fresh party/run.
With parallel parties, the escaped party waits for its batch peers: one shared update completes
before the whole batch restarts. Infrastructure errors still stop cycling. Action-receipt SOAP
timeouts are a separate existing failure path, not repaired by the boundary monitor.

## Uppercase summaries

At termination (once):

```text
RUN TERMINATED. PARTY 1. REASON: WANDERING OFF LIMITS. CURRENT TIME: 123.4 S. BEST TIME (SESSION): NONE. WEIGHTS CHANGES: NOT YET REPORTED.
```

A full clear instead prints `RUN FINISHED` and `REASON: GOAL ACHIEVED. ALL THE BOSSES KILLED`.
Times are elapsed episode durations. Best time is the fastest full clear in this server session
for matching templates/snapshots, timeout, recovery and tolerant-boundary settings. It is not a
persistent cross-restart leaderboard or a claim about results under different loot/server rules.

After successful training (once per party):

```text
RUN TRAINED. PARTY 1. REASON: WANDERING OFF LIMITS. CURRENT TIME: 123.4 S. BEST TIME (SESSION): NONE. WEIGHTS CHANGES (L2): 0.123456. CHECKPOINT: v8.
```

The values above are illustrative. L2 is measured parameter-change magnitude, not a count of weights
and not proof of improved play. In a parallel batch, each party summary identifies the same shared
model update. No weight-change value is fabricated at the moment of termination.

## Activation

Rebuild and restart the server to load the C++ monitor and summaries. The next controller launched
by `waiepisode start` loads the updated Python outcome/reward handling. Stop/cleanup existing runs
before changing versions. Do not mix a new server outcome with an already-running old controller.
No build or server is started automatically by this implementation work.
