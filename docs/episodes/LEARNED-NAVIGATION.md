# On-demand learned navigation

October 26: `waiepisode start N` supports 1–4 concurrent parties with isolated instances and journals,
and one shared controller/trainer. Collection freezes a model version until all parties finish;
one combined update precedes restart. Stop/cleanup/status apply to the pool; `watch N` prints current
GM observation instructions. Secondary account defaults append _2/_3/_4 to the base account, with
Account2/3/4 overrides. Missing secondary accounts use native account creation at episode start.
Parallel operation requires movement integrity off. All four C++ instantiations and 31 focused
Python tests pass; full linking and live operation are unverified. No server was started.

The required policy is `learned`; scripted configuration is rejected, with no fixed-route fallback.
Learned mode is for isolated cloned parties; existing-party attach is therefore unavailable.
Native class combat and followers remain, while the model chooses the tank's
local navigation. No boss destinations are supplied to the policy.

Configure the learner executable, script, home, loopback SOAP URL and optional private voice-bridge
credential file under `Wai.Episodes.Learner.*`. Use the learning environment's Python executable and
`rl_loop.py`. The learning repository's `RL-EPISODE-OPERATIONS.md` contains the complete configuration,
model architecture, reward definition, recording/checkpoint paths and limitations.

`waiepisode start` requests the learner process asynchronously. Normal server startup launches no
learner and loads no neural weights. Each terminal gameplay run is recorded and trained externally;
a validated checkpoint and short measured server summary precede automatic repeat. `waiepisode stop`
cancels cycling and requests termination of the owned process. Server shutdown also requests its stop.
Partial deaths recover through a survivor; a full wipe ends the episode, trains with a failure
penalty and then repeats in a fresh episode. An offline two-wipe regression verifies that order.

The six-line summary measures weight change and before/after action probabilities for the same
sampled state; it is not proof that play improved. Run-start logs print the actual tank's GM observation
commands. Passwords never appear in child-process arguments. Existing core process launching is reused
with a companion fix for early cancellation, borrowed-child lifetime and concurrent stdout/stderr drain.
The common utility's public header is unchanged.

Validation October 25: changed-source MSVC syntax, episode C++ style/format checks, six native episode
helper suites, native process lifecycle regressions and 18 learner/client offline tests passed.
No full worldserver build, live SOAP connection or game episode was started. Linking and live behavior
remain to be confirmed after the user's build. Existing outcome-journal and clone-DB blocking work was
identified separately; this change does not claim to resolve all reported lag.


## October 26: native looting and boss notifications

Running learned episodes enable native loot and gather strategies on every party member.
Preparation still holds the party stationary until entry; running setup restores those strategies.
Navigation yields to open loot, pending loot operations, valid selected loot/gather targets, and
non-melee casts. Native retry deferrals are respected. This changes bot settings, not model weights.
Skinning still requires the normal skill, tools and eligible corpse. Clone inventory remains empty
as requested; profession tools are not automatically supplied.
Boss deaths print one WAI_BOSS_KILLED uppercase headline per boss per episode, with progress,
elapsed seconds and death count. The structured boss_kill training record is retained.
Changed-source MSVC syntax and scoped style pass. Rebuild and live verification remain pending.
