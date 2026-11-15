# Silent automated launches

User-requested test policy: all automated game audio stays off during the test.
`flight_sim_run.py` now wraps each actual process launch in `muted_test_audio`.
The temporary game configuration sets `audio.enabled=false` and master, music,
ambient, sfx and UI volumes to zero before the process starts. Audio settings
are restored after normal exit, timeout or an error. Help/argument parsing and
build-only preparation do not write the game configuration.

The helper restores the original audio section while preserving unrelated
settings saved by a direct flight run. If nothing else changed, it restores the
original file bytes, including a UTF-8 BOM. Missing/deleted configurations and
corrupt game writes have explicit restoration paths. Outer item QA tools retain
their full world/config snapshot restoration as well. Their direct QA-world
creation launches use the same audio guard.

`observed-active-test.json` records the actual `bin/config.json` audio section
while a native test process was running. Flight reports also record
`runtime_resources.audio_disabled_for_test=true`; logs state the mute policy.
The regular player configuration is restored between tests rather than being
permanently muted.

Seven tests pass: all buses off and exact-byte exception restoration, preservation
of unrelated saved settings, absent audio section, initially missing config,
corrupt game write, deleted game config and invalid original config. The item
acceptance runner and Windows CI run these checks.
