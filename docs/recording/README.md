# Recording and navigation

## Recording

Set `GameplayObserver.Enable = 1` and use the normal config reload or restart. Capture defaults disabled.
`GameplayObserver.Profile = "navigation"` records position/facing, alive/combat/casting boundaries and
movement requests. `"full"` adds bounded party/selected-unit state, spell catalog/request/cast information
and interaction categories. Other profile values select navigation. Neither profile changes native AI.

Selection waits for one eligible in-world human. Headless bots and characters under AI control are
excluded; explicit navigation selection takes priority. No file is created outside a dungeon. Each entry
gets a separate file under `GameplayObserver.OutputDirectory` (default `gameplay-observations`).
Exit, logout, shutdown or selection change
closes the segment. `GameplayObserver.IntervalMs` defaults 200 and clamps to 100–5000 ms. Keep recordings
private; they contain gameplay telemetry. Do not commit or distribute raw visit logs.

Validate a saved visit without connecting to the server:

```sh
python tools/recording/validate_observation.py /private/path/visit.jsonl
```

The validator checks schema/provenance/sequence and shutdown evidence. It always reports
`training_ready: false`; a valid log does not establish suitable training labels or action outcomes.
Cast-go means release, not confirmed damage/healing. Client-suppressed inputs cannot be observed.

## Commands

| Player command | Meaning |
| --- | --- |
| `.wainav status` | Report control, recording and selection readiness |
| `.wainav run` | Explicitly request a compatible model/helper for this character's dungeon/difficulty; native PlayerBots handles combat |
| `.wainav record` | Release navigation automation and return to human recording on the next game tick |
| `.wainav stop` | Alias of `record` |

`GameplayNavigation.Enable = 1` makes requests available by default; it never starts a helper or grants
control automatically. Setting it to 0 rejects new requests. Reload requests release of an existing lease.
No compatible helper/model means no navigation movement. Run a separately configured helper manually;
the learning repository's `NAVIGATION-SWITCH.md` describes its current contract.

Deliberate movement, spells, attacks/item use, helper timeout, death, logout or map/instance changes cancel
the lease. Steps are bounded and use native navigation-mesh paths. Native combat and model navigation
yield movement to each other through the shared QoL client policy. Automated samples are excluded from
human demonstrations. Learned combat and successful dungeon clearing are not implemented claims.

Administrator console/SOAP helper operations are protocol commands, not player commands: `wainav observe`
returns `navigation_delta_v1` state; `wainav ready GENERATION SEQ`, `wainav step GENERATION SEQ X Y Z` and
`wainav release GENERATION SEQ` require the current fresh, unconsumed observation token/sequence. They
cannot initiate a lease without the player's explicit `run`. Follow native SOAP authorization and keep
credentials private. No model, controller, server, game client or microphone starts from cloning.

See [validation](VALIDATION.md) for fresh checks and remaining integration limits. Historical WAI
milestones are retained separately in [legacy history](LEGACY-HISTORY.md); their build reports do
not establish deployment of this extraction.
