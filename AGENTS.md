# WAI learning workflow

Maintain `wai-experiments-history.md` as a chronological project record. After material user
decisions, implementation changes, validation findings or inspected run results, append the outcome,
evidence, lesson and resulting adjustment. Distinguish user observation, source implementation,
offline validation, deployment and live verification. Do not claim learning quality from changing weights.

Commit history updates locally to this repository as part of the work. The trainer automatically
appends sanitized completed-update summaries and commits only the history file. Do not stage unrelated
files, overwrite other work, include credentials/account linkage/raw private logs/identifying machine
paths, or push without explicit authorization. If automatic Git recording fails, retain its private
pending receipt and preserve model operation; do not delete checkpoints or silently retrain old runs.

Do not start a server or full build unless the user requests it. Preserve the configured one-worker
build/publish workflow inherited from the workspace instructions.
