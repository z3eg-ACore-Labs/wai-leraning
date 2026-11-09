# Three independently maintained navigation policies

## Status before implementation

October 28, 2026: design recorded before changes. Existing client control and
behavior cloning are offline tested. Hybrid reinforcement updates and client
reward telemetry are not implemented at this point. No production weights will
be modified during development or tests.

## Separation and update rules

| Policy | Training data | Checkpoint ownership |
|---|---|---|
| Imitation learning through behavior cloning | Human demonstrations only | Own weights; never updated with rewards |
| Reinforcement learning through proximal policy optimization | Its own on-policy client rollouts only | Own weights and optimizer; never fitted to demonstrations |
| Hybrid imitation and reinforcement learning | Its own on-policy client rollouts plus human demonstrations | Own weights, optimizer, consumed-input history and version |

Keeping hybrid current means consuming new eligible human demonstrations and its
own new reward-bearing rollouts. It does not mean copying the latest standalone
weights or replaying another policy's trajectories through proximal policy
optimization. Checkpoint provenance, action semantics, input dimensions, and policy
hash must match. Different policies cannot train on each other's on-policy data.

The existing server-driven episode trainer remains separate: its movement action
contract is not assumed equivalent to client strafe inputs. Existing weights can
initialize a client policy explicitly, with source hash recorded and a fresh client
optimizer. Production server checkpoints are read-only sources, never destinations.

Publish updated client checkpoints as new versioned files under separate track
directories. Advance a small track registry only after successful validation.
Preserve old generations. A single writer lock serializes training/publication.
Keep input hashes so an automatic retry cannot train on the same session twice.
No implicit cross-track copying and no averaging model weights.

## Client controls

| Control | Action |
|---|---|
| F6 | You control movement; record demonstrations |
| F7 | Pause input and recording |
| F8 | Imitation-learning policy takes over navigation |
| F9 | Reinforcement-learning policy takes over navigation |
| F10 | Hybrid policy takes over navigation |
| F12 | Stop and release input |

Each policy key stops prior movement before loading its checkpoint. Missing or
incompatible models pause; there is no fallback. Manual input or focus loss pauses
model control. Training never runs while this process holds movement keys.

An explicit session option enables automatic post-session maintenance. Successful
human recording can update imitation and hybrid policies; completed eligible model
segments update only their owning reinforcement or hybrid policy. A failure to
maintain one track leaves its old registry entry intact and is clearly reported.
The next takeover reloads the published entry. Policy switching during a session
must end the previous segment without assigning later outcomes to it.

## Reward and observation requirements

Extend the existing read-only client lease with authoritative elapsed time, unique
boss-kill progress, party deaths and terminal outcome. Ragefire's existing four
boss identities are reward labels only, never route targets or observations of
unseen boss positions. Count only the leased party in its exact instance.

Reward: boss progress, a dominant full-clear bonus, death penalties, elapsed-time
cost and a small capped exploration bonus. Do not manufacture success from stopping
early. Timeouts can end an eligible segment; focus changes, manual interruption,
transport failure and model switching truncate it with a value bootstrap rather
than inventing terminal rewards. Missing reward telemetry prevents reward training.

Hybrid optimization uses the proximal-policy-optimization objective plus an
explicit weighted behavior-cloning loss on human-only samples. A human-only update
is allowed without claiming reinforcement experience. Demonstrations are retained
as a bounded replay set, so later hybrid reward updates can still use them.

## Validation and build boundary

Test isolated outputs and optimizers, provenance rejection, on-policy log
probabilities, truncation/bootstrap, human/model label separation, duplicate-input
rejection, corrupt/missing reward data and failed-publication rollback. Use synthetic
models and observations only. Validate the C++ telemetry change with native tests
and syntax/style checks. Then commit before the requested one-worker build.

No live game, server startup, database mutation or production training is part of
implementation validation. Announce build start explicitly. A successful build
still needs the user to restart and perform the short live control/reward check.


## Implementation completion

The managed initializer, three isolated optimizers, hybrid objective, bounded human
replay, on-policy hash/probability checks, truncation bootstrap, atomic registry
publication, duplicate-session prevention and retryable sanitized history are now
implemented. Read-only client reward telemetry is implemented for Ragefire.
Validation passed: 69 Python tests, production native reward hook fixtures, C++
syntax and targeted style/format checks. Full-core style findings were pre-existing
and unrelated. No live client or production training ran. Rebuild/publication and
user restart precede live validation. The operating guide is authoritative for
current commands; earlier sections preserve the pre-implementation design.
