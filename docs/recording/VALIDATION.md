# Extraction validation

November 8, 2026. Exact original working-tree hashes are in `source-provenance.json`. The original
core-tracked module was copied additively; its source, recordings and runtime configuration are unchanged.

Fresh checks:

- All three source translation units passed MSVC syntax against the pair in `COMPATIBILITY.md`, with
  supported QoL client API, no QoL header and an unsupported API-v2 header without client-control symbols.
- Three native C++20 `/W4 /WX` fixtures passed navigation-token, navigation snapshot and profile policy.
- Actual WAI adapter fixtures passed null/old/current/unknown session detection, optional contract
  absence/version rejection, SelfBot permissions, mode handoff and failed-new-attachment cleanup.
- Shared actual-source QoL client fixtures cover external mode requests and normal trade/input/leadership
  behavior. All 19 Python JSONL validator tests passed. See CI and current local check results.

Run `python -m unittest discover -s tests/recording -p "test_*.py"`; compile/run the three policy fixtures with C++20.
Run `python tests/recording/check-adapter.py --qol /path/to/mod-playerbots-qol` for the optional API matrix (`CXX`
selects the compiler). Without `--qol`, only missing/unsupported contract variants are checked.
CI uses that self-contained matrix; the supported-contract test was run locally against QoL
`d6661a411bd226a2f472b0a0b66dd16c095b63d5`, without requiring cross-private-repository CI access.

No full configure/build/link, runtime filesystem write, live entry/exit/navigation, model operation,
SOAP call, server/client launch or database operation was performed for this extraction. Historical
reports in `LEGACY-HISTORY.md` describe the previous WAI implementation, not these extracted sources.
