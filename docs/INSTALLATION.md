# Installation choices

This repository installs as one ordinary AzerothCore module, `modules/mod-wai`. Its GitHub name is
`wai-leraning`; the clone destination is intentional because the native loader uses the module folder name.

Server-only users need the root `src`, `conf` and documented native prerequisites. The optional Python
source in `apps/learning` adds no installed packages, runtime overhead or model downloads by itself.
Configure/build normally, copy `wai.conf.dist` to `wai.conf`, then explicitly enable only the desired
components. Start with a separate upstream test checkout; keep the existing custom WAI deployment intact.

Learning users create a normal Python environment under `apps/learning`, install its requirements and
configure private credentials/models. No custom installer is required. Run the matching controller only
when ready, then grant the documented player/administrator lease. Recording and telemetry are not
evidence of training quality or completed game actions.

Read [recording](recording/README.md), [character control](action/README.md), [episodes](episodes/README.md),
[native compatibility](../compatibility/README.md) and [validation](VALIDATION.md). Do not compile the old
three WAI modules alongside this consolidated loader or apply old policies twice. Preserve clone ownership
records and private models/configuration when planning a later explicit cutover.
