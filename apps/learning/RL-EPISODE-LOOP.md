# Learned navigation with automatic between-run training

User requirements confirmed 2026-10-25. Offline validation is complete; full server build and live
integration remain pending. The current operational description and validation evidence are in
[RL episode operations](RL-EPISODE-OPERATIONS.md). The requirements below are the earlier design;
its separate-watcher description was replaced by one external controller/trainer process.

## Ownership and execution

The navigation policy must select local movement from episode one. Do not provide boss coordinates,
waypoints, a fixed boss order, or recorded-route replay to this policy. Native PlayerBots combat may
remain, but its autonomous non-combat grind/travel/follow decisions must not override the tank's
learned navigation. Followers may follow the policy-controlled tank. Retain the scripted baseline
only as an explicitly selected evaluation mode, with policy identity logged on every run.

The existing human-character action lease is not the required five-bot episode control interface.
Implement an episode-scoped interface with bounded observations/actions, sequence IDs, model version,
stale-action rejection, heartbeat expiry and exclusive navigation ownership. Use local observations
and normal server navigation/collision data without exposing undiscovered boss destinations.

## Automatic cycle

1. Load a navigation checkpoint, or initialize an exploratory policy for the first run.
2. Start one isolated party episode. Record observations, proposed and executed actions, action
   probabilities/value estimates as required by the selected algorithm, rewards, next observations,
   terminal/truncated flags, episode ID and policy version.
3. On completion, wipe, exit or timeout, release policy control and finalize the trajectory. A separate
   writer publishes a completion manifest atomically only after writing and closing the complete data.
   Preserve failed gameplay episodes; quarantine incomplete/infrastructure-corrupted recordings.
4. The external trainer watches completed manifests, validates schemas/checksums and prevents duplicate
   consumption. Train automatically between runs, outside all server/world/map update threads.
5. Save the optimizer state, policy/value weights, normalization state, RNG state, counters and
   provenance to a new PyTorch checkpoint. Publish atomically after finite-value/schema validation.
   Record metrics in TensorBoard. A failed update preserves the prior checkpoint and stops automatic
   advancement with an explicit error rather than silently claiming successful training.
6. Load the validated checkpoint for the next fresh episode. Initially use one party and one fixed
   policy version per rollout. Do not mix stale on-policy data if an on-policy algorithm is chosen.

Use existing PyTorch tooling. Select and document the RL algorithm before implementation; imitation
learning examples and outcome-only JSONL receipts are not substitutes for RL transition data.

## Reward requirements

- All required bosses dead defines success, independent of discovery/kill order.
- Give each required boss one progress reward per episode and a larger full-clear reward.
- Among successful runs, prefer shorter completion times and fewer deaths, with zero deaths preferred.
- Count each actual player death once; resurrection/release/teleport events are not additional deaths.
- A faster failed run must not outrank a successful clear solely because it ended sooner.
- Exits, wipes and timeouts are failures. Infrastructure failures are not strategic failures to learn.
- Do not reward repeated easy mob kills, repeated boss-credit events, standing still or reset loops.
- Log reward components and a reward-config version. Coefficients remain to be chosen and validated;
  no claim that current coefficients already optimize clear rate or speed is justified.

## GM access in server logs

Draft server source now logs all five clone names once they have logged in. At run start it prints
the current tank's concrete appear command, GM/invisibility steps, outdoor teleport, self-only
unbind instructions and instance comparison. These are client-chat instructions, never commands
executed automatically by the server. The existing-party start path also receives the run-start help.
The source change has not been linted, compiled or observed in game under the execution hold.

## Performance requirement

Do not train, launch blocking child processes or flush every transition on world/map threads.
Use bounded queues and background I/O. A finished episode must relinquish navigation ownership and
disable episode movement audits; clone cleanup/retention must be explicit. Record timing and queue
health so performance regressions can be distinguished from model behavior.
