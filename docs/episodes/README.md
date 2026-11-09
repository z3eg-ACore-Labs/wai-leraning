# Episode harness

Experimental Ragefire Chasm runs with one to four isolated cloned parties and one external shared
controller/trainer. Native PlayerBots supplies class combat; the model chooses local navigation. This
does not implement learned combat or establish that the party can clear the dungeon.

Install the single `mod-wai` module and its [native prerequisites](../../compatibility/README.md).
Use one `wai.conf`; do not install the former three WAI modules alongside it. Capture/action/episode
settings remain independent. No database update or helper starts merely from cloning/building.

## Setup

1. Prepare a dedicated empty primary account and five distinct offline same-faction template characters,
   levels 13–20. Templates are read-only inputs; runs use new character/item/pet identities. Never point
   the harness at a human or random-bot account.
2. Set `Wai.Episodes.Enable = 1`, the private `Account`, `Tank`, `Healer`, `Damage1..3` settings, and retain
   `Policy = "learned"`. Scripted policy is rejected. Secondary accounts default to the base name plus
   `_2`, `_3`, `_4`; private `Account2..4` overrides are supported. Requested missing secondary accounts
   can be created through the native API after an explicit administrator start.
3. Configure `Learner.Executable` to the learning environment's Python, `Learner.Script` to `rl_loop.py`,
   and private learner home/loopback SOAP/optional credential-file settings. Keep model homes separate
   from source/templates. No weights or Python runtime are bundled.
4. Preserve `StateDirectory` ownership journals across restarts. `OutputDirectory` stores private JSONL
   outcomes. Configure recovery/native summon support as needed; complete the requested integration
   build and controlled deployment before issuing run commands.

The harness preserves template equipment/build and sanitizes clone state/inventory; profession tools
are not supplied automatically. Native XP lock, grouping, login/logout, movement, combat and loot APIs
are reused. Cleanup is restricted to verified clone GUID/name ownership. Explicit legacy recovery adopts
only a fully validated offline clone roster. Account preparation, cloning and cleanup perform native
database writes when commanded; extraction tests have performed none of those operations.

## Commands

Use the server console, or prefix with `.` in game. `waiepisode` requires administrator access.

| Command | Effect |
| --- | --- |
| `waiepisode start [1..4]` | Prepare requested cloned parties and request the configured learner; defaults to one |
| `waiepisode recover [1..4]` | Explicit validated legacy-clone recovery before starting; review existing ownership/state first |
| `waiepisode status` | Report all selected party states |
| `waiepisode stop` | Stop the pool/controller and automatic repeat |
| `waiepisode cleanup` | Clean verified harness-owned clones; preserve originals and private ownership records |
| `waiepisode repeat` | Single-party repeat; for a pool select a party explicitly |
| `waiepisode party N status|repeat` | Address one active party |
| `waiepisode watch N` | Print manual GM observation instructions for that running party |
| `waiepisode watch` | Local console only: explicitly request the separately configured observer-client launcher |
| `waiepisode attach|detach` | Retained legacy existing-party interface; attach is rejected by the required learned policy |
| `waiobserver status N` | GM/helper check for the configured observer slot |
| `waiobserver join N` | Authorized configured observer joins the running tank's instance/phase in GM invisible mode |

Example: `waiepisode start 1`, inspect `waiepisode status`, then use `waiepisode stop` and intentional
`waiepisode cleanup`. Starting two to four parties requires strict `MovementIntegrity.Enable = 0` because
that optional audit tracks one party. The tolerant boundary monitor remains separate. The external client
freezes a model during collection; training/repeat behavior follows its explicit rollout lifecycle.

Helper protocol operations `waiepisode party N observe`, `act EPISODE SEQ ACTION MODEL_VERSION` and
`training ...` are for the matching external controller, not manual gameplay input. They validate episode,
sequence, action bounds, model version, lifecycle stage and heartbeat. A training receipt/changed weight
is not proof of improved navigation. See the external learning operation guides for schema/reward details.

## Optional observers

Configure private `Wai.Episodes.Watch.Account1..4` and `Character1..4` values. All default empty; no account
or character is inferred from an operator's setup. Use dedicated GM accounts, each empty/offline initially
or containing the exact sole observer. Explicit watch can create the configured level-80 undead mage;
existing conflicting characters/accounts are rejected. It does not create accounts or change passwords.

Use the matching `tools/episodes/Watch-Wai.ps1` and set `Watch.Executable`, `Watch.Script`, `Watch.Client`,
private login-file and SOAP settings. Account slot values are passed explicitly; old hardcoded launcher
versions are incompatible. Passwords are not process arguments. The helper's Windows DPAPI login and
calibration files are private. It may launch clients and generate login/menu input only when the user
explicitly runs watch. None of those actions were performed during this refactor.

Missing movement API leaves episode commands unavailable with a diagnostic. Missing safe process API
rejects starts before account preparation and refuses observer/learner launches. Recording and character
telemetry remain available independently. Detailed [movement](MOVEMENT-INTEGRITY.md),
[navigation](LEARNED-NAVIGATION.md), [recovery](RECOVERY-AND-EXISTING-PARTY.md) and
[watcher](WATCH-OBSERVERS.md) notes contain historical milestones; use the root installation instructions
and current private configuration for this consolidated module.
