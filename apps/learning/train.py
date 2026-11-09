"""Small reproducible behavior-cloning experiment; no game/server control."""
import argparse
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path

import numpy as np

FEATURES = ["Self health", "Self resource", "Uses rage", "Self in combat", "Bear form",
            "Selected ally", "Selected self", "Selected corpse", "Selected health",
            "Selected distance / 100 yd", "Party mean health", "Party lowest health",
            "Party in combat", "Party missing Mark", "Selected has Mark", "Self casting"]
SPELL_NAMES = {5232: "Mark of the Wild", 5487: "Bear Form", 6808: "Maul",
               779: "Swipe (Bear)", 16857: "Faerie Fire (Feral)", 8618: "Skinning"}


def load_records(path):
    raw = Path(path).read_bytes()
    lines = raw.splitlines()
    records = []
    ignored = False
    for i, line in enumerate(lines):
        if not line.strip():
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            if i != len(lines) - 1 or raw.endswith(b"\n"):
                raise
            ignored = True
    return records, {"source_sha256": hashlib.sha256(raw).hexdigest(),
                     "bytes": len(raw), "incomplete_tail_ignored": ignored}


def features(state):
    me = state["self"]
    party = [me, *state.get("party", [])]
    selected = state.get("selected") or {}
    ally_ids = {u["actor"] for u in party}
    hp = lambda u: u.get("health", 0) / max(1, u.get("max_health", 1))
    distances = np.asarray(selected.get("position", me["position"]))[:3] - np.asarray(me["position"])[:3]
    return np.clip(np.asarray([
        hp(me), me.get("power", 0) / max(1, me.get("max_power", 1)),
        me.get("power_type") == 1, me.get("combat", False), 5487 in me.get("auras", []),
        selected.get("actor") in ally_ids, selected.get("actor") == me["actor"],
        bool(selected) and not selected.get("alive", True), hp(selected),
        np.linalg.norm(distances) / 100,
        np.mean([hp(u) for u in party]), min(hp(u) for u in party),
        np.mean([u.get("combat", False) for u in party]),
        np.mean([5232 not in u.get("auras", []) for u in party]),
        5232 in selected.get("auras", []), bool(me.get("casts")),
    ], dtype=np.float64), 0, 1)


def dataset(records):
    # Only pre-handler state is used. No spell-specific cooldown, outcome,
    # absolute position, actor ID, timestamp or future frame is a feature.
    return [r for r in records if r.get("kind") == "input" and
            r.get("category") == "ability" and "spell" in r and
            not r.get("decode_error") and r.get("source") == "human_request" and
            r.get("state_phase") == "pre_handler"]


def initialize(outputs, seed=7):
    rng = np.random.default_rng(seed)
    sizes = [len(FEATURES), 12, 8, outputs]
    return [[rng.normal(0, np.sqrt(2 / n), (n, m)), np.zeros(m)]
            for n, m in zip(sizes, sizes[1:])]


def forward(model, x):
    values = [x]
    for w, b in model[:-1]:
        values.append(np.maximum(0, values[-1] @ w + b))
    logits = values[-1] @ model[-1][0] + model[-1][1]
    exp = np.exp(logits - logits.max(axis=-1, keepdims=True))
    values.append(exp / exp.sum(axis=-1, keepdims=True))
    return values


def gradients(model, x, y):
    values = forward(model, x)
    n = len(y)
    loss = -np.log(np.maximum(values[-1][np.arange(n), y], 1e-12)).mean()
    delta = values[-1].copy()
    delta[np.arange(n), y] -= 1
    delta /= n
    grads = [None] * len(model)
    for i in reversed(range(len(model))):
        grads[i] = [values[i].T @ delta, delta.sum(axis=0)]
        if i:
            delta = (delta @ model[i][0].T) * (values[i] > 0)
    return float(loss), grads


def train(x, y, outputs, epochs=1600):
    model = initialize(outputs)
    first = [[np.zeros_like(p) for p in layer] for layer in model]
    second = [[np.zeros_like(p) for p in layer] for layer in model]
    snapshots = []
    for epoch in range(epochs + 1):
        loss, grads = gradients(model, x, y)
        if epoch % 100 == 0 or epoch == epochs:
            snapshots.append({"epoch": epoch, "loss": loss,
                              "accuracy": float(np.mean(forward(model, x)[-1].argmax(1) == y)),
                              "layers": [{"weights": w.tolist(), "bias": b.tolist()} for w, b in model]})
        if epoch == epochs:
            break
        step = epoch + 1
        for i in range(len(model)):
            for j in range(2):
                first[i][j] = .9 * first[i][j] + .1 * grads[i][j]
                second[i][j] = .999 * second[i][j] + .001 * grads[i][j] ** 2
                model[i][j] -= .015 * (first[i][j] / (1 - .9 ** step)) / (
                    np.sqrt(second[i][j] / (1 - .999 ** step)) + 1e-8)
    return model, snapshots


def load_validator(path=None):
    path = path or os.environ.get('WAI_OBSERVATION_VALIDATOR') or (Path(__file__).resolve().parents[2] / 'tools/recording/validate_observation.py')
    if not path or not Path(path).is_file():
        raise ValueError('Supply --validator or WAI_OBSERVATION_VALIDATOR pointing to mod-wai/tools/recording/validate_observation.py')
    spec = importlib.util.spec_from_file_location('wai_observation_validator', Path(path))
    if spec is None or spec.loader is None:
        raise ValueError('Unsupported observation-validator file')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if getattr(module, 'OBSERVATION_VALIDATION_API', None) != 1 or not callable(getattr(module, 'validate', None)):
        raise ValueError('Observation validation API v1 is required; update the mod-wai validator')
    return module.validate


def run(source, destination, validator=None):
    records, provenance = load_records(source)
    validate = load_validator(validator)
    validation = validate(records)
    samples = dataset(records)
    if len(samples) < 6:
        raise ValueError("Need at least six complete ability decisions")
    spells = sorted({r["spell"] for r in samples})
    x = np.stack([features(r["state"]) for r in samples])
    y = np.array([spells.index(r["spell"]) for r in samples])
    # A chronological diagnostic only, not an independent-run evaluation.
    split = max(1, min(len(samples) - 1, int(len(samples) * .7)))
    diagnostic, _ = train(x[:split], y[:split], len(spells))
    held = forward(diagnostic, x[split:])[-1].argmax(1)
    model, snapshots = train(x, y, len(spells))
    final_probs = forward(model, x)[-1]
    start = min(r["time_ms"] for r in records if r.get("kind") == "episode_start")
    examples = [{"seq": r["seq"], "seconds": round((r["time_ms"] - start) / 1000, 3),
                 "actual": int(y[i]), "predicted": int(final_probs[i].argmax()),
                 "features": x[i].tolist(), "target_actor": r.get("target", 0)}
                for i, r in enumerate(samples)]
    report = {"schema": 1, "mode": "offline_behavior_cloning", "architecture": [16, 12, 8, len(spells)],
              "parameters": sum(p.size for layer in model for p in layer), "features": FEATURES,
              "spells": spells, "actions": [SPELL_NAMES.get(s, f"Spell {s}") for s in spells],
              "provenance": provenance, "recording_validation": validation,
              "training_samples": len(samples), "training_accuracy": snapshots[-1]["accuracy"],
              "held_out": {"method": "chronological tail of same short episode; correlated samples",
                           "training_samples": split, "test_samples": len(samples) - split,
                           "correct": int(np.sum(held == y[split:])),
                           "accuracy": float(np.mean(held == y[split:])),
                           "unseen_training_spells": sorted(set(spells[v] for v in y[split:]) -
                                                           set(spells[v] for v in y[:split])),
                           "predictions": [{"actual": int(a), "predicted": int(p)}
                                           for a, p in zip(y[split:], held)]},
              "training_checkpoints": snapshots, "examples": examples,
              "limitations": ["No live control or simulated combat outcomes",
                              "Predicts spell choice only at recorded decision instants",
                              "No learned timing, movement, target selection, looting or bot commands",
                              "Training fit is memorization evidence, not dungeon competence",
                              "A separate completed run is required for generalization testing"]}
    dest = Path(destination)
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "experiment.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (dest / "model.json").write_text(json.dumps({"features": FEATURES, "spells": spells,
        "layers": snapshots[-1]["layers"], "architecture": report["architecture"]}, indent=2), encoding="utf-8")
    np.savez(dest / "weights.npz", **{f"layer_{i + 1}_{name}": parameter
             for i, layer in enumerate(model) for name, parameter in zip(("weights", "bias"), layer)})
    with (dest / "learning.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["epoch", "training_loss", "training_accuracy"])
        writer.writerows((s["epoch"], s["loss"], s["accuracy"]) for s in snapshots)
    values = forward(model, x)
    traces = [{"seq": sample["seq"], "seconds": examples[i]["seconds"],
               "actual": report["actions"][int(y[i])],
               "predicted": report["actions"][int(final_probs[i].argmax())],
               "activations": [v[i].tolist() for v in values]}
              for i, sample in enumerate(samples)]
    (dest / "activations.json").write_text(json.dumps(traces, indent=2), encoding="utf-8")
    summary = f"""# WAI first behavior-cloning experiment

Implemented and executed: a NumPy neural network trained on the recorded WC spell requests.
No game input, server build, live controller or simulated fight was run.

Architecture: {report['architecture']}; {report['parameters']} trainable parameters.
Inputs are 16 bounded pre-action state features. Hidden layers use ReLU;
the output uses softmax over the six demonstrated spells. Each dense layer
computes `activation = function(input @ weights + bias)`.

Training uses Adam and cross-entropy. All {len(samples)} spell requests, including
unsuccessful attempts, are demonstration labels. Model predictions do not prove
that the requested spell is legal, useful, or successful.

Full-demonstration fit: {report['training_accuracy']:.0%}.
Chronological diagnostic: train on {split}, test on {len(samples)-split};
{report['held_out']['correct']} correct ({report['held_out']['accuracy']:.0%}).
This is a correlated slice of the same run, not independent evaluation.
The tail includes a spell absent from training: {report['held_out']['unseen_training_spells']}.
The final saved model is trained on all examples; its fit is not the held-out result.

The source snapshot has an incomplete final JSON line, which was excluded explicitly.
Complete preceding records passed the existing recorder validator. No complete
dungeon ending or clean recorder shutdown is present in this snapshot.

Files: `model.json` contains all learned weights/biases and feature/action order;
`weights.npz` contains named weight tensors; `activations.json` contains each
input, hidden-neuron activation and output probability for all 15 decisions;
`learning.csv` contains loss/fit history; `experiment.json` includes training
checkpoints, provenance, validation and predictions.

The model currently chooses a spell at an already-known decision instant.
It does not choose when to act, which unit to target, how to move, how to loot,
or what command to issue to another bot. The recorded encounter's outcomes and
positions are not generated by this network. A live executor remains unimplemented.
"""
    (dest / "README.md").write_text(summary, encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("architecture", "parameters", "training_samples",
                                           "training_accuracy", "held_out")}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path)
    parser.add_argument("--output", type=Path, default=Path("output/wai-learning-demo"))
    parser.add_argument("--validator", type=Path, help="mod-wai observation validator (API v1); otherwise WAI_OBSERVATION_VALIDATOR")
    args = parser.parse_args()
    run(args.recording, args.output, args.validator)
