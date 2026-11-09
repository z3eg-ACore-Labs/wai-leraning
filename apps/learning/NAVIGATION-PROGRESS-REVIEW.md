# WAI navigation progress review - October 27, 2026

## Verdict

The training pipeline is doing work, but the available evidence does not demonstrate better
dungeon navigation. Continued identical training is not justified by checkpoint count alone.

## Evidence scope

Read the committed experiment history and current controller/reward/observation source. The
latest recorded training checkpoint is v66. This review did not read the deployed runtime's
actual checkpoint tensors, query the server, start/stop a run, or change model weights.
Later live results may exist outside the workspace and are not covered.

| Recorded results | Party-runs | Boss kills | Outcomes |
|---|---:|---:|---|
| Initial v1-v5, older 16-input policy | 5 | 0 | Five timeouts |
| Automatically summarized v6-v66, mixed experiment stages | 186 | 0 | 184 off-limits; two timeouts |

The latter summaries total approximately 13.61 party-hours, not wall-clock hours because parties
ran concurrently. They record one death. Low deaths cannot establish competent dungeon combat
when no bosses were reached/killed. The most recent four recorded runs all ended off limits.

Across the first 20 versus last 20 automatically summarized party-runs, median duration fell
from 228.8 to 124.6 seconds and mean visited cells fell from 36.7 to 12.9. This is not evidence
of faster completion: there were no clears. It is also not a controlled regression test, because
observations, boundaries, rewards and party configuration changed during the experiment.

The earlier scripted baseline's boss kills are separate evidence about native PlayerBots plus
predefined routing, not accomplishments of the learned navigation policy.

## What is working

Recorded episodes feed completed updates, checkpoints and history receipts. Exploration memory,
parallel on-policy collection and shared checkpoint updates exist in source and produced recorded
training updates. This is operational progress, distinct from demonstrated playing skill.

## Plausible limitations, not established root causes

- Inputs include position, facing, health/resources, party readiness/combat, boss count, elapsed
  time and local visit counts. They do not include wall/boundary distances, nearby enemy locations
  or a persistent topological map. The model chooses wait or eight short heading-relative moves.
- New-cell reward is 0.02 before lifetime decay, capped at two per episode. Time costs 0.01 per
  second; off-limits termination costs 100. Many failures provide little positive navigation
  experience, and lifetime novelty diminishes even while task progress stays at zero.
- The small feed-forward policy only receives local summaries of visit memory. It has no recurrent
  route memory. PPO weight changes do not prove it has discovered useful state/action relationships.
- A boundary termination is a detector result, not proof that every detection was correct.
  Frequent endings warrant checking geometry/classification against observed positions.
- There is no demonstrated frozen-checkpoint evaluation series proving improvement over the
  initial random policy under identical environment settings.

## Recommended next experiment

1. Keep current checkpoints/recordings. Do not discard or overwrite the historical evidence.
2. Verify ordinary navigable floor and boundary classifications in a small diagnostic session.
3. Evaluate a frozen current checkpoint and an untrained baseline under the same settings across
   multiple runs. Compare boss kills/clear rate, valid reachable coverage, off-limits frequency,
   and time to violation. Exploration counts alone must not reward inaccessible space.
4. Add local navigability/boundary observations and reachable exploration choices, without
   supplying boss coordinates or a prescribed route. Revisit reward scale and novelty decay as
   explicit, separately evaluated changes.
5. Resume longer parallel training only once those measurements show a useful learning signal.

These are recommendations. No training configuration, reward or running party was changed.
