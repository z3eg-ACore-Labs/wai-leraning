# Client navigation learning: controls and independent policies

## Current implementation status

For ordinary human dungeon runs, use [automatic demonstrations](AUTOMATIC-DEMONSTRATIONS.md):
the configured server launcher starts a passive helper, entry starts capture, and
authoritative supported dungeon completion triggers only behavior cloning and human-only hybrid
maintenance. No per-run F6 or client control lease is required. This new passive
telemetry and generic encounter-credit hook require a subsequent build and restart; live verification is pending.
The explicit model-control session described below remains separate.

The client session can record human movement or execute one of three independent
navigation policies. The managed trainer implements behavior cloning, proximal
policy optimization, and a hybrid objective. Offline checks cover separation,
provenance, truncation and publication. Live client performance remains unverified.
The server telemetry change requires rebuilding, publishing and restarting before
reward-based client training. No server is started automatically by the build.

Read [the implementation plan](HYBRID-LEARNING-IMPLEMENTATION.md) for the design
recorded before development. Chronological decisions remain in the experiment history.

## Controls

Start one session process for a specific already-open WoW client and character.
It starts paused. Focus the game with chat and menus closed:

| Key | Action |
|---|---|
| F6 | Human control: record movement demonstrations |
| F7 | Pause input and recording |
| F8 | Imitation learning, trained with behavior cloning, takes over navigation |
| F9 | Reinforcement learning, trained with proximal policy optimization, takes over navigation |
| F10 | Hybrid imitation and reinforcement learning takes over navigation |
| F12 | Stop, release generated movement keys, then maintain models if --learn was enabled |

Unbind these function keys from conflicting WoW actions. They are not swallowed by
the hook. Manual keyboard/mouse input and focus loss pause model control; select a
policy explicitly to resume. Missing or incompatible policies leave control paused,
never substitute another policy. Displays show full method names, version and hash.
Movement pulses are at most one second and have an independent input watchdog.
Stale telemetry, changed identity/instance or broken leases stop the session.

## What each policy learns

| Policy | Data it may train on | Data it must not train on |
|---|---|---|
| Imitation learning through behavior cloning | Human demonstration actions | Reward-based trajectories |
| Reinforcement learning through proximal policy optimization | Its own client trajectories | Demonstration labels or another policy's trajectories |
| Hybrid imitation and reinforcement learning | Human demonstrations and its own client trajectories | Standalone policies' on-policy trajectories |

The hybrid objective combines the proximal-policy-optimization loss with a weighted
behavior-cloning loss (default coefficient 0.1 when both are present). Human-only
updates are explicitly recorded as zero reinforcement decisions. Reward updates
reuse a bounded human demonstration buffer (4,096 samples). Each policy retains
its own weights, optimizer, random state and consumed-session hashes.

Keeping hybrid current means updating it after new demonstrations or its own new
reward experience. It does not copy the newest standalone weights, average models,
or claim that another policy's experience was on-policy. If you only run the pure
reinforcement policy and provide no demonstrations, hybrid correctly stays unchanged.

## Initialize a managed comparison

Run commands from your learning repository checkout, using its Python environment:

```powershell
.\.venv\Scripts\python.exe .\client_training.py initialize --home .\data\client-policies
```

This creates three separate untrained checkpoints from the same random initialization,
so starting conditions are disclosed and comparable. It refuses to reset an existing
registry. To reuse existing weights as an explicitly shared starting point, add
`--source <existing-navigation-checkpoint.pt>`; that file is read-only and preserved.
No training is performed during initialization. No production checkpoint is selected
automatically. Existing server-driven training continues to use its own home.

The managed layout is:

```text
client-policies/
  models.json                    current checkpoint for each policy
  il/v000000.pt, v000001.pt, ...  imitation-learning generations
  rl/v000000.pt, v000001.pt, ...  reinforcement-learning generations
  hybrid/v000000.pt, ...         hybrid generations
  latest-training-summary.json   most recent maintenance result
  client-history-pending/        retryable sanitized history receipts
```

Short configuration keys `il`, `rl`, and `hybrid` are stable technical identifiers;
operational displays use full scientific names. Historical `client_mixed` is the
hybrid checkpoint format identifier, not the name of a new algorithm.

## Start a client session

1. Rebuild/publish the server change, then restart normally when ready.
2. Enable `Wai.Action.Client.Enable = 1` in the existing wai-action configuration.
   `Wai.Action.Client.Map = 389` selects Ragefire Chasm. Apply the setting with the
   normal server reload/restart workflow. No database update is needed.
3. Log into the character in a fresh dungeon instance in WoW. Disable native SelfBot with `.bot client off`;
   native AI and this client executor must not drive the same character.
4. Verify movement bindings in `client-bindings.example.json`. Left/right mean
   STRAFE left/right (normally Q/E), not turning. Use the real client process ID
   and the character's full GUID from existing client telemetry/probe output.
5. Start the session below, then issue `.wai client start` from that character.
   Close chat, focus the game and select a mode with its function key.

```powershell
.\.venv\Scripts\python.exe .\client_session.py `
  --learning-home .\data\client-policies --learn `
  --pid <Wow-process-ID> --player-guid <character-GUID> `
  --bindings .\client-bindings.example.json `
  --credentials <private-SOAP-credentials-file> --seconds 900
```

Replace placeholders. Credentials use the existing private file or the environment
pair WAI_SOAP_USER / WAI_SOAP_PASSWORD, never a password on the command line.
The server grants one character-scoped observation lease. This lease never sends
server movement actions; normal inputs move the actual client. Nothing starts with
the server and no Python learning overhead exists when no session is running.

Omit `--learn` for evaluation: the session records observations but does not update
weights. `--track il|rl|hybrid` selects the initial loaded policy; execution still
starts paused. Existing `--models <mapping.json>` or `--checkpoint <file.pt>` options
remain available for frozen evaluation. For automatic maintenance use the managed
`--learning-home` registry, not a production PPO checkpoint directory.

## When updates happen

With `--learn`, a normal stop, the session time limit, character death or a full
clear finalizes the log, releases inputs, releases the lease, and runs maintenance.
New human samples update imitation and hybrid policies. Reward transitions update
only the policy that generated them. Combat defers navigation with wait actions;
manual interaction truncates the segment rather than mislabeling your actions.
Switching policies also truncates the segment with a value bootstrap.

Transport errors and other failed sessions do not automatically train. Recorded
valid data remains on disk for inspection; a partial/failed session cannot be fed
back to maintenance as a completed run. No success bonus is awarded for pressing
stop or reaching the client session time limit. Unobserved pending actions are dropped.

To maintain a finalized session manually:

```powershell
.\.venv\Scripts\python.exe .\client_training.py update `
  --home .\data\client-policies --session <completed-session.jsonl>
```

Each input session is consumed at most once per policy. Wrong checkpoint hashes,
incompatible action semantics or mismatched behavior probabilities reject the
update. New checkpoint files are immutable; models.json advances only after all
eligible updates validate and publish. Failed publication retains the prior registry
and removes only generations created by that failed attempt. Old successful
generations remain available for rollback. This session releases its keys before invoking the writer.

The terminal prints a short report with full method names, version, used sample
counts, losses and changed-parameter counts. A sanitized summary is appended and
committed locally to the experiment history. If history Git operations fail, a
pending receipt is retained without invalidating the saved checkpoints. No raw game
logs, credentials, account linkage or identifying machine paths enter public history.

## Rewards and limits

Read-only server telemetry now reports elapsed time, unique party boss kills,
party deaths and terminal outcome. Rewards match the experiment's priorities:
10 per boss, 1,000 for all four, minus 5 per death, minus 0.01 per second, and a
small capped exploration bonus. Controlled-character death ends the client session
with a failure penalty of 50. Boss identities are outcome labels, not path targets.
At present authoritative boss rewards cover Ragefire's four bosses only; training
refuses missing reward telemetry rather than inventing completion data elsewhere.

This implementation controls and imitates navigation only. It does not learn
combat, targeting, talent allocation or inventory use. A full dungeon clear is not
a validated capability. SelfBot is a separate native server controller and is not
used simultaneously on the controlled client. Other group bots may still be native.
Flying, swimming, vehicles and transport are unsupported. Pause before chat or menus:
the recorder cannot detect chat focus and must not interpret typed movement letters
as gameplay. Stationary waits, mouse turning, default turn keys, modifiers, jumps,
and ambiguous directions are excluded from human labels. Custom turn bindings are
not detected; pause for those. Managed training accepts stable human samples; the
standalone diagnostic fitter retains its minimum of 32 samples.

The original server PPO action contract is different from client strafe inputs.
Initializing from it is disclosed transfer, not proof of equivalent behavior.
Do not copy client artifacts over server navigation.pt or combine their rollouts.
Compare policies on separate runs with matching starting conditions and recorded
initialization. Lower imitation loss or changed weights alone is not evidence of
better navigation or dungeon completion.
