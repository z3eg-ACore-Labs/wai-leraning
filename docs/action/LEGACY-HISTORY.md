# WAI action mode: logged-in bear experiment

Status: opt-in server module and external PyTorch client implemented. No server
build, controller startup or in-game execution was performed for this change.
This is an assisted experiment in Wailing Caverns, not a general dungeon solver.

## What controls what

- The trained 16-12-8-6 neural network chooses among six demonstrated spells.
  The client loads the existing learned parameters into standard PyTorch Linear
  and ReLU layers and runs inference_mode on CPU.
- The server chooses a nearby candidate (engaged enemy, unbuffed party member,
  selected hostile, or nearby hostile/owned corpse). This target choice is a
  deterministic helper, not a learned target policy.
- Approaching, autoattack and corpse looting use ordinary server movement,
  spell and loot handlers. No item duplication or triggered/free casts are used.
- Optional recorded waypoint replay provides movement along this short WC
  demonstration. It is explicitly labelled route replay, not learned navigation.
  It does not know how to finish an unfamiliar dungeon.

The first version controls only the human druid that explicitly grants the
lease with `.wai start`. Ordinary party bots continue using native PlayerBots.
SelfBot must be off; the module rejects characters already controlled by its AI.
It does not modify the voice bot `mode action` command, which addresses a
different bot-control mechanism. Commands for this experiment begin with `.wai`.

## Build/deploy (user initiated only)

The module resides next to mod-playerbots and mod-wai and uses
normal module loader discovery. Include it in an explicitly requested CMake
configuration/build. Deploy the resulting executable and module configuration
using the existing server workflow. Enable in the active `wai.conf`:

```ini
Wai.Action.Enable = 1
Wai.Action.Map = 43
Wai.Action.AllowRouteReplay = 1
```

Leave AllowRouteReplay at 0 for stationary/nearby target tests. The client only
uses route replay when also passed `--route`. A reload stops any existing lease;
it never automatically grants control.

SOAP must be enabled on loopback in the active worldserver configuration:

```ini
SOAP.Enabled = 1
SOAP.IP = "127.0.0.1"
SOAP.Port = 7878
```

Use an existing SOAP-authorized administrator account. Credentials are read
from WAI_SOAP_USER and WAI_SOAP_PASSWORD environment variables; do not commit
them or put them in command arguments. The module rejects model actions from
normal game clients. Starting control requires the logged-in character itself.

## Start and stop

First install the workspace CPU dependencies explicitly:

```powershell
& .\wai-learning\Setup-Wai.ps1
```

Then, from the workspace root in PowerShell 7:

```powershell
& .\wai-learning\Start-Wai.ps1
```

The launcher reuses the voice bridge's saved SOAP credentials automatically when
WAI_SOAP_USER and WAI_SOAP_PASSWORD are both unset. It reads the same local store
using the bridge's credential helper; it does not copy or print its contents.
Use `-VoiceCredentialPath path/to/credentials.json` for another bridge store.
An explicitly set pair of WAI environment variables takes precedence. A partial
pair is rejected rather than mixing credentials from different accounts.
If neither source is available, it prompts without echoing the password. It starts
only the controller, never the game server. The direct Python entry point is
`wai-learning/live.py --model output/wai-learning-demo/model.json`; it reads the
same environment variables if a manual launch is preferred.

The client waits until you enter WC as the druid and issue in game:

```text
.wai start
.wai status
.wai stop
```

Grant the lease after the client is running: its first heartbeat must arrive
within three seconds. Keep the game client connected. Movement/casting/selection
input revokes control; `.wai stop` is the explicit stop. Ctrl+C in the external
client sends stop; if the connection fails, the server lease expires itself.

For route replay, pass `-Route path/to/events.jsonl` to Start-Wai.ps1 (or
`--route` with the direct Python entry point).
Start at the recorded entrance position. Every step must remain within eight
yards of the actor's current position and is sent through path-generated movement;
a stale, unreachable or wrong-floor recording is not teleported into place.
The client stops at the end of its waypoint list without claiming a dungeon clear.

## Lease and result semantics

Only one character can grant a lease. Observations have a generation, sequence,
bound target and one-second action deadline. Each sequence is consumed once,
including rejected attempts. Old generations and duplicate actions are rejected.
A missing controller for three seconds, death, logout, changing map/instance,
configuration reload or manual input releases control and stops automation.
An inactive observation request never repeatedly stops human movement.

Spell legality, ownership, resources, cooldown and target checks are performed
again before a normal spell request. A requested cast is not confirmation that
the cast completed. Loot helpers honor ordinary loot and inventory restrictions;
"loot attempted" is not confirmation that every item was acquired. Group-roll
decisions, quest completion, recovery and autonomous route finding are deferred.

The external client writes learned/native/scripted provenance and server receipts
to local live-decisions.jsonl. It never stores SOAP credentials. Training from
these automated attempts requires separate provenance filtering; do not treat
them as new human demonstrations.

## Validation

The authenticated `wai client watch <full-guid> <observer-token>` request is a passive
human-dungeon observer. It does not acquire/renew the action lease or move a player.
It reuses client features across non-raid dungeon maps. The existing global
encounter-credit hook proves completion using required encounter masks, owned final
credit, real state updates and exact map/difficulty/instance/party identity. Missing
coverage stays unknown. Four observed Ragefire boss identities remain an additional
proof. Run progress is separate from the explicit controller lease. Fresh competing controllers,
native SelfBot or interrupted continuity invalidate human-run completion. Death is
not completion. The desktop helper and deployment guide live in the companion
learning repository's `AUTOMATIC-DEMONSTRATIONS.md`. Existing explicit action and
episode commands are unchanged. This addition is offline validated only and needs
a new server build and user restart before use.

Offline tests cover lease expiry, generation/sequence matching, duplicate and
stale action rejection, action masks, busy handling, native-helper labelling,
local-only SOAP configuration and command-injection rejection. Server API usage
was checked against the local headers. The full module has not been compiled
or exercised in game; live motion, packet timing and cast/loot behavior require
the user-initiated build and first controlled test.
