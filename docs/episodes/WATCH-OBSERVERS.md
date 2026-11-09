# Watch concurrent WAI parties

## Start

After rebuilding and restarting the published server, wait until each requested party has its
`start` event. In the **server console**:

```text
waiepisode start 4
waiepisode watch
```

Plain `watch` opens one observer client per ready, running party (up to four).
Inactive/unready slots are logged and skipped without blocking the others. A client/login/join
failure is reported for that observer while the helper continues with the remaining slots.
Party 1 is top-left, 2 top-right, 3 bottom-left, 4 bottom-right on the primary monitor.
`waiepisode watch 2` still prints party 2's manual observation instructions.
The trainer's `observe` commands are unchanged.

The watcher creates the requested level-80 undead mage observer characters on the existing
GM accounts on first use. Those accounts must already have GM security and be empty/offline.
Later uses accept only the matching sole character; conflicts are reported without deleting,
renaming, resetting or replacing existing characters. Character saves are committed and checked
before launching clients. The normal realm character count is refreshed.

## First-use login setup

A small Windows dialog asks for the shared **observer account** password. It is stored using
Windows DPAPI for the current Windows user in `wai-watch-login.xml`. This is distinct from the
admin SOAP credentials; passwords are never command-line arguments or server messages.

The first client also asks for login field calibration: point at its account-name field and
press **F8**, then point at the password field and press **F9**. Do not type the password into
chat or another window. The field positions are saved beside the encrypted login file.
Subsequent launches reuse them. The helper checks focus before every typed character and
stops on login/realm-selection/time-out errors instead of repeatedly entering credentials.
Login occurs at a consistent window size, then successful clients are tiled.

A realm-selection dialog, custom login interface, changed UI layout or slow startup may require
manual attention. Delete only the `.fields.json` calibration file to recalibrate; deleting the
encrypted login file prompts for the observer password again. These files are private and
must not be committed. Existing online observers are reused without logging them out.

## Instance entry

The helper polls the authenticated loopback SOAP endpoint and requests `waiobserver join N`
only after that observer is online. This enables GM mode and invisibility, clears **only that
observer's RFC binding**, binds to the current tank's instance and performs a forced instance
transfer when needed. It then waits for confirmation that observer and tank share the same map
object, GM mode, invisibility and matching phase. The observer remains ungrouped and does not take control of any bot.

Normal five-player group membership is preserved. This is observation, not a guarantee that all
encounter scripts ignore GM presence. The helper does not continuously move the camera or
follow a newly restarted party: issue `waiepisode watch` again to rejoin current parties.

## Deployment and settings

The build publisher installs the canonical `tools/Watch-Wai.ps1` into the release home and
fills empty watch executable/client settings using the locally saved installation defaults.
Explicit administrator overrides are retained and existing configuration is snapshotted.

`Wai.Episodes.Watch.Executable` is PowerShell 7; `Watch.Script` is the published script;
`Watch.Client` is the game executable; `Watch.LoginFile` is the private DPAPI file.
SOAP uses the existing `Wai.Episodes.Learner.Credentials` and `.Url` settings.
Nothing launches on ordinary server startup. Launch requests require administrator access;
GM characters may use only their own observer slot's join/status command.

## Validation status

Source syntax/style, PowerShell parsing/native input layout, and publisher fixture tests pass.
The offline actual-loop regression confirms a failed observer does not block later selected
slots, retaining their original party numbers/quadrants. No client or SOAP endpoint is used
in that test. Already-online observers are reused; their existing windows are not retiled.
No full server build, account mutation, client launch or live login/teleport test was performed
by the implementation agent. Character creation happens when the rebuilt server receives watch.
The first live launch must verify login calibration, four clients, quadrant order and matching
instance IDs before calling the complete UI flow verified.
