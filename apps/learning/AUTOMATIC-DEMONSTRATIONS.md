# Automatic human dungeon demonstrations

## Design recorded before implementation — October 28, 2026

The user starts WoW normally. The established server `Start-Wai.ps1` launcher also
starts one resident local observer when its private observer settings exist.
The observer waits for server readiness and a single matching WoW process; it
exits when its owning server exits. Running a worldserver executable directly
does not run the PowerShell launcher integration. Private settings select the
character and learning directory; no account identifiers belong in examples.
The observer never opens WoW, generates input, grants an action lease, starts a
server, or invokes reinforcement-learning episode commands. Entry into the
configured Ragefire instance starts human demonstration recording automatically.
Only authoritative observation of all four party-owned boss kills completes a run.
Completion closes and saves the recording before starting behavior-cloning updates
for imitation learning and human-only updates for hybrid imitation and reinforcement
learning. Human runs supply zero proximal-policy-optimization transitions. Existing
independent optimizers, immutable generations, input hashes and atomic publication
remain the training boundary. The pure reinforcement-learning policy stays unchanged.

Reuse client observation features and boss identities through a separate passive
authenticated observation request. It must never acquire or refresh the action
lease. A fresh action/client-controller lease or native SelfBot excludes recording.
The existing explicit `.wai start` / `.wai stop` and `.waiepisode start`,
`.waiepisode stop`, `.waiepisode repeat` workflows remain unchanged.

The observer targets one explicit client process and full character GUID. Server
run identifiers distinguish character, instance and observer continuity. Death/wipe
does not mean dungeon completion: recording waits for recovery. Leaving temporarily
pauses the same instance's recording; reentry can resume while this helper remains
alive. Disconnect, stale transport, controller conflict, observer restart and stop
preserve an incomplete recording and cannot train it. Repeated completion polling
and reentry must not train twice. A fresh instance creates a fresh recording.

Only stable foreground human forward/back/strafe labels are accepted. Injected
input, combat, unsupported movement, focus loss, turning, modifiers and ambiguous
directions are excluded. F7 pauses, F6 resumes, F12 stops. Chat/menu focus cannot
be fully inferred by this implementation: pause before using either. Recognized
chat/menu-opening keyboard keys also latch pause. No model takeover keys exist in
the automatic recorder. A completed recording is private local data; public history
contains sanitized training summaries only.

Implementation, offline tests and live verification will be recorded separately.
The desktop observer must remain alive; WoW addons cannot directly launch Python.
Server telemetry changes require a later requested build and manual restart.

## Operating setup

Implementation and the requested build/publication are complete. Private setup,
server restart and live behavior remain unverified. See the build receipt below.
The later all-dungeon request supersedes the initial Ragefire-only design above:
passive recording accepts non-raid dungeon maps; completion uses the existing
server encounter-credit hook where covered, with the original four-boss Ragefire
proof retained. Unsupported completion stays unknown.
The server publisher installs the updated `Start-Wai.ps1`, the observer startup
script and a synthetic settings example. It never enables recording by inventing a
character identity, initializes production weights, or starts a process during publication.

One-time setup, before using the normal server launcher:

1. Explicitly initialize each desired dungeon/difficulty's policies, or use a
   compatible existing registry. Normal Ragefire preserves the existing root store.
   Other stores are `<client-policy-directory>/dungeons/<map>/<difficulty>`:
   `client_training.py initialize --home <that-store> --dungeon-map <map>
   --difficulty <0-normal-or-1-heroic>`. Each checkpoint records dungeon identity;
   maintenance rejects a foreign dungeon/difficulty. No source or optimizer is copied
   automatically. Without a store, capture still works; a completed recording is
   saved with training pending explicit initialization. No random production policy
   is silently created. Retry that saved recording with the matching store after setup.
2. Copy `automatic-demonstrations.example.json` to the server home's private
   `configs/automatic-demonstrations.json`. Set the real learning directory, policy
   base home, verified strafe bindings, existing encrypted voice credential file and full
   character GUID. Keep this file private. Within each store, imitation and hybrid policies must share input dimensions.
3. Enable `Wai.Action.Client.Enable = 1` using the normal configuration workflow.
   Passive watching accepts non-raid dungeon maps; `Wai.Action.Client.Map` still
   selects the separate explicit model-controller lease's map. Use `.bot client off` on the human character. Do not grant `.wai client
   start` for automatic demonstrations: passive watching needs no control lease.
4. After a requested rebuild/publication, start the server using its established
   `Start-Wai.ps1`. It starts a hidden helper in the same Windows login session,
   which waits for server readiness and exactly one `Wow.exe`. Start WoW normally.
   Multiple game clients suspend capture rather than guessing. Keep the intended
   character in that client. The helper does not verify client-account linkage.

The source launcher is `release-tools/Start-Wai.ps1`; the everyday installed copy
is in the shared server home. Opening the server executable directly bypasses
this integration. No service, scheduled task, WoW launcher or audio service is added.
Private helper output goes to `logs/automatic-demonstrations-<server-pid>.log`
and its separate error log. A missing settings file is reported by the launcher.
Python singleton locking allows one recorder per policy home. The helper holds an
OS handle to the actual server process, so PID reuse cannot keep an orphan running.
When the server exits, recording stops; an already-running atomic maintenance
transaction finishes before the helper exits. No process is killed by this helper.

## Daily controls and completion evidence

Entry requires no F6. F7 pauses; F6 explicitly resumes after intentional pauses;
F12 stops the observer until the next server start. Enter, Escape and the default
slash key latch pause, as does detected injected input. Custom chat/menu bindings
and mouse-opened menus are not fully detectable: **F7 before chat or menus, F6 only
after closing them**. Pause remains intentional across entries. F8/F9/F10 pause
this recorder; they never activate a model. Focus loss and unstable samples are
excluded without generating key-up or movement input.

Each run has a private immutable `human-<server-run>-<map>-<difficulty>-<instance>.jsonl` recording.
Temporary departure preserves the open recording and reentry into the same instance
can resume it. Death/wipe waits for recovery. Switching instances closes the old
recording as incomplete. Disconnect, transport interruption, changed client, native
SelfBot or action-controller conflict closes it as incomplete and prevents reuse
of that run. A fresh instance creates a new recording. Starting observation midway
through an already-cleared instance cannot reconstruct earlier boss kills.

For covered maps, `OnAfterUpdateEncounterState` supplies real kill/spell encounter
credits and the instance's authoritative completed mask. Coverage requires a real
instance script, valid 32-bit encounter indices and a final-encounter definition.
The recorder requires **every listed encounter bit**, an owned final-encounter
credit observed during the run, an actual mask update and exact map/difficulty/
instance/party scope. A finale observed before other bosses is retained until the
remaining required bits complete. Duplicate credits cannot create a new run.
Optional/branch definitions may make this deliberately conservative: coverage is
reported per live map/difficulty in `completion_available`, `required_mask` and
`completed_mask`, not claimed universally. Missing metadata/scripts or unowned/
unsupported credit never imply success. The original four observed, party-owned
Ragefire bosses also remain a valid proof. The initial completed mask is recorded
so joining an in-progress instance is distinguishable from entrance capture.

Stop, time, exit, death and arbitrary kills do not prove completion. The observer
closes the file before queuing maintenance; polling continues while the single
worker trains. Check the final `session_end` proof and the matching dungeon store's
`latest-training-summary.json`:
imitation learning and hybrid may advance, hybrid reports zero reinforcement decisions,
and reinforcement learning reports `explicit_reinforcement_only`. A zero-sample run
does not change weights. Checkpoint session hashes prevent duplicate maintenance;
repeated completion and reentry do not enqueue the recording again.

If maintenance fails, the completed recording remains available for explicit retry:
`client_training.py update --home <same-policy-home> --session <completed-human-jsonl>`.
Partial/incomplete recordings are rejected. Do not edit a recording to fake success.
Old checkpoint generations remain intact. Public history contains sanitized update
summaries, never raw recordings or identities.

## Scope and validation

Offline lifecycle and training tests cover entry, death/recovery, same-instance
reentry, new instances, interruption, immutable partial logs, duplicate completion,
human-only hybrid updates and unchanged reinforcement-learning checkpoint bytes, generic encounter-mask proof,
foreign dungeon rejection and isolated dungeon stores.
Native fixtures exercise production passive and existing client reward hooks;
the modified translation unit passes syntax-only compilation. No server, game,
production initialization/training, build or live input was run for this change.

Non-raid dungeon capture and separate dungeon/difficulty store routing are
implemented. Generic completion is conditional on the real server encounter
metadata and credit path; live coverage for individual dungeons remains unverified.
A remembered-dungeon bot invitation and automatic matching model-play selection
remain follow-ups. Joint learned navigation and combat is a separate contract
expansion described in `DUNGEON-MEMORY-AND-COMBAT-PLAN.md`. Existing F8/F9/F10 policy play remains in `client_session.py`
and requires explicit setup/control. These policies learn navigation, not combat or
proven dungeon completion. Existing `.waiepisode start`, `.waiepisode stop`,
`.waiepisode repeat` reinforcement-learning operations remain explicit and unchanged.


## Requested build receipt - October 28, 2026

`20261028-181145_automatic-dungeon-recording_3872b2d2.exe` was built and published successfully. The named and published hashes
match the manifest; normal linker output is preserved. Compilation/link reported
zero warnings and zero errors. Guarded offline movement preflight passed. The
installed server/helper startup scripts match these sources, and this build is
selected for the next manual launcher start. Existing unrelated dirty source
state is recorded in the manifest. No server/game/helper was started, and no
production policy was initialized or trained. Private observer settings are still
absent, so starting the server alone does not yet enable capture. Configure the
private settings and desired matching model stores using the setup section above.


## Private setup preparation - October 28, 2026

The passive-observation flag is enabled in the installed module configuration for
the next server start, with the prior file backed up. A private, ACL-restricted
`configs/automatic-demonstrations.pending.json` and separate bindings file have
been prepared and checked offline. The draft is not active: the intended character
GUID and actual movement bindings still need confirmation. No previous addon or
account identity was inferred. The Python/dependency/credential paths and loopback
endpoint were checked without a service connection or runtime launch. There are
no existing policy registries/checkpoints; no model initialization or training ran.
Rename/activation is deferred until the character and bindings are resolved.
