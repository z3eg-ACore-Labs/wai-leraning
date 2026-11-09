# Optional learning tools

These Python controllers/trainers belong to the same repository as the server module. They run outside
worldserver and are installed only when needed. No models, private recordings or Python environments are
bundled. From this directory (`apps/learning`), create a normal environment:

```sh
python -m venv .venv
# Windows:
.venv/Scripts/python -m pip install -r requirements.txt
# Linux/macOS:
.venv/bin/python -m pip install -r requirements.txt
```

Use that environment's Python for the commands below. Configure the matching server components in
`wai.conf` using the [root installation guide](../../docs/INSTALLATION.md). Native prerequisites and
permissions remain required. Install nothing into the existing WAI deployment merely to inspect source.

| Workflow | Entry point / guide |
| --- | --- |
| Explicit druid server-action experiment | `Start-Wai.ps1 -Model path/to/private/model.json`, or `live.py --model ...`; player grants `.wai start` |
| Recorded human navigation / explicit model handoff | `Start-Navigation.ps1`; [navigation switching](NAVIGATION-SWITCH.md), player `.wainav run/record/status` |
| Cloned-party reinforcement learning | `rl_loop.py`; [episode operations](RL-EPISODE-OPERATIONS.md), administrator episode commands |
| Independent client imitation/reinforcement/hybrid navigation | `client_session.py`; [client modes](CLIENT-LEARNING-MODES.md) |
| Passive human demonstrations | `automatic_demonstrations.py`; [capture lifecycle](AUTOMATIC-DEMONSTRATIONS.md) |
| Offline NumPy diagnostic | `train.py RECORDING --output data/demo`; uses the owned API-v1 recording validator |

PowerShell launchers accept `-Python` and `-Credentials` explicitly. The former `-VoiceCredentialPath`
argument remains an alias; no voice checkout is imported or searched. Use either a complete
`WAI_SOAP_USER` / `WAI_SOAP_PASSWORD` environment pair or a private JSON with `Username` and `Password`,
never both. Credentials never appear as Python command arguments. SOAP is restricted to loopback.
Models and optional route files are supplied explicitly; defaults no longer depend on an operator's
workspace layout. The diagnostic validator can be overridden with `--validator` or
`WAI_OBSERVATION_VALIDATOR`; unsupported APIs reject only that diagnostic workflow.

Keep recordings and model/checkpoint stores private under `data/` or another private location. Capture,
controller startup, training and deployment are distinct operations. Current client policies learn
navigation, not general combat. Native combat/helpers and route replay are labelled separately. Changed
weights, lower loss and accepted command receipts are not proof of dungeon completion or better play.

Run `python -m pytest -q` for offline synthetic fixtures. Run `pwsh -NoProfile -File tests/Test-WaiRuntime.ps1`
for explicit-setting/credential validation without launching Python/controllers or making network calls.
See [validation](../../docs/VALIDATION.md), [experiment history](../../wai-experiments-history.md) and
[background/training concepts](TRAINING-AND-DEBUGGING.md). `network-example.html` is the existing small
saved diagnostic visualization; it is not live inference. Separate imitation, reinforcement and hybrid
policy stores, optimizers and evaluation provenance must remain independent.
