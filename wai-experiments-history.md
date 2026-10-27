# WAI experiments history

Chronological record of the experiment, user decisions, observed results and resulting adjustments.
Dates are stated only where supported by the conversation or inspected artifacts. Automated result
entries use UTC. Source changes, offline validation and live behavior are separate evidence levels.
Credentials, account linkage, machine-specific paths and raw private logs are deliberately excluded.

## Initial direction — before the first learned dungeon episodes

**Goal:** a party controller that can eventually complete unfamiliar dungeons, rather than a fixed
script for one dungeon. The human demonstrates gameplay; later bots collect their own experience.
Actions should represent abilities or intentions, not a fixed keyboard layout. Voice input is not
part of the gameplay training signal.

**First implementation:** server-side observations paired with requested actions and subsequent spell
events. Pre-request state matters: recording only after handlers/cast checks risks pairing an action
with information that became available after it. The recorder was made independent of any model so
recorded human runs could be trained on later. Character selection should follow the active human
character instead of requiring a GUID before startup. Recordings should have dungeon boundaries and
recognizable dungeon names rather than one server-lifetime stream.

**Tools and first model:** the earlier spell-imitation example is distinct from dungeon-navigation
RL. It used a small 16→12→8→6 network and a human Maul example to explain normalized inputs, weights,
ReLU, activations and spell probabilities. NumPy/PyTorch, TensorBoard and Netron have different roles;
viewing network structure or changing weights does not by itself establish effective gameplay.

**Lesson:** record state, action and outcome consistently; keep recordings usable without a model;
measure held-out performance rather than treating training fit as proof of generalization.

## Repeatable dungeon experiments — clone isolation and operational lessons

The user chose Ragefire Chasm as the first controlled environment. Five configured characters are
read-only templates: create distinct clones with the same level, talents and equipped gear, empty
carried inventory, and XP locked. Never run the original templates or reuse their identities as the
experimental party. All four required bosses must die for completion; deaths and elapsed time matter.

The implementation reuses native character dump import/export, bot login, group, movement and
recovery functionality. Separate parties need separate groups and dungeon instances. Clone accounts
and ownership ledgers must be reusable across runs while protecting unrelated or online characters.

Early failures included incompatible movement-map versions and overly strict movement checks that
rejected apparently legitimate travel. Repeatedly repairing individual sampled path segments was
not sustainable. The user explicitly chose an **unrestricted experiment** for now: leave strict
movement integrity off, allow questionable geometry shortcuts, and observe whether any clear emerges.
This is not evidence that movement is player-legal; constrained navigation is a later comparison.

The deployment workflow also separated builds from the working server, used versioned executables
and manifests, and reused runtime assets. Source updates and a selected executable filename do not
prove which process is actually running. Inspect deployed configuration and episode policy receipts.

GM observation exposed another distinction: matching coordinates is not the same as matching an
instance. A source fix makes appear use a full transfer between copies and reject conflicting
personal bindings. Observation instructions must use the current clone name and instance, because
the next episode replaces the previous party. Live multi-party observation still needs validation.

## October 25 — scripted baseline was not the intended learned route

**Verified deployed run:** the inspected baseline explicitly reported scripted boss routing. It
killed Oggleflint (one of four bosses), recovered three dead members, and eventually ended in a full
wipe after approximately 24 minutes 26 seconds with eight recorded deaths. No learned controller or
training updates were present for that run.

**User correction:** the experiment should discover the route from run one, without a predefined
boss order or destinations. The apparent partial success of native PlayerBots plus a script was not
proof of a trained model. The source was changed to require learned navigation, reject scripted
configuration and reject missing learner configuration rather than silently use a scripted fallback.

**Recovery distinction:** partial deaths can recover through a surviving bot after combat. A full
wipe is a terminal episode, not a recovery anchor. The intended learned loop trains from that failure
and then creates a fresh episode. An offline regression subsequently verified that order.

## October 26 — first verified learned run/training cycle

The loop launches only when an administrator starts episodes, not on ordinary server startup.
Model inference and training run outside server threads. Native PlayerBots handles combat and
followers; the model chooses wait or one of eight short heading-relative movement goals. The server
validates episode identity, sequence, policy version and decision freshness.

An episode has a configurable 60-minute budget. Timeout is a gameplay failure that can train the
model, then repeat. Continuous cycling remains active until stopped or an infrastructure/training
failure halts it. Stopping mid-run retains the last saved checkpoint but does not train that
unfinished run. Cleanup removes clones, not previous model updates or recording files.

### Inspected completed updates

These results were read from local saved trajectories, training receipts and summaries. Times below
are UTC, corresponding to the overnight runs on October 26 in Toronto. Every row used the older
16-input policy, without the later visited-area memory. Each row has a completed training receipt.

| Recorded end (UTC) | Policy → checkpoint | Outcome | Bosses | Deaths | Seconds | Decisions | Total reward | Weight-change L2 |
|---|---|---|---:|---:|---:|---:|---:|---:|
| 2026-10-26 05:32:28 | v0 → v1 | Timeout | 0/4 | 2 | 3601.0 | 893 | -95.9080 | 0.942936 |
| 2026-10-26 06:32:41 | v1 → v2 | Timeout | 0/4 | 0 | 3600.0 | 896 | -85.8975 | 0.565162 |
| 2026-10-26 07:32:55 | v2 → v3 | Timeout | 0/4 | 0 | 3600.7 | 896 | -85.9069 | 0.478073 |
| 2026-10-26 08:33:08 | v3 → v4 | Timeout | 0/4 | 1 | 3600.8 | 894 | -90.9076 | 0.445684 |
| 2026-10-26 09:33:21 | v4 → v5 | Timeout | 0/4 | 0 | 3600.1 | 896 | -85.8989 | 0.364557 |

**Finding:** recording, training, checkpoint saving and automatic repeat were functioning. All five
completed runs killed zero bosses. We have not demonstrated useful route learning or successful
completion. Decreasing weight-change magnitude is not evidence of better play.

At a later inspected snapshot, a sixth episode was still running with five living online clones.
That snapshot was not a completed result and is intentionally absent from the completed-update table.
The history had not disappeared: event journals and training trajectories were configured in
different folders. Automatic clone cleanup also makes previous bot names disappear while keeping
their recordings. Future summaries should distinguish a live snapshot from a finalized outcome.

## October 26 — feedback, gathering and exploration adjustments

**User observation:** a warrior fought while nearby party members appeared idle. Logs supported
some members being in combat while others were not, but did not establish the underlying assistance
failure. Combat-assistance diagnosis remains open; it was not solved merely by adding exploration.

**Looting/gathering:** episode setup had explicitly disabled these behaviors to avoid competing
movement. That was too broad for the requested dungeon behavior. Source now enables native loot and
gather on all party members during a run and yields navigation to active loot targets, pending loot
operations and gathering casts. Native profession skills, tools and eligibility still apply. Empty
clone inventory is retained; tools are not magically supplied. C++ checks passed; live behavior
requires the corresponding build. Boss deaths also gained one uppercase headline per counted boss,
using existing deduplication and retaining the structured training event.

**Exploration limitation:** the original policy had no explicit visit memory and mostly delayed
task rewards. PPO does not identify and delete detours. It can reinforce unnecessary wandering near
a successful outcome; credit assignment is statistical, not knowledge of which turns were needed.
Parallelism alone would collect more experience without fixing this information gap.

**Implemented adjustment:** derive visit counts from the tank's reported XYZ coordinates in 8×8×4
yard cells, append ten memory features to the original sixteen inputs, and add a small diminishing
new-cell reward capped at two points per episode. Boss kills remain +10 and completion +1000;
deaths cost five points and elapsed time costs 0.01 per second. Revisits do not keep paying. Large
position jumps, wait actions, failed movement and death-associated movement earn no discovery bonus.
Height bands are only a floor approximation, not a geometry map or wall validator.

Episode visit memory resets each run; lifetime counts persist in the checkpoint and reduce repeat
discovery bonuses. Older checkpoints are backed up and migrated: learned weights are retained, new
input connections begin at zero, and optimizer state resets for the changed tensor dimensions.
An offline regression verifies unchanged predictions immediately after migration. Twenty-four
focused tests passed at this milestone. An already running controller does not hot-reload this code.
Live exploration improvement remains unverified; no clear-rate gain is claimed.

## October 26 — parallel parties sharing one model

The user explicitly selected **one shared model**, not separate models per party, and requested
`waiepisode start N` with N representing party count.

**Implemented source:** one to four independently scoped party runtimes, groups, instances, queues,
recovery and journals. Each runtime clones the same read-only templates. Secondary dedicated
accounts are created on demand through the existing account API when requested; existing account
and ownership safeguards remain. `watch N` prints a party's current GM observation instructions.
Stop, cleanup and status apply to the selected pool.

One external process collects concurrently with frozen weights, waits for all parties to finish,
then performs one shared PPO update and checkpoint write. Early finishers wait for the slowest party.
Terminal boundaries stop advantage/reward propagation between parties. Per-episode visit memory
stays independent; lifetime-count increments merge after collection. Training or infrastructure
failure stops the batch and keeps the last checkpoint. Per-party artifacts and a shared batch
manifest preserve what contributed to each update. Strict movement integrity remains off as requested.

**Validation:** 31 focused offline tests passed before the history feature, including concurrent
collection, two shared batches, four parties resuming checkpoint version five, separate files,
advantage boundaries, count merging and failed-update preservation. All four C++ runtime
instantiations passed syntax checks and scoped style. No full build or live multi-party episode was
run during implementation. See `PARALLEL-EPISODES.md` for operation and remaining live checks.

## Related environment change — guaranteed boss loot

The separate, user-authorized loot patch was backed up, rehearsed transactionally with rollback,
applied and verified: 802 creature variants, 86 chest templates and 20,674 copied loot rows at 100%.
Matching server overflow-mail support is required; normal difficulty/quest/class/faction conditions
remain. Live kill/mail behavior has not been verified. Loot distribution can affect the experimental
environment, so it should be considered when comparing later runs. It is not model learning.

## Ongoing recording policy

The history-recording feature passed three additional isolated-Git tests, bringing the focused suite
to 34 tests. They verify history-only commits with unrelated staged work preserved, sanitized and
deduplicated retries, and retained pending receipts without altering a model after a Git failure.

Record material decisions, implementation changes, validation results, deployment changes, observed
outcomes and resulting lessons in this file. Preserve uncertainty and failed attempts. Do not turn
unverified user observations into measured results, or synthetic tests into live-game evidence.

After each successful future training update, the controller appends a sanitized result and commits
only this Markdown file locally. History/Git failures retain a private pending receipt for retry
without discarding a saved model or stopping training. This begins on the updated controller's next
launch; an existing old process will not acquire the hook automatically. Nothing is pushed upstream
without authorization. New implementation milestones are also recorded and committed during work.

## Automated training updates

Future controller-generated entries appear below. Public summaries exclude raw episode IDs,
character/account linkage, private logs, credentials and machine-specific paths.

## October 26 — implementation checkpoint and Git-operation safeguard

The parallel server implementation is committed as `29ed056`; shared collection and history-hook
implementation as `81555fc`; the initial curated history as `f8f3228`. These are source milestones,
not evidence of live parallel execution. A subsequent safeguard defers automatic history commits
during unresolved merges, cherry-picks or rebases, preserving a pending receipt instead of staging
conflict markers. The focused suite now passes 35 tests. No new server build, restart or live
parallel run was performed. The next required validation is a rebuilt server with two parties.

## October 26 — observation timeout interrupted an exploration run

A user-reported run ended after approximately 19 minutes 37 seconds with zero bosses and one
death: an observation SOAP read exceeded its two-second timeout and the controller exited.
The exploration-input migration had succeeded beforehand, preserving previous weights and
saving a backup while resetting optimizer state. This is an infrastructure interruption,
not evidence that migration failed or that the model learned from the interrupted episode.

Observation reads now retry timeouts up to three attempts, including party-specific reads.
Each attempt retains the two-second timeout; movement and other mutating commands are never
automatically retried because their execution may already have occurred. Authentication failures
are not retried. Persistent delays still stop the controller. The underlying server delay has
not been established. This Python-only fix needs a controller restart, not a server rebuild;
live recovery from a delayed server remains unverified.

## October 26 — recoverable boundary design and parallel-start observation

The user observed same-instance movement into exterior-looking geometry and requested penalties
instead of discarding the whole run. The approved next step was a draft, recorded in
MOVEMENT-RECOVERY-DRAFT.md: confirmed violations reject/recover and receive attributable penalties;
uncertain geometry and missing data are separate. Recovery earns no exploration credit. No runtime
behavior has changed for this draft. The existing integrity subsystem is still single-party.

The user also reported requesting two parties but seeing only the first. The latest workspace
recording contains only party-one creation/start receipts; that corroborates missing evidence of a
second start, not its cause. The latest build log reports successful parallel-capable publication.
Current live pool status and second-party setup errors are still needed to establish what happened.

## October 26 — secondary-account startup race

The user supplied a party-two status receipt with an empty phase, and party one's terminal receipt
showed an administrative stop rather than a gameplay failure. Source inspection identified a race:
native account creation queues writes, but secondary-party startup queried the new account immediately
and aborted if it was not yet visible. This explains a possible first-start failure; the specific
original party-two error receipt has not been supplied, so the live cause is not proven conclusively.

The source now waits across world updates, bounded to 30 seconds, before resuming ordinary clone
validation. Pending setup is isolated per party, cancellable by stop/cleanup, and prevents an
overlapping pool start. New preparation/ready/error events and numbered queue receipts expose the
stage that previously appeared silent. No credentials are logged. This change requires a rebuild;
no server or full build has been started during this fix.

<!-- wai-training:720c4c9c1a718ef5390e5ac50495b11e4149e94cc179dfa75632f3eb85933f6f -->
### 2026-10-26 12:22:37 UTC — checkpoint v6

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | timeout | 0/4 | 0 | 3600.2 | 895 | 26 | 222 | 2.000 |

Total training reward: -83.9017; weight-change L2: 0.481899.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:7b9351dda4786e07e02343914d5914183e1c33f218b9e3bc9ad7a2063b22bf85 -->
### 2026-10-26 13:22:51 UTC — checkpoint v7

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | timeout | 0/4 | 0 | 3600.7 | 896 | 26 | 153 | 2.000 |

Total training reward: -83.9057; weight-change L2: 0.598085.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

Validation: C++ syntax checks passed, including all four party runtime instantiations; scoped core style checks passed. Native regression checks passed, including delayed account visibility, timeout, cancellation and independent wait state. Source fix committed as 56b49b3. No full build or live two-party startup was run.

## October 26 — readable episode counters

At the user's request, human-facing WAI_EPISODE server-log JSON now displays BOSSES_KILLED and DEATHS in uppercase. Canonical episode JSONL retains the original lowercase keys, preserving existing recording consumers and schema compatibility. This is a display-only C++ change, requiring a rebuild/restart to become visible. No model weights or rewards change.

Validation: C++ syntax and scoped style checks passed. No build/server was started and live display is not yet verified.

## October 26 — action receipt timeout stopped cycling after checkpoint seven

The user supplied a controller traceback showing a timeout while awaiting an action-command receipt, distinct from the earlier observation-read timeout. The existing observation-only retry intentionally does not resend mutating commands with uncertain outcomes. Source defaults confirm unlimited episode cycling (zero episode limit), stopping on exceptions rather than at checkpoint seven. Automated history confirms checkpoints six and seven completed from two 60-minute timeouts, both with zero bosses, zero deaths and 26 exploration inputs. The subsequent user-reported episode ended as controller_disconnected after about 22 minutes 48 seconds with zero bosses/deaths. Its unfinished trajectory did not produce another successful training receipt. Completed checkpoint updates remain saved; this failure does not erase them. The underlying server response delay remains unproven. Safe future recovery needs action-receipt reconciliation or idempotent decision handling; blind retries could duplicate execution. No runtime changes, builds or server starts were performed for this diagnosis.

## October 26 — canned chatter during autonomous runs

The user observed bots exchanging predefined lines and asked whether these were failed command loops. Source inspection found a separate canned-chat handler: situational speech can seed replies, and received bot speech can trigger further canned responses. Its free-bot classification uses random-account membership rather than actual bot-session status. Dedicated training clones therefore fall into the more permissive non-random-sender branch, bypassing the random-bot cooldown gate. This provides a plausible feedback mechanism without failed command execution. The exact initial message was not captured, so this specific exchange's origin remains unverified. No chatbot behavior was modified, build started or live chat test performed.

## October 26 — tolerant termination replaces recover-and-continue draft

The user chose to end sustained off-limits excursions, penalize the recorded sequence, train, and restart instead of teleporting back within the same run. Source implementation uses a separate per-party post-movement monitor: over three yards from navigation/floor support for at least three seconds. Minor contacts and uncertain queries do not immediately terminate; missing geometry is an infrastructure failure rather than a policy penalty. Walkable exterior geometry can still pass; authoritative playable-area constraints remain a limitation, not a claimed solved detection case.

A confirmed escape ends once as wandering_off_limits, receives a -100 terminal penalty, and is accepted for PPO training before normal repeat. The terminal transition receives no exploration bonus. Existing checkpoints remain compatible. Parallel parties retain one shared batch update and restart after all batch peers finish. Uppercase RUN TERMINATED or RUN FINISHED messages show reason and elapsed/session-best time; RUN TRAINED reports the actual shared parameter-change L2 only after training succeeds. Session bests compare matching template snapshots and monitor/recovery/time settings; they are not persisted across server restarts.

Module implementation committed as 6098446; learner/reward/tests/operations guide as fe0c84f. All 39 focused Python tests passed, including off-limits training before repeat and mixed success/escape parallel batches. C++ syntax instantiation, native boundary timing/isolation regressions and scoped style checks passed. No full build, deployment or live dungeon test was performed. Rebuild/restart and a fresh controller are required. Existing action-receipt timeout handling remains a separate unresolved interruption path.

<!-- wai-training:41eda2c2c5c9a643713b58a1f391e5ed5ddde91ae688673cb7b7d5324074005f -->
### 2026-10-26 16:29:03 UTC — checkpoint v8

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 407.6 | 99 | 26 | 26 | 0.251 |
| 2 | wandering_off_limits | 0/4 | 0 | 98.4 | 22 | 26 | 8 | 0.117 |

Total training reward: -204.4919; weight-change L2: 0.104242.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:60b97b68010dd8d6f8d6f95709c95b0b5b4057c153c833f1ab931a1d18f3ad1c -->
### 2026-10-26 16:33:35 UTC — checkpoint v9

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 26.1 | 4 | 26 | 4 | 0.042 |
| 2 | wandering_off_limits | 0/4 | 0 | 258.9 | 62 | 26 | 20 | 0.150 |

Total training reward: -202.4573; weight-change L2: 0.159471.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:b09741fb6b1ef3a39722e2d8689df909fd513c75f04f15db771188213d12b9ba -->
### 2026-10-26 16:47:15 UTC — checkpoint v10

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 379.4 | 92 | 26 | 33 | 0.433 |
| 2 | wandering_off_limits | 0/4 | 0 | 805.3 | 198 | 26 | 45 | 0.323 |

Total training reward: -210.8894; weight-change L2: 0.394487.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

## October 26 — baseline Corpse Explosion strategy

At the user's request, Death Knight bots now receive an independent Corpse Explosion combat strategy by default, at the top combat priority band (99, below reserved pull actions). The action uses learned-spell range/radius metadata and cached corpse candidates, checks combat, cooldown and runic power, and selects an eligible corpse whose blast reaches the current live enemy. It excludes invalid/consumed corpses and does not sacrifice a living ghoul. The earlier low-priority Unholy AoE entries and forced Blood Presence prerequisite were removed. This is deterministic PlayerBots behavior, not a neural-network training update.

PlayerBots source commit f0d3392f. C++ syntax checks for changed translation units, the full module codestyle checker, creator registration checks and clang-format validation passed. No full build or live combat test was run; server rebuild/restart required. Best-effort upstream fetch failed, synchronization unverified. Existing unrelated changes were preserved.

<!-- wai-training:ec81c7e0f9ae68162e8c38d755c969082f7bae5e0e594f4da6ad424545a68429 -->
### 2026-10-26 16:53:57 UTC — checkpoint v11

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 387.8 | 94 | 26 | 30 | 0.311 |
| 2 | wandering_off_limits | 0/4 | 0 | 182.7 | 43 | 26 | 18 | 0.113 |

Total training reward: -205.0771; weight-change L2: 0.199909.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

## October 26 — post-fight corpse cleanup and CraftTrail request

The user requested priority corpse cleanup after combat and made a CraftTrail dropdown fix the higher priority. Investigation found no installed CraftTrail source in the workspace, only an older preview ZIP containing InkTrail; the active addon path was requested rather than patching an unverified version. CraftTrail remains pending that path.

Independent server work extends the existing Corpse Explosion strategy to non-combat defaults and removes the live-enemy requirement once out of combat. Learned spell, cooldown, runic power, eligible corpse, range and line-of-sight checks remain. Corpses can be cleared after the last enemy dies, but resource/target constraints can prevent a cast. Changed-source C++ syntax, full module codestyle and clang-format checks passed. No full build, server start or live combat test was performed; rebuild/restart required. Existing unrelated work remains preserved and upstream synchronization remains unverified from the earlier failed fetch.

<!-- wai-training:5221a825ccec72b133d58968d8a4289cdaba5b4418cc0d4823e8b6bdd0c1721d -->
### 2026-10-26 16:55:34 UTC — checkpoint v12

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 82.3 | 18 | 26 | 7 | 0.057 |
| 2 | wandering_off_limits | 0/4 | 0 | 22.1 | 3 | 26 | 2 | 0.008 |

Total training reward: -200.7733; weight-change L2: 0.061790.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:b408566dc785d4a08f92e28ac215fb8c6c4cec96bdf363ab459ffa331ade5e19 -->
### 2026-10-26 16:56:58 UTC — checkpoint v13

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 34.1 | 6 | 26 | 4 | 0.027 |
| 2 | wandering_off_limits | 0/4 | 0 | 70.2 | 15 | 26 | 5 | 0.043 |

Total training reward: -200.7680; weight-change L2: 0.059118.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:37ecb172ef00ab6d4be4139e1b3b3ef1ad3929596f0b8e3eed4634892f286f86 -->
### 2026-10-26 17:00:31 UTC — checkpoint v14

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 74.3 | 16 | 26 | 7 | 0.050 |
| 2 | wandering_off_limits | 0/4 | 0 | 198.8 | 47 | 26 | 17 | 0.100 |

Total training reward: -202.3749; weight-change L2: 0.057062.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:db8ec84f78ea8b897e10062c9835056227010408d91ccb7cc6cbdfe3757a5352 -->
### 2026-10-26 17:15:27 UTC — checkpoint v15

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 447.5 | 109 | 26 | 38 | 0.243 |
| 2 | wandering_off_limits | 0/4 | 0 | 881.6 | 217 | 26 | 53 | 0.372 |

Total training reward: -212.4752; weight-change L2: 0.277662.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:712b578b6a6d228b7db622853b4fec7777158c3be9f43d6eca4c44d14bb4c3df -->
### 2026-10-26 17:27:01 UTC — checkpoint v16

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 680.6 | 167 | 26 | 35 | 0.158 |
| 2 | wandering_off_limits | 0/4 | 0 | 74.3 | 16 | 26 | 7 | 0.054 |

Total training reward: -207.1328; weight-change L2: 0.112792.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:0c2eecb25afaffcf37cfd60365e83c3c1c6dd623a967a715a0e05b8daff4b930 -->
### 2026-10-26 17:28:57 UTC — checkpoint v17

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 102.4 | 23 | 26 | 10 | 0.085 |
| 2 | wandering_off_limits | 0/4 | 0 | 62.3 | 13 | 26 | 6 | 0.039 |

Total training reward: -201.3186; weight-change L2: 0.036174.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:c395dc48d1a884f288838217219b98d4bdbc0cb70c3db2b23215aaba1bb86247 -->
### 2026-10-26 17:37:07 UTC — checkpoint v18

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 475.6 | 116 | 26 | 28 | 0.170 |
| 2 | wandering_off_limits | 0/4 | 0 | 78.3 | 17 | 26 | 8 | 0.050 |

Total training reward: -205.1138; weight-change L2: 0.106451.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:562ed656f7fb6248108645e234cf2cb39590476a83d9a820a0942a1706ab4a2d -->
### 2026-10-26 17:51:34 UTC — checkpoint v19

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 853.6 | 210 | 26 | 61 | 0.630 |
| 2 | wandering_off_limits | 0/4 | 0 | 455.7 | 111 | 26 | 42 | 0.304 |

Total training reward: -211.9547; weight-change L2: 0.174753.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:3f66611f2bc64448550e3506ee2bdf6fe5abc80bb3495c840e57b69a59661769 -->
### 2026-10-26 18:02:12 UTC — checkpoint v20

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 154.6 | 36 | 26 | 16 | 0.088 |
| 2 | wandering_off_limits | 0/4 | 0 | 624.2 | 153 | 26 | 36 | 0.224 |

Total training reward: -207.2746; weight-change L2: 0.076951.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:b3d63b0579dc8fb49d37500fc5901a962a38f1cfcaab695d9f9096edfee7f845 -->
### 2026-10-26 18:04:36 UTC — checkpoint v21

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 130.5 | 30 | 26 | 11 | 0.062 |
| 2 | wandering_off_limits | 0/4 | 0 | 62.2 | 13 | 26 | 5 | 0.026 |

Total training reward: -201.6348; weight-change L2: 0.025995.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:639e4e21be5df1a69d9fcf5c5ccd82d7203382a6291c851e7470e3b5975533df -->
### 2026-10-26 18:06:48 UTC — checkpoint v22

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 82.3 | 18 | 26 | 5 | 0.021 |
| 2 | wandering_off_limits | 0/4 | 0 | 118.4 | 27 | 26 | 12 | 0.072 |

Total training reward: -201.7119; weight-change L2: 0.025955.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:50c7ef63b87de75b5265286740d70a02983c2ac590108a244e7202ee128ef103 -->
### 2026-10-26 18:09:48 UTC — checkpoint v23

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 86.2 | 19 | 26 | 9 | 0.052 |
| 2 | wandering_off_limits | 0/4 | 0 | 166.5 | 39 | 26 | 16 | 0.068 |

Total training reward: -202.2031; weight-change L2: 0.025637.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:d077c88249aa88b6304f745c5c4270c28b47cea247bfc25d70e7e63b1dffac2b -->
### 2026-10-26 18:13:33 UTC — checkpoint v24

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 78.2 | 17 | 26 | 6 | 0.022 |
| 2 | wandering_off_limits | 0/4 | 0 | 210.7 | 50 | 26 | 14 | 0.058 |

Total training reward: -202.6051; weight-change L2: 0.051837.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:103c6a02b1637869fdad357870eb1046ef95fa5899c3a12c5f03b5d7d0c8df83 -->
### 2026-10-26 18:14:49 UTC — checkpoint v25

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 62.2 | 13 | 26 | 4 | 0.025 |
| 2 | wandering_off_limits | 0/4 | 0 | 58.2 | 12 | 26 | 4 | 0.023 |

Total training reward: -200.9522; weight-change L2: 0.025476.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:db8b78617806371edffb9a1e919844fcd50de3c06ad2d45a17ec2db18c975c85 -->
### 2026-10-26 18:17:17 UTC — checkpoint v26

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 134.5 | 31 | 26 | 9 | 0.035 |
| 2 | wandering_off_limits | 0/4 | 0 | 134.4 | 31 | 26 | 12 | 0.054 |

Total training reward: -202.3968; weight-change L2: 0.024776.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:7262afbfff091f32b7766344fdee191a860267ba43599b0e1ee0ba87d9d9aba8 -->
### 2026-10-26 18:18:06 UTC — checkpoint v27

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 22.1 | 3 | 26 | 2 | 0.004 |
| 2 | wandering_off_limits | 0/4 | 0 | 34.1 | 6 | 26 | 4 | 0.024 |

Total training reward: -200.3303; weight-change L2: 0.023521.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:f27bcdc977ae22df8388c3c98892e2c32d98db351c1e94101fc1931aeaf3f388 -->
### 2026-10-26 18:34:43 UTC — checkpoint v28

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 984.0 | 243 | 26 | 55 | 0.298 |
| 2 | wandering_off_limits | 0/4 | 0 | 586.2 | 144 | 26 | 33 | 0.215 |

Total training reward: -214.9859; weight-change L2: 0.148540.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:ee6b523eae1aaf245437aeffc345897c795d0b1e1280d6ed787ae3829508bf03 -->
### 2026-10-26 18:36:32 UTC — checkpoint v29

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 78.3 | 17 | 26 | 6 | 0.021 |
| 2 | wandering_off_limits | 0/4 | 0 | 94.3 | 21 | 26 | 7 | 0.022 |

Total training reward: -201.4792; weight-change L2: 0.016505.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:5276c40016cbf02b7ee8032c98c87860c5febd4b9adf7a8ac43eff812e969bcc -->
### 2026-10-26 18:43:25 UTC — checkpoint v30

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 399.4 | 97 | 26 | 32 | 0.208 |
| 2 | wandering_off_limits | 0/4 | 0 | 130.4 | 30 | 26 | 9 | 0.037 |

Total training reward: -204.8474; weight-change L2: 0.035794.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:bff53e2767b61eb429d299ebff406b3a2aa3b8361b4f2c0d70c5546111fac4b8 -->
### 2026-10-26 18:46:53 UTC — checkpoint v31

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 74.2 | 16 | 26 | 5 | 0.014 |
| 2 | wandering_off_limits | 0/4 | 0 | 194.5 | 46 | 26 | 17 | 0.079 |

Total training reward: -202.3893; weight-change L2: 0.018568.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:3665b520916c171825b2ce9c853f18de7f778c8b2687c8d43f8da68c13d38224 -->
### 2026-10-26 18:48:22 UTC — checkpoint v32

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 70.2 | 15 | 26 | 5 | 0.015 |
| 2 | wandering_off_limits | 0/4 | 0 | 74.2 | 16 | 26 | 5 | 0.017 |

Total training reward: -201.2102; weight-change L2: 0.018890.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:96a05763040b3bcb553ad505c51a12533e18276c83a9c7b7370eb7a890b71e57 -->
### 2026-10-26 18:56:35 UTC — checkpoint v33

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 480.2 | 117 | 26 | 29 | 0.169 |
| 2 | wandering_off_limits | 0/4 | 0 | 247.1 | 59 | 26 | 18 | 0.091 |

Total training reward: -206.8086; weight-change L2: 0.056897.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:b03d254e668aeca8c330fffb92a14fa7d4d159cc9d6832b363eb3b202e9917ef -->
### 2026-10-26 19:13:23 UTC — checkpoint v34

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 471.2 | 115 | 26 | 44 | 0.295 |
| 2 | wandering_off_limits | 0/4 | 0 | 170.4 | 40 | 26 | 18 | 0.078 |
| 3 | wandering_off_limits | 0/4 | 0 | 82.2 | 18 | 26 | 9 | 0.033 |
| 4 | wandering_off_limits | 0/4 | 0 | 54.1 | 11 | 26 | 4 | 0.017 |

Total training reward: -406.9471; weight-change L2: 0.055430.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:bce682b14f54c2d64de37d8eca81d2d4cb23533cd1dd98441c129b266880bfd5 -->
### 2026-10-26 19:21:30 UTC — checkpoint v35

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 320.9 | 78 | 26 | 18 | 0.079 |
| 2 | wandering_off_limits | 0/4 | 0 | 94.2 | 21 | 26 | 5 | 0.030 |
| 3 | wandering_off_limits | 0/4 | 0 | 471.4 | 115 | 26 | 27 | 0.121 |
| 4 | wandering_off_limits | 0/4 | 0 | 294.9 | 71 | 26 | 26 | 0.151 |

Total training reward: -411.0252; weight-change L2: 0.092043.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:9a5dbd4304aa9ced916584eca651cbbae6658842c217c9b478e888e385627384 -->
### 2026-10-26 19:43:37 UTC — checkpoint v36

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 1312.2 | 324 | 26 | 62 | 0.563 |
| 2 | wandering_off_limits | 0/4 | 0 | 136.5 | 32 | 26 | 14 | 0.059 |
| 3 | wandering_off_limits | 0/4 | 0 | 753.4 | 185 | 26 | 42 | 0.167 |
| 4 | wandering_off_limits | 0/4 | 0 | 70.5 | 15 | 26 | 6 | 0.030 |

Total training reward: -421.4970; weight-change L2: 0.149055.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:02f1072874adbdf67e1d8d542f6c98835f4f2f17f1a0772e04e2562cff6f7c44 -->
### 2026-10-26 19:52:12 UTC — checkpoint v37

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 215.2 | 51 | 26 | 15 | 0.082 |
| 2 | wandering_off_limits | 0/4 | 0 | 239.1 | 57 | 26 | 21 | 0.108 |
| 3 | wandering_off_limits | 0/4 | 0 | 500.1 | 122 | 26 | 24 | 0.129 |
| 4 | wandering_off_limits | 0/4 | 0 | 82.4 | 18 | 26 | 6 | 0.026 |

Total training reward: -409.6094; weight-change L2: 0.062291.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:f42b712fb4dab4177bbb114c738ea0dad0feeae7c491ddd055c276e1b9220494 -->
### 2026-10-26 19:54:17 UTC — checkpoint v38

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 60.2 | 13 | 26 | 5 | 0.015 |
| 2 | wandering_off_limits | 0/4 | 0 | 110.4 | 25 | 26 | 10 | 0.028 |
| 3 | wandering_off_limits | 0/4 | 0 | 106.4 | 24 | 26 | 8 | 0.029 |
| 4 | wandering_off_limits | 0/4 | 0 | 26.1 | 4 | 26 | 3 | 0.006 |

Total training reward: -402.5363; weight-change L2: 0.030866.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:f79caabfc340bb94f85668b5244d0d01023b517bc04d063778e615951404bf7c -->
### 2026-10-26 20:00:39 UTC — checkpoint v39

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 367.3 | 89 | 26 | 27 | 0.095 |
| 2 | wandering_off_limits | 0/4 | 0 | 46.1 | 9 | 26 | 6 | 0.022 |
| 3 | wandering_off_limits | 0/4 | 0 | 321.3 | 78 | 26 | 21 | 0.064 |
| 4 | wandering_off_limits | 0/4 | 0 | 68.2 | 15 | 26 | 4 | 0.012 |

Total training reward: -407.4261; weight-change L2: 0.046594.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

## October 26 — separate green human-facing WAI events

The user requested green console messages for significant WAI events while keeping structured status/training JSON visually separate. Human-facing episode logs now use a dedicated event category; structured WAI_EPISODE messages retain their original category. The build publisher prepares a bright-green console appender and preserves inherited file appenders, snapshots the pre-change config, and avoids repeated duplicate declarations. No ANSI escapes are added to saved JSONL or file log text. Nine offline publisher tests passed after running outside the sandbox's hard-link restriction; tests operated only on temporary workspace fixtures. Changed C++ syntax, scoped style and formatting passed. No full server build, live config edit, server restart or live color check was performed. The next normal build/publication and restart activates the routing.

## October 26 — investigated apparent cancellation of parallel peers

The user asked whether one party's failure cancels all parties. The latest recorded four-party batch instead shows independent wandering_off_limits outcomes for party four at about 130 seconds, party two at 143 seconds and party three at 348 seconds. Party one's latest inspected record is a member snapshot around 664 seconds, with no terminal event in that journal at inspection. Finished parties are parked while the synchronous batch waits for all results. A prior completed batch recorded four independently timed off-limits outcomes and a successful shared checkpoint update (version 39). These observations confirm independent gameplay termination and batch waiting, not successful dungeon completion or validated geometric accuracy.

Source still has a separate global-stop path: collector exceptions such as transport failures, setup/early-terminal collection failures, or failed shared training stop the entire pool. Ordinary fully recorded wipe/timeout/off-limits gameplay outcomes do not use that exception path. No scheduling or gameplay behavior was changed during this diagnosis.

<!-- wai-training:8d610dfaf222f43a5107cfaaf5ff8cff09a7a0b186434567197e5419549e0794 -->
### 2026-10-26 20:50:23 UTC — checkpoint v40

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 798.2 | 196 | 26 | 56 | 0.520 |
| 2 | wandering_off_limits | 0/4 | 0 | 142.5 | 33 | 26 | 10 | 0.025 |
| 3 | wandering_off_limits | 0/4 | 0 | 347.5 | 84 | 26 | 24 | 0.126 |
| 4 | wandering_off_limits | 0/4 | 0 | 130.4 | 30 | 26 | 12 | 0.047 |

Total training reward: -413.0615; weight-change L2: 0.087881.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:838038de890e746d7dd832e2473f51ea5060f38a193c7ea6fceaeda413f5a01e -->
### 2026-10-26 21:02:48 UTC — checkpoint v41

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 34.2 | 6 | 26 | 5 | 0.023 |
| 2 | wandering_off_limits | 0/4 | 0 | 729.4 | 179 | 26 | 37 | 0.195 |
| 3 | wandering_off_limits | 0/4 | 0 | 66.4 | 14 | 26 | 3 | 0.011 |
| 4 | wandering_off_limits | 0/4 | 0 | 62.4 | 13 | 26 | 5 | 0.011 |

Total training reward: -408.2743; weight-change L2: 0.057782.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:e14bed4da97629de66cfa38abb654ff334ea80a59f7fb9ba861a8c7a7ccc58d4 -->
### 2026-10-26 21:09:18 UTC — checkpoint v42

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 26.1 | 4 | 26 | 2 | 0.003 |
| 2 | wandering_off_limits | 0/4 | 0 | 66.2 | 14 | 26 | 5 | 0.009 |
| 3 | wandering_off_limits | 0/4 | 0 | 118.5 | 27 | 26 | 11 | 0.032 |
| 4 | wandering_off_limits | 0/4 | 0 | 375.4 | 91 | 26 | 20 | 0.064 |

Total training reward: -405.3387; weight-change L2: 0.042924.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:17b3c3692ac69d660804718d7c1a39ec5fe769f5b73d30999b8edc17d20a0e1f -->
### 2026-10-26 21:12:43 UTC — checkpoint v43

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 82.2 | 18 | 26 | 6 | 0.020 |
| 2 | wandering_off_limits | 0/4 | 0 | 174.6 | 41 | 26 | 11 | 0.030 |
| 3 | wandering_off_limits | 0/4 | 0 | 190.8 | 45 | 26 | 18 | 0.197 |
| 4 | wandering_off_limits | 0/4 | 0 | 116.5 | 27 | 26 | 14 | 0.060 |

Total training reward: -404.9213; weight-change L2: 0.042053.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:12cbe33832fe9c651b3c23fd978d45991c33dfc7879925d42792207a159a2d12 -->
### 2026-10-26 21:27:21 UTC — checkpoint v44

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 84.5 | 19 | 26 | 7 | 0.020 |
| 2 | wandering_off_limits | 0/4 | 0 | 460.4 | 112 | 26 | 28 | 0.095 |
| 3 | wandering_off_limits | 0/4 | 0 | 94.5 | 21 | 26 | 8 | 0.031 |
| 4 | wandering_off_limits | 0/4 | 1 | 863.1 | 210 | 26 | 52 | 0.403 |

Total training reward: -419.0612; weight-change L2: 0.083702.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:dd2370b41478007698a804afedd2dcfefb2e1b6e03ac00182e4593a5d042bda7 -->
### 2026-10-26 21:32:46 UTC — checkpoint v45

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 311.5 | 75 | 26 | 20 | 0.074 |
| 2 | wandering_off_limits | 0/4 | 0 | 70.3 | 15 | 26 | 4 | 0.008 |
| 3 | wandering_off_limits | 0/4 | 0 | 231.0 | 55 | 26 | 14 | 0.041 |
| 4 | wandering_off_limits | 0/4 | 0 | 98.5 | 22 | 26 | 7 | 0.017 |

Total training reward: -406.5671; weight-change L2: 0.041411.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:7da6000f0c41d2971d8d6c67156f3ac24ba01798fed1143b9a693c38f95c0c59 -->
### 2026-10-26 21:37:11 UTC — checkpoint v46

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 251.2 | 60 | 26 | 17 | 0.054 |
| 2 | wandering_off_limits | 0/4 | 0 | 70.4 | 15 | 26 | 4 | 0.024 |
| 3 | wandering_off_limits | 0/4 | 0 | 191.1 | 45 | 26 | 14 | 0.059 |
| 4 | wandering_off_limits | 0/4 | 0 | 148.8 | 35 | 26 | 9 | 0.033 |

Total training reward: -406.0374; weight-change L2: 0.041247.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:b9cc2bf623a43c2da1af01a7398b3f9267246bf998f8e55cd24d323e54331378 -->
### 2026-10-26 21:40:21 UTC — checkpoint v47

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 112.5 | 26 | 26 | 8 | 0.019 |
| 2 | wandering_off_limits | 0/4 | 0 | 174.8 | 41 | 26 | 11 | 0.035 |
| 3 | wandering_off_limits | 0/4 | 0 | 70.3 | 15 | 26 | 6 | 0.016 |
| 4 | wandering_off_limits | 0/4 | 0 | 62.3 | 13 | 26 | 4 | 0.015 |

Total training reward: -403.7007; weight-change L2: 0.027098.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:a388f556167d9af2e038b1bb78012c2c87ea3e0d1a700f4abd177fd91cabdb77 -->
### 2026-10-26 21:43:37 UTC — checkpoint v48

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 182.9 | 43 | 26 | 17 | 0.066 |
| 2 | wandering_off_limits | 0/4 | 0 | 54.2 | 11 | 26 | 3 | 0.006 |
| 3 | wandering_off_limits | 0/4 | 0 | 98.4 | 22 | 26 | 8 | 0.025 |
| 4 | wandering_off_limits | 0/4 | 0 | 86.4 | 19 | 26 | 6 | 0.031 |

Total training reward: -403.6842; weight-change L2: 0.026969.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:077e98cc268632d1fcd31ab4f328ca5f2811e4795db951628fe22a57279da383 -->
### 2026-10-26 22:09:02 UTC — checkpoint v49

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 80.4 | 18 | 26 | 9 | 0.043 |
| 2 | wandering_off_limits | 0/4 | 0 | 22.1 | 3 | 26 | 3 | 0.005 |
| 3 | wandering_off_limits | 0/4 | 0 | 142.6 | 33 | 26 | 8 | 0.025 |
| 4 | wandering_off_limits | 0/4 | 0 | 1509.8 | 373 | 26 | 79 | 0.328 |

Total training reward: -416.7400; weight-change L2: 0.090761.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:0d649b3119ee8c2e4f2353b3b0ecff7d12081b65a762a7123da95a4e323a8d6e -->
### 2026-10-26 22:16:29 UTC — checkpoint v50

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 432.2 | 105 | 26 | 27 | 0.073 |
| 2 | wandering_off_limits | 0/4 | 0 | 84.3 | 19 | 26 | 6 | 0.013 |
| 3 | wandering_off_limits | 0/4 | 0 | 62.2 | 13 | 26 | 3 | 0.004 |
| 4 | wandering_off_limits | 0/4 | 0 | 114.6 | 26 | 26 | 6 | 0.010 |

Total training reward: -406.4266; weight-change L2: 0.039244.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:29735504c044216c322f9d836d5822c6cd86d43335298da1164e08eee2c9abfb -->
### 2026-10-26 22:25:04 UTC — checkpoint v51

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 162.9 | 38 | 26 | 13 | 0.034 |
| 2 | wandering_off_limits | 0/4 | 0 | 500.4 | 122 | 26 | 26 | 0.072 |
| 3 | wandering_off_limits | 0/4 | 0 | 58.2 | 12 | 26 | 5 | 0.019 |
| 4 | wandering_off_limits | 0/4 | 0 | 66.3 | 14 | 26 | 4 | 0.015 |

Total training reward: -407.3310; weight-change L2: 0.039468.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:fdbfc12573a37fadea6d561a1e8eb3b9b71b829eadb42dacf11f9e294be1c7ec -->
### 2026-10-26 22:32:51 UTC — checkpoint v52

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 307.7 | 74 | 26 | 22 | 0.049 |
| 2 | wandering_off_limits | 0/4 | 0 | 58.2 | 12 | 26 | 5 | 0.014 |
| 3 | wandering_off_limits | 0/4 | 0 | 90.6 | 20 | 26 | 9 | 0.025 |
| 4 | wandering_off_limits | 0/4 | 0 | 452.3 | 110 | 26 | 24 | 0.087 |

Total training reward: -408.5074; weight-change L2: 0.053519.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:f73f78cbec5e63339753fdef9fd3c92083b98134607281dd234c35ed360b1027 -->
### 2026-10-26 22:50:51 UTC — checkpoint v53

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 34.2 | 6 | 26 | 5 | 0.015 |
| 2 | wandering_off_limits | 0/4 | 0 | 38.1 | 7 | 26 | 3 | 0.006 |
| 3 | wandering_off_limits | 0/4 | 0 | 576.8 | 141 | 26 | 47 | 0.255 |
| 4 | wandering_off_limits | 0/4 | 0 | 1064.2 | 262 | 26 | 46 | 0.148 |

Total training reward: -416.3050; weight-change L2: 0.087217.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:e90e750add889e6377c70fa2464e166f7b2986591f84c39e4f1fc9cd366709c4 -->
### 2026-10-26 22:54:32 UTC — checkpoint v54

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 207.0 | 49 | 26 | 11 | 0.020 |
| 2 | wandering_off_limits | 0/4 | 0 | 130.7 | 30 | 26 | 11 | 0.017 |
| 3 | wandering_off_limits | 0/4 | 0 | 22.1 | 3 | 26 | 2 | 0.002 |
| 4 | wandering_off_limits | 0/4 | 0 | 150.7 | 35 | 26 | 12 | 0.029 |

Total training reward: -404.6295; weight-change L2: 0.037479.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:5b7c456db32fe46ee6a1448a20680bf67b8b446c70d1be457bb6c89adb40a186 -->
### 2026-10-26 22:57:37 UTC — checkpoint v55

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 106.6 | 24 | 26 | 8 | 0.019 |
| 2 | wandering_off_limits | 0/4 | 0 | 74.4 | 16 | 26 | 7 | 0.017 |
| 3 | wandering_off_limits | 0/4 | 0 | 170.9 | 40 | 26 | 11 | 0.036 |
| 4 | wandering_off_limits | 0/4 | 0 | 126.7 | 29 | 26 | 8 | 0.020 |

Total training reward: -404.2825; weight-change L2: 0.026618.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:1a585bd3414e34390f6596ce6ca009337024cbb00984f9edbce7895ef3ac6beb -->
### 2026-10-26 23:02:19 UTC — checkpoint v56

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 100.5 | 23 | 26 | 8 | 0.018 |
| 2 | wandering_off_limits | 0/4 | 0 | 62.3 | 13 | 26 | 5 | 0.016 |
| 3 | wandering_off_limits | 0/4 | 0 | 178.9 | 42 | 26 | 16 | 0.040 |
| 4 | wandering_off_limits | 0/4 | 0 | 267.4 | 64 | 26 | 18 | 0.052 |

Total training reward: -405.5556; weight-change L2: 0.037012.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:5e9046aee74e74ae2e942fcd349d2200a74e0346cde954e2c0d55ae12725ac95 -->
### 2026-10-26 23:07:46 UTC — checkpoint v57

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 311.5 | 75 | 26 | 29 | 0.095 |
| 2 | wandering_off_limits | 0/4 | 0 | 62.2 | 13 | 26 | 5 | 0.011 |
| 3 | wandering_off_limits | 0/4 | 0 | 307.6 | 74 | 26 | 21 | 0.095 |
| 4 | wandering_off_limits | 0/4 | 0 | 142.8 | 33 | 26 | 9 | 0.025 |

Total training reward: -407.6036; weight-change L2: 0.049867.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:5f6dc25eadb75c11265757dc1f7cab751f5b6d528c2513f3daf2d371cffb1a3a -->
### 2026-10-26 23:18:11 UTC — checkpoint v58

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 74.4 | 16 | 26 | 5 | 0.010 |
| 2 | wandering_off_limits | 0/4 | 0 | 611.0 | 150 | 26 | 38 | 0.128 |
| 3 | wandering_off_limits | 0/4 | 0 | 26.1 | 4 | 26 | 3 | 0.005 |
| 4 | wandering_off_limits | 0/4 | 0 | 219.0 | 52 | 26 | 13 | 0.029 |

Total training reward: -408.7281; weight-change L2: 0.049067.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:f1af88826837c03ac9f77ded8134d7282b4a0882de026aefeb4ddb6075b28392 -->
### 2026-10-26 23:31:16 UTC — checkpoint v59

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 70.3 | 15 | 26 | 7 | 0.018 |
| 2 | wandering_off_limits | 0/4 | 0 | 30.1 | 5 | 26 | 5 | 0.015 |
| 3 | wandering_off_limits | 0/4 | 0 | 54.2 | 11 | 26 | 5 | 0.010 |
| 4 | wandering_off_limits | 0/4 | 0 | 769.9 | 189 | 26 | 31 | 0.108 |

Total training reward: -408.6891; weight-change L2: 0.048462.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:25cd58f266e1c7da34c392a46dd539a290977680b3dc7905d259c5ffdf345af4 -->
### 2026-10-26 23:35:50 UTC — checkpoint v60

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 62.3 | 13 | 26 | 6 | 0.018 |
| 2 | wandering_off_limits | 0/4 | 0 | 58.3 | 12 | 26 | 5 | 0.015 |
| 3 | wandering_off_limits | 0/4 | 0 | 259.4 | 62 | 26 | 8 | 0.017 |
| 4 | wandering_off_limits | 0/4 | 0 | 64.3 | 14 | 26 | 7 | 0.018 |

Total training reward: -403.9715; weight-change L2: 0.024183.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:ea0b060fd3b0cc1e092f84a575c7c94fca7f4a9acd5f08339b1d04f8931382dd -->
### 2026-10-26 23:38:47 UTC — checkpoint v61

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 88.4 | 20 | 26 | 7 | 0.018 |
| 2 | wandering_off_limits | 0/4 | 0 | 66.3 | 14 | 26 | 4 | 0.009 |
| 3 | wandering_off_limits | 0/4 | 0 | 162.6 | 38 | 26 | 11 | 0.036 |
| 4 | wandering_off_limits | 0/4 | 0 | 146.6 | 34 | 26 | 8 | 0.020 |

Total training reward: -404.1524; weight-change L2: 0.024106.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:4c0f2bb65179887e4975eb178056c4b434d628b7b7afb5737c40c8269126e683 -->
### 2026-10-26 23:53:48 UTC — checkpoint v62

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 54.2 | 11 | 26 | 4 | 0.011 |
| 2 | wandering_off_limits | 0/4 | 0 | 32.1 | 6 | 26 | 6 | 0.024 |
| 3 | wandering_off_limits | 0/4 | 0 | 886.6 | 218 | 26 | 38 | 0.112 |
| 4 | wandering_off_limits | 0/4 | 0 | 160.8 | 38 | 26 | 13 | 0.042 |

Total training reward: -410.7351; weight-change L2: 0.059121.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:19708b33bcbd4423bc8d52f6e85124e1edb18e0aa963235950ff813b0c807f4f -->
### 2026-10-26 23:59:22 UTC — checkpoint v63

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 22.1 | 3 | 26 | 2 | 0.002 |
| 2 | wandering_off_limits | 0/4 | 0 | 74.3 | 16 | 26 | 6 | 0.016 |
| 3 | wandering_off_limits | 0/4 | 0 | 58.3 | 12 | 26 | 7 | 0.025 |
| 4 | wandering_off_limits | 0/4 | 0 | 319.6 | 77 | 26 | 18 | 0.046 |

Total training reward: -404.2490; weight-change L2: 0.025578.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:6b22f9249a02d64a82e7e9865e6138aa40b63897fa42da8fb2faa9e11e4fc467 -->
### 2026-10-27 00:07:49 UTC — checkpoint v64

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 106.5 | 24 | 26 | 10 | 0.034 |
| 2 | wandering_off_limits | 0/4 | 0 | 126.6 | 29 | 26 | 9 | 0.021 |
| 3 | wandering_off_limits | 0/4 | 0 | 492.5 | 120 | 26 | 24 | 0.053 |
| 4 | wandering_off_limits | 0/4 | 0 | 118.5 | 27 | 26 | 8 | 0.020 |

Total training reward: -407.9088; weight-change L2: 0.048145.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:aabc58e6c31c5b3f5676c39e42719403cd03be69b4ea24d1988f503a189c2c46 -->
### 2026-10-27 00:12:45 UTC — checkpoint v65

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 122.6 | 28 | 26 | 10 | 0.028 |
| 2 | wandering_off_limits | 0/4 | 0 | 281.3 | 68 | 26 | 20 | 0.068 |
| 3 | wandering_off_limits | 0/4 | 0 | 34.1 | 6 | 26 | 5 | 0.012 |
| 4 | wandering_off_limits | 0/4 | 0 | 78.3 | 17 | 26 | 5 | 0.007 |

Total training reward: -404.6406; weight-change L2: 0.023902.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

<!-- wai-training:bb405d0b070d1bca0c3ed4333df471e09d0f1bfc7da1679f90fe0e12856cb447 -->
### 2026-10-27 00:27:27 UTC — checkpoint v66

| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | wandering_off_limits | 0/4 | 0 | 868.4 | 214 | 26 | 32 | 0.085 |
| 2 | wandering_off_limits | 0/4 | 0 | 142.7 | 33 | 26 | 15 | 0.054 |
| 3 | wandering_off_limits | 0/4 | 0 | 263.3 | 63 | 26 | 15 | 0.037 |
| 4 | wandering_off_limits | 0/4 | 0 | 247.1 | 59 | 26 | 12 | 0.033 |

Total training reward: -414.6030; weight-change L2: 0.070895.
Checkpoint saved and training completed. Weight changes alone do not establish improved play.

### 2026-10-26 � One-command observer clients: implementation and first-use setup

- User requested four tiled observer clients, one per concurrent party, using separate level-80
  undead mage characters. Existing accounts were created by the user; this change does not change
  their passwords. Character creation is deferred until the rebuilt server receives `waiepisode watch`.
- Added queued world-thread setup and the local Windows launcher. Existing `watch N` instructions
  and trainer observations remain unchanged. Observers stay outside the training parties and
  enter the current tank's exact instance, with their own RFC binding replaced as needed.
- Login uses a first-use encrypted password dialog and explicit field calibration, then reuses that
  setup. Per-character focus checks and bounded login/teleport waits stop failed launches.
- Publisher fixture tests, source syntax/style and PowerShell input-layout checks pass. No server
  build, live character creation, client launch or end-to-end observation was run by the agent.
- Lesson: saved installation paths should be checked before asking the user to repeat them;
  source implementation and live creation must be described separately.
- Next: user rebuild/restart and first-use UI verification. Off-spec best fallback remains the next
  feature, including explicit off-spec upgrade requests and existing trade initiation behavior.

### 2026-10-26 — Upgrade requests include mailbox collection and other receipts

- User observed that taking equipment from mail did not notify bots and required manual best queries.
- Added existing native receipt hooks for mail, inventory transfers, new-item storage, quest rewards
  and vendor equip. Loot, rolls and crafting now feed the same deferred coalescing path.
- Final inventory ownership is rechecked; overlapping hooks produce one evaluation. Ordinary bag/bank
  rearrangements and buybacks stay excluded. Bound-item restrictions and normal trade safeguards remain.
- Crafted bot upgrades can now initiate trade under the same safeguards as other sources. Non-loot
  player alerts use received wording and ship translations for all nine supported locales.
- Validation: changed C++ syntax checks and full module style checks pass. The standalone syntax
  harness needed the configured MySQL include path for the registration source; no source workaround
  was needed. Upstream fetch failed in the local SSH runtime; synchronization remains unverified.
- No full build, database script execution, server restart or live acquisition test performed.
  Off-spec best is still a separate pending feature.

### 2026-10-26 — Epic-only loot preparation and mailbox-capacity investigation

- User found all-quality guaranteed boss loot excessive and requested guaranteed epics only, with
  other qualities restored to normal. Prepared a reversible database transition from the saved
  original audit, applied-patch manifest and read-only item-quality data. No database writes made.
- Mixed groups require retaining the ordinary roll before adding epic bonuses; simply changing
  quality-row chances would alter rare odds. Generator preserves normal non-epic rows/groups/counts.
  It assumes reference/group amount multipliers of one, matching the workspace configuration.
- Twelve offline tests pass. SQL, both undo policies, private manifest and operator prerequisites
  are saved separately from the original patch. A live transactional rehearsal is still pending.
- Shared world-drop epics remain in scope: the largest epic pool has 174 types. Native non-equipment/
  quest duplicate rules can allow a normal epic copy plus its bonus; this is documented for review.
- Mail source inspection found a 50-message response batch and roughly 100-message player-send gate.
  Postmaster mail bypasses that gate; storage can already exceed the visible batch. Count caching
  and expiry are separate concerns. Uncapped storage/refresh remains a proposed source change,
  not implemented by this investigation. No mail changed and no build/server started.

### 2026-10-26 — Transmog upstream update pulled

- User requested an upstream check, then authorized pulling the installed transmog module forward.
- Fetched the configured official remote and fast-forwarded six commits, from f07686b to 327753a.
  The module checkout is clean and matches the fetched upstream master.
- Updates include inventory/bank appearance backfill, module_string migration, optional set-bonus
  disclaimer, GM diagnostics, bag appearance claiming and missing command messages.
- All three changed C++ implementation files pass syntax checks against the current core.
  The core updater discovers the module's db-world directory; workspace world config enables updates.
- No full build, database update or server start was performed. Normal rebuild/publication plus restart
  is still required; the actual runtime config must keep world database updates enabled.
- New appearance claiming binds claimed items and removes refundable state; the tradeable setting
  controls clearing BoP trade flags. Distribute desired gear before explicitly claiming appearances.
