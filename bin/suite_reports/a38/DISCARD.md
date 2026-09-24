# A38 R0 — discarded / invalid evidence

Do **not** use these as Gate 8 / merge_green baseline:

- Debug `ExeDir` AF → `post_create` / Creating world (no `World_164` under Debug/worlds)
- Contaminated hang runs that reuse stale perf periods
- Dirty-tree AF without clean SHA (unless explicitly labeled experiment)
- Historical A29/A30 scorecards (different SHA / dirty_diff)

Valid AF: Release `bin/Cubatarium.exe` only, load `World_164` (`post_load_*`), clean SHA preferred.
