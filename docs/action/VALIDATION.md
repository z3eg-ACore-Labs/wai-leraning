# Extraction validation

November 8, 2026. Source input: action module `df5f6a17d7b3b94ea2bb45aad0cf61c7f1d45b20` plus
pre-existing dirty `WaiAction.cpp`/`ClientIdentity.h`/addon work. Hashes are in `source-provenance.json`.
The original module and runtime were preserved; no model, client, recording or secret was copied.

Fresh checks:

- All three extracted translation units passed MSVC syntax against core
  `e7e2e288e7d9ec30e2dfe6f417178a95986fcbb2` / PlayerBots
  `405f81196161017eebae3ee7a612d786a3ed2a9d`. Bot-mode source also passed with available/unsupported
  extension headers; absent extension compiles its diagnostic stub.
- Native C++20 `/W4 /WX` policy fixture: lease generation/sequence, expiry, duplicate consumption,
  addon identity/binding parsing and bot-mode parser boundaries.
- Actual client reward, passive-run and encounter-completion production hooks passed fixtures for
  instance/party/ownership, unique bosses/deaths, interruption/expiry and encounter-mask/final-credit rules.
- Actual bot-mode handler fixture passed authorized observation, unavailable action, invalid command,
  disabled feature and null/unauthorized requester cases.

Compile/run `tests/action/policies.cpp` with C++20. Run each `tests/action/test_*.py --output /private/temp/fixture`
using `--compiler` or `CXX` for your compiler. These native fixture scripts compile doubles, not worldserver.
CI repeats portable policy/hook tests; remote success is separate from local results.

No full configure/build/link, addon/client launch, SOAP call, model inference/training, generated input,
database operation or deployment was performed. Vanilla-core compilation and all real movement/cast/loot,
session identity, helper timeout and per-dungeon encounter behavior remain integration work. Historical
build reports in `LEGACY-HISTORY.md` do not validate these new sources.
