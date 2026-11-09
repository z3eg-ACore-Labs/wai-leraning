# Controller protocols and limits

Use the matching `wai-learning` client; these are administrator console/SOAP operations. They do not
grant a lease themselves. Native core SOAP checks the account/password and administrator level; never
expose administrator SOAP credentials to untrusted users/models. Local console is an administrator path.
Actual transport requests, clients and model processes were not started for extraction validation.

| Operation | Purpose |
| --- | --- |
| `wai observe` | Fresh server-action observation: generation, sequence, features, allowed spells and native target/helper state |
| `wai act GENERATION SEQ wait` | Consume one fresh observation without acting |
| `wai act GENERATION SEQ cast SPELL_ID` | Request one of the narrow experiment's learned/allowed spells through normal casting |
| `wai act GENERATION SEQ attack` | Native attack/chase helper for an eligible selected candidate |
| `wai act GENERATION SEQ loot` | Attempt ordinary native owned-corpse loot in range |
| `wai act GENERATION SEQ approach` | Native approach to an eligible owned corpse |
| `wai act GENERATION SEQ move X Y Z` | Optional bounded route-replay step, at most eight yards |
| `wai client observe` | Observation-only client state/reward polling; rejects server actions |
| `wai client stop GENERATION` | Release only the matching client lease |
| `wai client discover` | Discover fresh addon session identities; does not choose or control a character |
| `wai client watch FULL_GUID OBSERVER_TOKEN` | Passive per-character human dungeon observation, separate from the action lease |

Generation/sequence must match the current unconsumed observation, observed within one second; controller
heartbeat expires after three seconds. Logout, eligibility/instance change, reload and lifecycle checks
release control. Ordinary clients cannot submit model actions through `.wai`. Server actions use native
spell/item/loot permissions, resources and cooldowns. Target choice, attack/chase and corpse helpers are
deterministic native helpers, not a learned targeting/navigation policy. Route replay is separately labelled.

Passive watch never acquires/renews the action lease or moves the character. It records exact
map/difficulty/instance/party identity and excludes conflicting native/model controllers. Encounter-credit
events require valid metadata/required masks and a real owned final update. Missing coverage stays
unknown; exit, death or timeout is not completion. The four owned Ragefire boss deaths are an additional
completion proof. Do not claim general completion coverage until scripts/data and live behavior are checked.

Telemetry supports later navigation learning; full combat imitation and a general learned combat contract
are not provided here. Preserve separate human/model provenance, independent policy stores and held-out
evaluation. Raw telemetry, client GUIDs/tokens and credential settings belong in private local storage.

`WaiExternalCommands.h` is optional static-link discovery API v1. Registration publishes only bot command
names after the generic PlayerBots integration is present. Missing/unsupported discovery excludes those
voice commands; it does not disable other voice/QoL commands or the independent character telemetry.
No separate mandatory adapter repository or voice-to-learner control channel is introduced.
