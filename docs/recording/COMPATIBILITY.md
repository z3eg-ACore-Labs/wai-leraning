# Compatibility boundary

Checked source pair: core `e7e2e288e7d9ec30e2dfe6f417178a95986fcbb2`, PlayerBots
`405f81196161017eebae3ee7a612d786a3ed2a9d`. This is an actual checked pair, not a promise about future
upstream changes. No native core patch is introduced by the recorder itself.

`src/integration/WaiPlayerbotsAdapter.h` owns the WAI-to-PlayerBots boundary. It isolates current `IsHeadless` and
previous `IsBot` session APIs and treats an unknown PlayerBots session API as unavailable. Ordinary
humans require no attached AI. AI-controlled characters require a known manual mode to qualify for
human recording, preventing autonomous samples from being relabelled as human demonstrations.

Navigation handoff optionally consumes `QolClientControl.h` with
`AC_PLAYERBOTS_QOL_CLIENT_CONTROL_API == 1`, plus registered reader/setter callbacks. Missing/unsupported
contracts compile out those calls and yield a meaningful startup/command message. QoL keeps client
mode/queue/movement state; this module keeps navigation lease/generation/sequence state. The adapter
respects native SelfBot permission and cleans up a newly attached AI if handoff fails. No separate
mandatory adapter repository is required. Static linkage is assumed for shared inline callback state.

Source checks exercised missing, supported and unsupported optional QoL headers. Arbitrary upstream
class/layout changes can still require adapter updates; runtime exception handlers cannot repair them.
Vanilla core without PlayerBots is intended for recorder-only use but has not been independently
syntax-checked against a separate vanilla checkout. Do not claim that test from the paired-core checks.
