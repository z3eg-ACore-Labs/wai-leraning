# WAI backlog

October 25: automatic cloned-party navigation/PPO source is drafted in `rl_loop.py` and the episode
module. See `RL-EPISODE-OPERATIONS.md`. On-demand process launch, transitions, checkpoint update,
automatic repeat and compact feedback passed offline validation (18 learner/client tests plus C++
syntax/native helpers). Full server build and live integration are pending; no build/server was started.

## Next: reuse existing neural-network monitoring tools

User preference: use established tools instead of spending compute and time
building a custom neural-network visualizer. This item is planned, not installed
or connected to the running game.

Main-stack decision: use PyTorch for model definitions, autograd, optimizers,
datasets and checkpoints. Keep the already executed NumPy model as a small
diagnostic baseline, not a new framework to maintain. Prioritize workflows the
user can transfer to other machine-learning projects.

Use standard PyTorch state_dict checkpoints plus feature/action schema and
configuration, deterministic seeds, explicit train/validation/test splits by
complete run, and recorded dataset provenance. Use pytest for the main-stack
tests. ONNX is an inspection/export artifact, not the training checkpoint.
Do not introduce a cloud experiment service or custom dashboard without a
concrete requirement that TensorBoard/Netron cannot satisfy.

- TensorBoard: training/validation loss, per-action accuracy, gradient norms,
  weight distributions, hidden activation distributions and dead-ReLU rates.
  Record training and validation separately; never label training fit as policy
  success. Bound activation sampling rather than recording every neuron every tick.
  https://www.tensorflow.org/tensorboard/get_started
  https://docs.pytorch.org/docs/stable/tensorboard
- Netron: inspect the model graph, layer shapes and learned tensors. Export a
  graph format such as ONNX when connecting the training framework. The current
  prototype already saves named NumPy tensors, which Netron supports inspecting;
  that tensor archive does not encode the complete computation graph.
  https://github.com/lutzroeder/netron
- Per-decision inspection: small framework hooks capture chosen examples' inputs,
  activations and action probabilities. TensorBoard can show distributions, but
  Netron is not a live learning monitor. Keep this integration small rather than
  writing a new animated neuron UI. The prototype currently exports JSON traces.
- Operate locally, without requiring cloud experiment accounts or sending private
  recordings elsewhere. Installation and setup remain backlog work.

## Subsequent controller work

1. Complete recording: loot requests/results, structured bot instructions,
   creature template IDs, movement intent and selected-enemy context.
2. Independent completed runs for evaluation; retain failures as separately marked
   attempted actions rather than treating them as successful training targets.
3. Shadow inference using fresh server observations, with no control authority.
4. Opt-in game executor: resolve targets, validate range/resource/cooldown,
   hand off ownership explicitly, expire stale decisions, and return results.
   This requires server implementation and a user-requested build before live use.
5. Full party policy: shared entity encoder -> temporal state (e.g. GRU) ->
   separate intent, recipient, target, movement and timing heads -> class executors.
   Sizes, weights and performance claims for this larger policy are not trained yet.

## First action-mode implementation

Implemented source: standalone mod-wai-action grants the logged-in druid an
explicit `.wai start` lease, and the PyTorch `live.py` client consumes fresh SOAP
observations and requests legal spells. Native target/movement/autoattack/loot
helpers and optional recorded-route replay are labelled separately from learned
spell decisions. No full dungeon navigation or full-party neural policy exists.
The server module has not been built or tested in game. PyTorch client inference
requires the task-local dependency setup; no controller was launched here.
