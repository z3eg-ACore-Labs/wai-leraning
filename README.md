# WAI server module and learning tools

One server module containing human recording, explicit character control/telemetry and experimental
episode coordination. Components are separated internally and retain individual configuration and
dependency checks. Optional Python controllers/trainers live in `apps/learning` in this same repository;
their dependencies and private models are installed separately. This extraction preserves the existing
WAI source, configuration and runtime.

## Installation

From a separate target core checkout, clone the existing repository into the standard module folder:

```sh
git clone https://github.com/z3eg-ACore-Labs/wai-leraning.git modules/mod-wai
```

Use the normal
AzerothCore configure/build workflow, then copy the installed `wai.conf.dist` to `wai.conf` in the server
configuration directory. Read [native prerequisites](compatibility/README.md) before building for episode
or bot-mode support. No custom installer, automatic database updates, model downloads or client assets
are included. Static linkage is the integration target.

For the optional Python tools, follow [learning setup and usage](apps/learning/README.md). Installing
the server module does not install Python packages or start controllers. Models, virtual environments
and recordings are excluded from source control. Server and learner changes can be reviewed/released
together; their internal boundaries allow a later repository split if independently useful.

Do not compile this module alongside the original `mod-gameplay-observer`, `mod-wai-action` or
`mod-wai-episodes` implementations. Their extracted source now lives under one loader; installing both
would duplicate commands/hooks. AQ mount behavior and `.qirajimount` belong to `mod-aq-mounts`.

## Choose components

| Component | Settings and default | Guide |
| --- | --- | --- |
| Human recording | `GameplayObserver.Enable = 0`; profile `navigation` or `full` | [Recording](docs/recording/README.md) |
| Explicit learned-navigation handoff | `GameplayNavigation.Enable = 1` permits requests only; needs QoL client API and a manually configured helper/model | [Recording/navigation](docs/recording/README.md) |
| Druid server-action experiment | `Wai.Action.Enable = 0` | [Character control](docs/action/README.md) |
| Client observations/passive telemetry | `Wai.Action.Client.Enable = 0` | [Character control](docs/action/README.md) |
| Bot mode-status replies | `Wai.Action.BotModeCommands = 1`; `mode action` explicitly unavailable | [Commands](docs/action/README.md) |
| Cloned-party episode harness | `Wai.Episodes.Enable = 0`; learned navigation required | [Episodes](docs/episodes/README.md) |

Settings keep their existing names in one configuration file. Availability does not start recording,
grant a lease, launch a helper, create characters or train models automatically. Those operations require
their documented enablement and explicit user/administrator commands. Keep filled configurations,
credentials, client identities, raw recordings, clone ownership journals and models private.

Missing episode movement prerequisites produce an unavailable episode command/startup message while
recording and character telemetry remain separate. Missing safe process support rejects helper/episode
starts. Missing optional QoL control rejects navigation handoff while ordinary human recording remains
available. Runtime exception handlers cannot repair arbitrary upstream C++/ABI changes; source checks
and a later integration build are still necessary.

## Source structure

`src/recording`, `src/action` and `src/episodes` own feature state/policy. `src/integration` owns the WAI
session/client-control/process/spline boundaries and optional voice command discovery. QoL retains its
client state; WAI retains leases, recordings and episodes. `tests/` and `docs/` follow the same boundaries.
There is one repository, loader and configuration file; no separate mandatory adapter repository.

See [validation](docs/VALIDATION.md) for checked revisions and limits. Full worldserver linkage, live
movement/casting/loot, real recording/encounters and model behavior remain separate integration work.
Historical component notes describe earlier implementations and do not establish deployment here.
