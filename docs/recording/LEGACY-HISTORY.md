# Gameplay observer: recorder-only foundation

Status: dungeon-visit lifecycle and selectable navigation/full profiles are implemented and offline tested. Consult the current build receipt for compilation status; in-game profile behavior is unverified. Capture remains disabled by default. No controller, training loop, voice integration, addon installation or autonomous bot actions are included.

## Navigation and full recording profiles

`GameplayObserver.Profile = "navigation"` is the default, including when the
option is absent from an existing configuration. Capture must still be enabled
with the existing `GameplayObserver.Enable` setting. Navigation records map and
instance, the selected human's position/facing, alive/combat/casting booleans and
movement requests. Periodic snapshots continue while stationary or in combat so
offline processing can separate routes; this does not train combat decisions.
Detailed spell catalogs, cast events, non-movement requests, party combat state,
auras and targets are omitted. Each episode-start record identifies its profile.

Set `GameplayObserver.Profile = "full"` to retain the detailed legacy collection
described below for later research. Other values fall back to navigation. Existing
recordings are not converted or deleted. Profiles change collection only: native
PlayerBots combat, packet handling, movement and game decisions are untouched.
These records are route-preparation data, not trained pathfinding models, proof
of dungeon completion or automatic model playback. Training/control integration
remains separate work. A manual restart after building is needed to use new code.

## What the full profile records

The single logged-in human character's dungeon gameplay, with current same-map party context and selected-unit state. The module uses existing server hooks; core headers and existing PlayerBots source are unchanged.

- Periodic state, 200 ms by default: map/instance, character class/level, local actor identifiers, positions/facing, health/resources, combat/alive status, targets, current casts and up to 32 aura identifiers per unit.
- A fresh pre-handler snapshot for supported gameplay requests, captured through the read-only `CanPacketReceive` hook, which always returns true. Existing later packet hooks are not misrepresented as pre-action observations.
- Spell requests with concrete spell, cast counter, explicit unit target, optional ground location, current spell cooldown and heuristic multi-label action categories.
- Movement start/stop/turn/jump/facing, targeting, autoattack, item-use and selected interaction request categories. Movement opcode plus sampled positions are retained; this version does not decode a complete desired movement trajectory or item-use payload.
- Accepted spell execution stage, outgoing cast start/go and cast failures. A cast-go is release, not proof of damage/healing or successful completion. Aura-triggered execution is marked and not automatically correlated as a human request.
- A bounded active-spec spell catalog, refreshed every five seconds and on dungeon entry, including spell IDs, passive status and heuristic labels.
- Dungeon episode boundaries: entry/instance change, exit, logout and recorder stop. Multiple dungeon visits can be saved in one recording session and separated later using the episode field. Re-entering an instance after leaving starts a new episode; this does not claim a boss kill, completion or instance reset.

No keybinding map or keyboard simulation is involved. Only actions visible to the server can be recorded. A keypress suppressed entirely by the client is unavailable.

## Recording and later training

The output is immutable JSONL data, independent of any model. Retain multiple sessions for later offline processing and repeated training. Reserve complete runs/dungeons for evaluation rather than continually training on the evaluation set. Logs alone do not train a model or demonstrate prediction accuracy.

Enabling the recorder only arms capture; no recording file or directory is created at server startup or outside a dungeon. Entering a dungeon opens a new visit file. Leaving the dungeon, changing dungeon/instance or observed character, logout, disabling/reloading the recorder, or server shutdown queues its end marker, writer summary, flush and close. Re-entering the same dungeon starts a separate file even if the instance ID is unchanged.

Each visit gets an atomically allocated directory and a matching filename beneath the configured output directory, for example `wailing-caverns-20261023T180000Z-<unique>-0/wailing-caverns-20261023T180000Z-<unique>-0.jsonl`. The dungeon name is sanitized, the timestamp is UTC, and the suffix prevents overwriting previous visits. Sequence numbers and actor aliases restart per file, and each file contains its own initial observer selection and episode markers. Existing older `events.jsonl` recordings remain valid and unchanged.

The background writer flushes the first record, at approximately one-second intervals during a visit, and when the visit closes. Disk retention is manual and total recording volume is not capped.

Every regular record has schema, sequence, monotonic timestamp, episode and kind. Input records identify their state as pre-handler and source as a human request. Cast-event request references are candidate correlations by spell and client cast counter, with a bounded recent-request cache; zero means unmatched. Do not infer a human choice from an unmatched or triggered execution event. Rank substitution, queued casts, wrapped client counters and delayed effects need explicit handling in later dataset construction.

Queue memory is capped at 8 MiB. Serialization happens on the gameplay thread; a dedicated worker performs disk writes. Overflow consumes a sequence and increments the dropped counter. Each dungeon close drains its preceding records and appends that visit's writer summary. Up to 128 queued open/close boundaries are reserved separately from the bounded record bytes; boundary overflow disables capture rather than silently merging visits. Missing sequence numbers, nonzero drops or a missing summary must be treated as data-quality issues. This is not a lossless crash-recovery journal. Disk/open/flush errors disable recording and emit a server error; gameplay requests are still allowed.

Actor IDs are sequential local aliases. No account IDs, character names, raw chat, raw packet payloads, credentials or voice are written. The account ID is used internally to filter outgoing packets, never serialized. Auras and selected-unit properties are authoritative server data; a player-observable information mask is not yet implemented. These logs are not automatically suitable for training a perception-limited policy.

## Setup after an explicitly requested build

The folder belongs beneath the core's `modules` directory and uses the normal `Addmod_gameplay_observerScripts` loader. New module discovery requires CMake regeneration as part of a later authorized build. The module was discovered in the user-configured build and then compiled successfully during the guarded incremental repair build.

The standard module config installer discovers `conf/wai.conf.dist`. Copy it to the corresponding active `.conf` when deploying, then enable recording:

```ini
GameplayObserver.Enable = 1
GameplayObserver.IntervalMs = 200
GameplayObserver.OutputDirectory = "gameplay-observations"
```

Disabled is the shipped default. After deployment, normal configuration reload arms/stops capture; the next eligible dungeon entry opens a file. No character ID is required. On the world thread, the recorder checks for a unique in-world human every 250 ms, excluding bot sessions and, when PlayerBots is enabled, AI-controlled altbots. With no eligible human or multiple eligible humans, capture waits instead of picking one. Logout clears the selection immediately; changing characters is detected automatically without restarting the server. Observer changes close the prior dungeon episode so different demonstrations do not get mixed. There is no per-player chat command. The old `GameplayObserver.CharacterGuid` key is no longer consulted. Only dungeon maps are recorded; outside a dungeon, only relevant episode-close/logout events may be emitted.

The user controls all builds and server launches. Use the workspace's guarded build script when a build is explicitly requested. Do not start another build or server to test this module automatically.

## Offline validation

```text
python -m unittest discover -s tests -v
python tools/recording/validate_observation.py <recording-directory>/events.jsonl
```

The validator checks ordering, known private/raw field names, finite numbers, bounds, pre-handler provenance, candidate spell correlation and episode boundaries. It reports dropped/gapped records, unmatched casts and incomplete sessions. It deliberately reports `training_ready: false`; field validity does not prove gameplay correctness or imitation-learning suitability.

Validation completed: 17 synthetic validator tests passed; the core C++ codestyle checker passed on the module sources; clang-format dry-run passed; 49 referenced opcode/effect/hook constants were checked against local headers. Server API signatures and packet layouts were inspected locally. C++ compilation/linking subsequently passed with zero warnings/errors after correcting two level-accessor calls to GetLevel(). Packet-path integration, lifecycle/thread behavior, runtime costs, actual log contents and dungeon-entry/exit behavior remain unverified in game.

## Deliberately deferred

Nearby-enemy/world-object discovery beyond the selected unit; routes/navmesh/hazards; full target feasibility and global-cooldown masks; durations/stacks on all auras; complete effect/damage/healing and encounter outcome correlation; structured human bot commands; individual bot action lifecycle events; class-independent high-level intent annotation; automatic completion/wipe scoring; dataset exporter; offline trainer and shadow predictor.

Labels currently cover common direct/periodic damage/healing, interrupts, dispels, selected crowd-control aura types and support/summons. They are spell-metadata heuristics, not ground-truth human intent. Uncovered effects remain unknown. This first milestone provides a recorder foundation; the deferred fields should be added before claiming a complete general-dungeon training dataset.

## Source baseline

Core revision: `467b546fb4f0787ca193bdba335d9d5b67b2442f` (pre-existing totem changes retained).

PlayerBots revision: `eae1c26ab8a3a510ca7657997ae4e30df3e9bc88` on `adv-pb` (pre-existing voice/control edits retained; no PlayerBots files changed for this recorder).

This new standalone module is ignored by the parent core's module exclusion rules; the accompanying workspace ZIP preserves the source. No commit or push was made.

License: GNU GPL version 2 or later, consistent with AzerothCore.

Automatic-selection validation includes log cases for unique-human selection, character handoff, multiple-human pause and invalid selection. These are log-validator tests; live eligibility filtering and polling still require compilation and server testing. Selection only updates cached identity on the world thread; player callbacks never scan all players.

## Dungeon-file lifecycle validation

The standalone `client-patch-tools/observer-tests/test_dungeon_writer.cpp` fixture
exercises the actual file-writer header without starting a server. It checks no
startup filesystem creation, sanitized names, two independent WC visits and a
different dungeon, visible first-record writes, flush/close, writer summaries and
per-file dropped counters. All three generated files passed the existing JSONL
validator as independently complete episodes. The 17 existing validator tests
also pass. Full module compilation and live entry/exit hooks remain unverified.

## October 29 navigation handoff update

The earlier baseline and validation paragraphs describe historical milestones.
The module is now tracked in the core, and the navigation recording profile was
built successfully. New recording/model-control switching is implemented here:

- `.wainav run` requests a compatible local learned-navigation model for the
  caller's dungeon and difficulty; native PlayerBots supplies combat.
- `.wainav record` (or `stop`) releases automation and resumes a fresh human
  segment in the same dungeon. Manual movement/spells/item use also cancels it.
- `.wainav status` distinguishes waiting, model control, active recording and
  observer/selection/dungeon prerequisites.

The feature is available by default but never takes control until an explicit
run command. `GameplayNavigation.Enable=0` disables requests. Existing client-bot
permissions apply. All automated samples are excluded from human demonstrations;
explicit record/run selection takes priority over automatic human selection.

The local helper uses frozen neural policies, bounded generation/sequence leases
and game-thread navigation-mesh paths. It learns from closed human-only movement
segments in a separate worker; reinforcement learning and learned combat remain
deferred. Configuration reload, input takeover, death, instance change, logout,
stale actions and helper loss stop automation. A closed segment is not dungeon
completion evidence. No in-game verification or dungeon clear is claimed.

See the companion learning repository's `NAVIGATION-SWITCH.md` for setup, model
scope, quality checks and manual test steps. The requested guarded build of core
revision e32e08f57 succeeded with zero compiler warnings/errors and was published.
No server or character was started; runtime verification remains a manual step.
