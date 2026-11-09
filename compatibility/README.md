# Native and optional prerequisites

Checked paired source: core `e7e2e288e7d9ec30e2dfe6f417178a95986fcbb2` and PlayerBots
`405f81196161017eebae3ee7a612d786a3ed2a9d`. Future upstream revisions need their own checks.

| Feature | Boundary |
| --- | --- |
| Ordinary human recording / character telemetry | Existing core hooks; WAI session/AI adapter |
| Learned navigation with native combat | Optional QoL client-control API v1, enabled client control and native SelfBot permission |
| Bot `mode` replies | Optional generic PlayerBots extension API v1, packaged by QoL; gameplay QoL features need not be enabled to expose that native hook |
| Episode movement integrity | `spline-validation.patch`, generic callback only; movement policy remains in WAI |
| Supervised learner/observer launches | `process-cancellation.patch`, native lifecycle fix and API-v1 capability marker |
| Voice bot-mode discovery | Optional `WaiExternalCommands.h` API v1; native authorization still applies |

For episodes, from the target core root before the normal configure/build:

```sh
git apply --check modules/mod-wai/compatibility/spline-validation.patch
git apply modules/mod-wai/compatibility/spline-validation.patch
git apply --check modules/mod-wai/compatibility/process-cancellation.patch
git apply modules/mod-wai/compatibility/process-cancellation.patch
```

Spline support adds a narrow header/implementation and one launch callback. Missing/unsupported support
compiles an episode-unavailable path instead of weakening movement checks. Process support publishes the
live child before pipe drain, protects borrowed-child lifetime/cancellation, remembers early cancellation
and drains stdout/stderr concurrently. The rebase retains current upstream process-start error handling.
Its header adds a capability marker. These native changes require rebuilding affected core code.

Without safe process API v1, episode starts reject before account preparation and helper launch is refused.
Other installed components stay available. No patch contains model, bot/class, drop or recording policy.
Check conflicts rather than forcing old patches over changed native code. Reverse-check before removal;
preserve private clone ownership/state until intentional cleanup is complete.

Static linkage is the checked target for inline callback registries. Unsupported optional interfaces are
excluded at compile time where feasible; native signatures/layouts can still require adapter maintenance.
Vanilla-core compilation without PlayerBots has not been independently verified against a separate checkout.
