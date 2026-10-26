# Recoverable movement violations — design draft

Status: proposed, not implemented or deployed. No server or build started for this draft.

## Goal and evidence

The user observed bots clipping into exterior-looking geometry while remaining in the dungeon
instance. The existing integrity handler consumes a text violation and ends the entire episode.
Earlier checks also rejected legitimate movement. Therefore a generic integrity failure is not
sufficient evidence for a learning penalty. No predefined boss route is introduced by this design.

## Proposed behavior

1. Replace the single text failure with a typed result: valid, confirmed invalid, uncertain, or
   environment unavailable. Include participant, party, instance and decision sequence.
2. Check proposed movement before execution. A confirmed collision or validated forbidden boundary
   rejects the movement and produces a negative reward; the episode continues at the same position.
3. Monitor executed movement as well, including native follow, chase and gathering. If a confirmed
   escape occurs, stop affected movement and recover to a recently verified safe party checkpoint.
   Revalidate that checkpoint in the same instance before using it. Never use an unchecked latest
   coordinate as a safe anchor, or recover into a currently unsafe combat location.
4. Uncertain geometry produces diagnostics without a policy penalty. Missing or incompatible
   navigation data is an environment fault, not a bad model decision. If no valid recovery anchor
   exists, stop with a distinct infrastructure outcome rather than teleport blindly.
5. Native forced movement, knockbacks, falling and explicitly permitted setup/death-recovery
   teleports must be classified separately. Do not blame the latest model action for every follower
   violation. Only attribute a policy penalty where the recorded decision caused the invalid move.

## Bounds and geometry

A map ID or instance ID is not a playable-area boundary. Navigation connectivity can also include
unwanted exterior geometry. Begin with validated local path, body-clearance and surface checks;
if those cannot distinguish the reported exterior region, add independently verified allowed-area
volumes or excluded portal regions. Such geometry constraints must not encode routes or boss
destinations. Height bands alone cannot distinguish stacked floors reliably. Log uncertain cases
for inspection rather than presenting approximate checks as physical proof.

## Reward and recording contract

Proposed initial policy penalty: -1 per confirmed invalid decision, at most once per sequence;
boss reward remains +10. These are tuning proposals, not validated hyperparameters. Use cumulative
penalty counters in observations so polling/retries cannot lose or duplicate a penalty. Preserve
all underlying events in the private journal, but rate-limit human console messages per party.
Store original and recovery position, reason, provenance, policy version and decision sequence.

Recovery is an explicit discontinuity: no exploration reward, no intermediate visited-cell credit,
and no ordinary movement transition across the teleport. Break or mask temporal credit across
recovery and bootstrap from the recovered state; retain an explicit invalid-action penalty for
the responsible decision. Existing saved model weights remain usable. Training happens after the
episode/batch, not by manually editing weights at the time of violation.

## Isolation and validation before rollout

The current integrity subsystem has one roster and one pending violation; parallel mode therefore
disallows it. Replace that ownership with per-party state before supporting recovery in parallel.
Each party needs independent safe anchors, counters, permits and console cooldowns.

Required checks: legitimate reported floor segments remain accepted; confirmed wall crossings are
rejected; missing tiles are not penalized; same-instance exterior escapes are detected only with
validated evidence; penalties are exactly once despite repeated observations; no teleport exploration
bonus; forced movement is not misattributed; unsafe/stale anchors are refused; party recovery cannot
move or penalize another party. First live validation uses one party, then two isolated parties.

An implementation would change the episode module and movement validator, plus the learner reward
and transition handling, and would require a server rebuild. This document alone changes no behavior.
