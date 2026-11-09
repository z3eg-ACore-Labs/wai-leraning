# WAI module and learning workflow

Server components live under `src/`; optional Python tools under `apps/learning`. Keep their concerns
separate inside this repository. Install the repository as `modules/mod-wai`. Native build/install
does not authorize starting Python, training models or changing the current WAI deployment.

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

## Terminology

Use established scientific terms in future work: imitation learning (IL), behavior cloning (BC), reinforcement learning (RL), and proximal policy optimization (PPO). Describe the combined track as hybrid imitation and reinforcement learning; do not present hybrid as a distinct named algorithm. Avoid user-created nicknames such as assisted learning when the standard term is known. Preserve historical wording in chronology, with dated corrections.

Prefer full scientific names in operational displays and explanations so the user learns them through repeated use. Spell out imitation learning, reinforcement learning, behavior cloning, and proximal policy optimization; abbreviations may remain in stable technical identifiers.
