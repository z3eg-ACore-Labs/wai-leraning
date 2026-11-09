# Character control and telemetry

## Configuration

| Setting | Default | Purpose |
| --- | --- | --- |
| `Wai.Action.Enable` | 0 | Enable explicit server-action experiment for the human druid |
| `Wai.Action.Map` | 43 | Restrict that experiment to Wailing Caverns |
| `Wai.Action.AllowRouteReplay` | 0 | Permit bounded recorded route steps; route replay is not learned navigation |
| `Wai.Action.Client.Enable` | 0 | Enable explicit client-observation leases and passive human telemetry |
| `Wai.Action.Client.Map` | 389 | Restrict explicit client-observation lease to the configured map |
| `Wai.Action.BotModeCommands` | 1 | Enable bot observation/action-unavailable replies; startup-only |

Normal config reload releases active character control; it does not start a helper or grant a new lease.
For bot-mode settings restart after changing configuration. Controller/model processes are manually
configured/launched outside worldserver. Use native authenticated administrator SOAP on loopback and
private credential settings, never passwords in commands/source. SOAP authentication is supplied by the
core; these character protocols are separate from the voice gateway's service-account allowlist.

## Player and bot commands

| Command | Use and effect |
| --- | --- |
| `.wai start` | Eligible logged-in druid explicitly grants the configured server-action experiment lease |
| `.wai stop` | Owning character releases its lease |
| `.wai status` | Report whether the lease is fresh |
| `.wai client start` | Eligible logged-in character grants an observation-only lease for real client input |
| `.wai client stop` | Owning character releases the client lease (`.wai stop` also releases it) |
| `.wai client status` | Report lease freshness |
| Bot whisper/party `mode` or `mode ?` | Report native observation mode; no trainer/recording is connected to that bot |
| Bot `mode observation` | Return the same observation status; it does not start recording/training |
| Bot `mode action` | Explicitly reject unavailable external **bot** handoff; native AI continues |

Example: after enabling client telemetry, the character types `.wai client start`, then its separately
started controller polls observations. The module never drives server movement in client-observation
mode. Disable native SelfBot/client AI on the controlled character first; attached AI rejects the lease.
The druid server-action experiment and bot `mode action` are different mechanisms. No voice command
automatically grants a `.wai` character lease.

The optional WAI command-discovery API v1 advertises registered bot-mode names to compatible voice
consumers. It contains no gameplay handler or QoL state. Native bot authorization and WAI replies still
enforce the request. See [protocols](PROTOCOLS.md) and [validation](VALIDATION.md).

Historical operating examples/build reports are retained in [legacy history](LEGACY-HISTORY.md).
They refer to the previous workspace layout, not deployment of this extracted repository. A successful
offline fixture or changed model weight is not evidence of learned combat or successful dungeon clearing.
