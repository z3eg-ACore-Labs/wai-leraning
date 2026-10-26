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
