# Recovery and existing-party episodes

## Death recovery and detailed logging

Partial deaths no longer end a run merely because a dead member releases to a
graveyard. The runner records recovery start, waits for all surviving members
and pets to leave combat, stops survivors briefly, uses native ReleaseSpiritAction
and SummonAction with a living survivor as anchor, then confirms the character
is alive in the original instance before resuming. Actions are retried at five
second intervals and recovery is bounded to 120 seconds by default. A full wipe,
a real disconnect, an unexpected exit by a member not in recovery, or a recovery
timeout still ends the episode. Native resurrection by a party member also counts
as recovery completion. Native summon/release settings and side effects (including
configured durability repair and resurrection resources) still apply. This is an
assisted baseline, not a player-only movement benchmark or a trained policy.

```ini
Wai.Episodes.Recovery.Enable = 1
Wai.Episodes.Recovery.TimeoutSeconds = 120
```

These defaults need no new config entries. Existing native PlayerBots
ReviveBotWhenSummoned must be enabled; otherwise the episode logs a clear
recovery_configuration failure. In strict movement mode, recovery teleports
have bounded explicit permits and the arrival surface is revalidated.

New JSONL events include member_state, member_presence, recovery_begin,
recovery_wait, recovery_release, recovery_summon, recovery_complete,
recovery_failed, teleport_requested, graveyard_release, map_changed,
resurrect_requested, logout, route_pause and telemetry_dropped. They include
member slot, source map/instance, alive/ghost/combat/teleport flags, health/mana
and coordinates. Heartbeats and route pauses include party snapshots. Hook
events carry their capture times; use timestamps when reconstructing order.
A teleport request is not completion: recovery_complete confirms the arrival.
The bounded hook queue reports drops rather than silently claiming completeness.

## Run the baseline on existing characters

This runs native PlayerBots plus the scripted Ragefire boss route. It does not
load or train a neural network. The original five configured template names
(Tank, Healer, Damage1..3) also select the existing-party roles for this mode.

1. Start with a fresh Ragefire instance. Log in all five as bots, group them
   together with the configured tank as group leader, and enter the same instance.
   All five must be alive and out of combat. Human-controlled characters are rejected; a human taking control during a run ends the episode.
2. Stop/clean up any previous cloned episode before attaching. Existing-party
   mode cannot replace an active episode or coexist with a pending clone cleanup.
3. In the server console:

```text
waiepisode attach
waiepisode status
```

For in-game GM commands, prepend a dot. The runner records a separate episode
and drives that existing group. It does not clone/delete/log out/disband them,
reset equipment/talents/inventory, or change their XP flag. Normal gameplay
changes such as XP gain, loot, durability and deaths still happen. The mode
saves and restores their prior non-combat strategies and master relationships
on stop, finish or detach while they are online.

```text
waiepisode stop
waiepisode detach
```

Detach releases the roster without deleting characters. Clone cleanup/repeat
is explicitly rejected for an attached party. Start with all four bosses alive;
this mode does not reconstruct boss kills from before attachment.

Validation: changed-source MSVC syntax and offline helper tests, including the
production recovery presence policy, plus formatting. Full server build and
live recovery/attachment are pending. No build or server was started.
