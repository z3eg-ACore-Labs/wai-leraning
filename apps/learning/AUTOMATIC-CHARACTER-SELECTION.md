# Foreground client character discovery

October 28, 2026: design recorded before implementation. The user requests the
human character active in the client at dungeon entry, without a fixed name/GUID.

Use a small addon handshake, not online-player order, window titles, saved account
names, process memory or injected commands. The addon self-whispers a temporary
random marker and actual movement bindings. The server associates registration
with the authenticated sending session and expires it after three seconds. An
authenticated discovery request returns fresh human registrations only; bots and
native SelfBot are excluded. No registration grants an action lease.

The addon renders a small black/white marker in a fixed corner of its own client.
The helper reads only those marker pixels from the foreground WoW client and
matches the marker to exactly one fresh server registration. No screenshots are
saved, no arbitrary screen text is read, and no input is generated. Missing marker,
stale registration, unsupported bindings or duplicate matches means no capture.
Bindings come from the live addon rather than an assumed character profile.

At dungeon entry, bind the run to that client process, marker and server character.
Focus loss pauses samples without adopting another character mid-run. Logout or
a different character/marker in the same client finalizes the old run as incomplete
before another character can be selected. New selection occurs only between runs.
Existing authoritative completion, dungeon-store isolation and behavior-cloning/
human-only hybrid maintenance remain unchanged; reinforcement learning is explicit.

The addon is staged as ordinary Lua/TOC files; activating it requires addon reload
or the next normal login. Server registration/discovery requires a later requested
build/restart. Source tests must cover marker corruption, ambiguity, stale sessions,
foreground/character changes and actual server session binding. Production model
initialization, runtime launches, a build and input are outside this implementation.
