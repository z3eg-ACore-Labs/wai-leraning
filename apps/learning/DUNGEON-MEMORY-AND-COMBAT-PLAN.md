# Dungeon memory and learned navigation plus combat

## Current phase: navigation first (October 29, 2026)

The user narrowed the immediate scope to dungeon pathfinding/navigation learned
from the dungeons they run. Existing PlayerBots logic should continue to handle
combat; learned combat is deferred for a later design. Earlier joint-learning
requirements below remain long-term history, not the current implementation task.
Begin with reviewed offline route extraction from recorded position snapshots.
Do not interpret navigation segments as trained models or successful dungeon
completion, and do not assume the existing client-input lease already permits
native combat to coexist with a navigation controller. That control boundary must
be verified separately before live replay. Raw telemetry is preserved.

October 28, 2026. User-approved direction. Dungeon/difficulty store routing and
server encounter-mask completion are now implemented and offline tested; joint
combat and remembered-dungeon invitations remain design work, not live-verified capabilities. Automatic recording/startup work is
documented separately in `AUTOMATIC-DEMONSTRATIONS.md`.

## Requested behavior

Capture human demonstrations in every supported dungeon entered normally. Save
each run with its dungeon, difficulty, instance and policy-schema identity. A real
server completion event finalizes successful recordings and triggers imitation
learning through behavior cloning and human-only hybrid maintenance. Pure
reinforcement learning stays behind its existing explicit server commands.

On returning to a dungeon with usable experience, a party bot should offer a text
invitation such as “I've seen this place before. Shall we try a rerun from memory?”
The offer must disclose whether experience is recorded, trained but unverified, or
validated. A visit alone does not establish learned competence. There is no audio
requirement and no automatic takeover. The user explicitly chooses model play.

The user's final target is **learned navigation and learned combat**. Native bot
combat may remain a diagnostic baseline, not a substitute for that requested goal.

## Dungeon-specific policies

Use one implementation and training pipeline with independent per-dungeon,
difficulty and character-build policy stores. Each store keeps separate imitation,
reinforcement and hybrid tracks, optimizer/RNG state, immutable generations,
processed input hashes and evaluation evidence. Never mix one dungeon's absolute
coordinate demonstrations into another dungeon's current movement model.

The present client observation has 16 features, optionally extended to 26 through
visit memory. Map identity is outside that vector; the new recorder routes to separate per-dungeon/difficulty registries, while
build conditioning and the memory invitation remain unimplemented. A shared cross-dungeon model would first require
explicit dungeon/build conditioning, compatible action vocabularies and evaluation
of transfer/interference. Do not silently copy or overwrite the existing registry.
Initialize new stores only through an explicit setup operation.

## Actual server completion source

Source inspection found the existing `GlobalScript::OnAfterUpdateEncounterState`
hook, dispatched by `Map::UpdateEncounterState` after kill or spell encounter credit.
The hook supplies the map, credit type/entry, source unit, difficulty, encounter
definitions, a final-encounter dungeon ID and an `updated` flag. No new core-wide
header hook is needed. `InstanceScript::GetCompletedEncounterMask` supplies the
authoritative mask when an instance script supports it.

The final-encounter dungeon ID alone does **not** establish all encounters were
completed: the core sets it when a matching definition has `lastEncounterDungeon`.
The implementation now derives and validates required masks from the loaded
encounter definitions, record coverage per map/difficulty, and require the matching
instance's actual completion state. An absent definition or script is “completion
unavailable,” never successful exit. Optional encounters, branches, shared
difficulties, duplicate credits and spell-based finales need explicit fixtures.
Filter credit to the observed party and exact instance; reject stale/foreign
source units and controller conflicts. Preserve a stable per-run completion receipt
and deduplicate reentry/repeated polling. Do not manufacture generic completion
from arbitrary kills, elapsed time, exit, death or a guessed last boss.

The passive observer now uses that existing hook, validates required masks and
exact party/map/difficulty/instance scope, remembers an owned final credit, and
requires an actual completed-mask update. Non-raid dungeon capture is generic;
completion remains unavailable when the runtime metadata/script/credit path cannot
prove it. The existing four observed unique Ragefire bosses remain an additional
completion proof. Offline production-hook fixtures cover masks, missing metadata,
spell credits, duplicates, foreign sources and difficulty. Per-dungeon live coverage
is still unverified; no universal completion claim is made.

## Joint navigation and combat contract

Current `client_strafe_v1` has nine actions: wait and eight directional keyboard
combinations. Client features include health/resource/combat summaries, position,
orientation, time and boss progress. `ready` excludes combat/casting; human labels
currently exclude combat. Those checkpoints cannot learn combat by changing a flag.

The separate server-action observer exposes a selected target, six allowed druid
spells, distance, attack/cast/loot/busy state and legality checks. Its `cast`,
`attack`, `loot` and route actions are server/native helpers under an explicit
lease, not equivalent to client-generated semantic combat decisions. The older
offline spell-choice experiment predicts six spell requests at known decision
instants and explicitly does not learn timing or target selection. Neither is a
complete joint navigation-and-combat implementation.

The next contract needs pre-action observations for target/candidate identity and
relative geometry, hostile/friendly validity, health/resources, known spells and
action bindings, cooldown/global-cooldown readiness, cast/channel state, relevant
auras and line-of-sight/range legality. Human demonstrations need semantic spell
and target actions with timestamps and observed outcomes, not guesses from pressed
keys. Action-bar pages, modifiers, macros, mouse targeting and failed requests
require explicit attribution or exclusion. Capture human and model provenance
independently and never turn a failed request into a successful cast label.

Use a new versioned schema and compatible policy heads for movement, target,
ability and timing/wait actions with legality masks. Disclose any movement-encoder
transfer; retain old checkpoints unchanged and reset incompatible optimizers.
Combat samples cannot use the current out-of-combat `ready` gate. Training and
execution need identical semantic action contracts, bounded client input, focus
and manual-override guards and a user-selected model-play controller. Hybrid
proximal-policy-optimization updates still accept only that hybrid version's own
on-policy experience plus separately labelled human behavior-cloning samples.

## Completion criteria for the next phase

Offline tests must cover encounter coverage/masks and missing coverage, dungeon
and build isolation, timing/target attribution, incompatible model rejection,
invalid-action masks, manual handoff, no model-labelled human actions, exact-once
completion/publication and no automatic reinforcement-learning launch. Live checks
must then establish observation correctness, explicit play handoff and performance
on each supported dungeon. Changed weights, a lower loss or an invitation is not
evidence of dungeon-completion ability.
