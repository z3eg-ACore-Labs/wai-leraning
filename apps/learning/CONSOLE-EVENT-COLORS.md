# Green WAI event messages

Human-facing episode messages use `server.wai.events`: run termination/completion, boss-kill
headlines, observer teleport instructions, pool preparation, learner startup and training summaries
or failures. Structured `WAI_EPISODE` JSON stays under `server.wai.episodes` with its existing console
configuration. Saved JSONL and file-log text contain no color escape sequences.

The normal build publisher installs a dedicated bright-green console appender into the shared
worldserver config at publication time. Existing inherited file appenders remain attached, while
other console appenders are omitted from the new event route to prevent duplicate messages. Other
server categories and the JSON console route retain their current settings. A pre-change config
snapshot is saved before the edit; repeated publications are idempotent.

The generated settings resemble this example (file-appender names depend on the active config):

```ini
Appender.WaiEventsConsole = 1,6,0,"10 10 10 10 10 10"
Logger.server.wai.events = 4,WaiEventsConsole Server
```

The six colors cover fatal/error/warn/info/debug/trace for this appender only. This honors the
requested green color for all human-facing WAI episode events without changing severity or
injecting colors into telemetry. The native Windows console coloring is reused.

Source and publisher changes require the next normal server build/publication and manual restart.
No existing running server or deployed config was changed during implementation. C++ syntax/style
checks and nine offline publisher tests passed; the tests use temporary fixtures and never start
the server, compile worldserver, or publish into the game directory. Live appearance is unverified.
