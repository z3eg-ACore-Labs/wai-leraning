# Validation and preservation

The final package uses the existing `wai-leraning` GitHub repository: server module at the root, optional
Python tools under `apps/learning`. Server source/header hashes match the previously checked consolidated
module exactly. After moving the Python tools, 95 tests and 5 subtests passed, plus pure PowerShell
runtime/credential/launcher checks. A sibling-output-directory assumption was removed from old test
fixtures; launchers no longer import voice files or depend on the original workspace layout.

The offline learning environment contained Python 3.12, CPU PyTorch 2.14.1+cpu, NumPy 2.5.4, filelock
3.32.3, pytest 9.1.1 and TensorBoard 2.21.0. Tests use temporary synthetic models/data; no production
weights, game client, running learner or live transport was used. CI runs are separate evidence.
Upstream reference refresh succeeded November 9; both checked heads remained unchanged.

The consolidated source comes from the additive recording/action/episode extractions. Original input
revisions and working-tree hashes are recorded per component in `source-provenance.json`; they do not
establish deployment. Historical standalone layouts are superseded by the single-module structure.

Before consolidation, current-pair syntax checks passed each component with/without its native or optional
prerequisites. Modified native spline/process units passed syntax; the rebased process implementation
passed real owned-child lifecycle tests (live stop, ten startup cancellations, dual-pipe flood, exit status
and failed spawn). Eight native episode policy/registry fixtures passed. The observer launcher passed
offline mocked-slot handling and ValidateOnly account-configuration checks; no client/window/network API
was called. Recording and action source/fixture evidence is in their component validation notes.

November 9 consolidated checks passed: all seven translation units with supported, missing and unsupported
PlayerBots extension/QoL client/spline/process interfaces; one loader and shared adapters compiled in every
case. Eleven self-contained native policies, four actual production-hook fixtures, optional client-adapter
matrix, 19 Python validators and offline PowerShell observer checks passed. `python tests/check-all.py`
repeats self-contained checks; add `--qol /path/to/mod-playerbots-qol` for the supported optional contract.
Native process tests require the matching patched core/Boost. CI results are separate from local evidence.

These results must not be presented as a full-module link. No configure/full worldserver build, server/client launch, database
operation, live gameplay, production model/controller session or deployment was performed. Temporary
synthetic model operations were confined to the offline learning tests described above.

Fresh preservation comparison matched every recorded original core/PlayerBots source hash and HEAD/dirty
status (10,452 core files, 1,781 PlayerBots files). Existing original WAI/learning configuration, runtime
snapshots and pre-existing dirty work are retained. New source changes remain additive.
