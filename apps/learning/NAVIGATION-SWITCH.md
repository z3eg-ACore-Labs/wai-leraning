# Human recording and learned navigation

Implemented October 29, 2026. This is an experimental navigation-only policy.
Native PlayerBots handles combat; no learned combat or automatic reinforcement
learning is started. A model covers demonstrated regions, not every possible
dungeon branch or a guaranteed dungeon clear.

## In-game commands

| Command | Effect |
| --- | --- |
| `.wainav run` | Your logged-in character requests a compatible model for this dungeon and difficulty. |
| `.wainav record` | Cancel model control and return to human recording on the next game tick. |
| `.wainav stop` | Alias for returning to human recording. |
| `.wainav status` | Report pending model selection, model control, active recording, or why recording is waiting. |

You can switch repeatedly without leaving the dungeon. The explicit character
selection takes priority over automatic single-human selection. Normal automatic
recording remains available before any command. Movement, a manual spell/attack
or item use cancels model control too. The standard client-bot manual command is
also honored. Only one character can own this experimental navigation lease.
Request run while out of combat and trade; returning to record is available
during combat too.

Human recording closes at model handoff and resumes as a new segment afterward.
Model movement and native combat are excluded from human demonstration files.
Closing a segment does not mean the dungeon was completed.

## Startup

The requested build publishes the navigation helper launcher and prepares its
path-only settings for the next **manual server startup**. It does not restart
the running server. The helper follows the actual server process and uses the
existing encrypted voice SOAP credentials; credentials never appear in arguments
or the settings file. Existing helper settings are preserved.

Alternatively, start the helper yourself from the workspace in PowerShell 7:

```powershell
& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -ExecutionPolicy Bypass -File .\wai-learning\Start-Navigation.ps1
```

The launcher requires PowerShell 7; use your installed PowerShell 7 executable
with `-NoProfile -ExecutionPolicy Bypass -File .\wai-learning\Start-Navigation.ps1`
when Windows PowerShell is your default. It never starts a server or game.
Use the published server launcher for automatic helper ownership and training
directory configuration. The learning environment needs its existing PyTorch and
filelock packages; there are no runtime model downloads or cloud inference.

`GameplayNavigation.Enable` defaults to availability, but nothing moves until
the character types the run command. Set it to zero to disable run requests.
`GameplayObserver.Enable` still controls capture independently. Existing
PlayerBots client-bot permission settings are respected.

## Learning and execution

The helper trains closed human segments in a separate worker and retains accepted
per-dungeon/difficulty memories. It deduplicates unchanged recordings and never
starts reinforcement learning. A rejected candidate does not replace an accepted
model. New short segments do not erase older memories. Model selection uses
nearby demonstrated support and loads a frozen policy for that control session.

Training learns unit travel direction from position, heading and demonstrated
progress using a small neural network. One-second movement windows reduce packet
cadence noise. Reference positions bound supported locations and progress; the
network, rather than waypoint replay, predicts movement direction. The server
supplies ordinary movement speed and validates a complete navigation-mesh path.
No missing-path straight-line fallback, forced destination or teleport is used.
Missing navigation meshes and unsupported water/flying paths return control.

Generation, sequence, duplicate and one-second action checks reject stale model
commands. Movement is applied on the character's game update thread, not inside
SOAP. Native combat has exclusive control during fights. Manual takeover,
death, map/instance change, logout, configuration reload and a three-second
helper timeout stop automation. No movement progress for eight seconds or route
progress for twenty seconds also returns control. Logs distinguish queued steps
from actual movement; queue acceptance is not execution confirmation.

## Evidence and limits

Offline validation covers human-only boundaries, casting/combat/death gaps,
teleports, model scope and nonfinite values, maintenance/promotion, duplicate
processing, SOAP restrictions, startup with mocked processes, publication
preservation and native generation/deadline checks.

The retained Maraudon checkpoint uses 584 movement windows. Its held-out
same-recording direction mean squared error was about 0.150, below the 0.20 gate.
This measures interpolation among related examples, not independent run success.
Earlier short-interval models failed; they were not promoted. An offline rollout
without combat reached a support boundary, so native combat transitions and
resuming across those gaps require an in-game test.

No live character control or dungeon-completion test has been performed. Start
with a short familiar corridor, verify status, let it move, then issue record
and confirm immediate human takeover and a new human recording segment. Test
native combat handoff separately before attempting a full run. Unsupported
regions should be recorded by you and may become usable after maintenance.

## Build delivery

The requested guarded build completed successfully with zero compiler warnings
and errors. Artifact: `20261029-142556_navigation-mode-switch_3872b2d2.exe`.
Core implementation revision: `e32e08f57507d683119ef7480913d1486dc9cfcb`.
Initial Python implementation revision: `b47361a`. A subsequent maintenance fix
preserves a better accepted memory when repeat training has worse validation;
that Python-only change requires no server rebuild. Publication, selected-build
and executable hashes matched.
The helper launcher/settings were published for the next manual server startup;
the configured observer and existing client-bot permission allow this workflow.
The server, helper and live character were not started by this implementation.
