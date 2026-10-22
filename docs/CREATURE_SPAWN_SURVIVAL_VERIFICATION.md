# Creature spawning and Survival verification
Date: 2026-10-22
Branch: `codex/creature-spawn-survival` (based directly on `develop`)
Worktree: `C:/Users/Bakhshiev/.codex/worktrees/creature-spawn-survival/Cubatarium`

## Build environment

- CMake preset: `windows-msvc` / `windows-msvc-debug`
- Generator: Visual Studio 17 2022, x64
- vcpkg: `E:/vcpkg`, manifest mode, triplet `x64-windows-static`
- Installed dependencies: `build/desktop-msvc/vcpkg_installed` in this worktree
- Executable: `build/desktop-msvc/Debug/Cubatarium.exe`
- Runtime working directory: `bin`

The CMake cache points to this worktree, not the primary checkout. The Debug executable's static dependency check passed; it imports only seven Windows system DLLs.

## Automated verification

Command: `ctest --preset windows-msvc-debug`

Result: **56/56 passed**, exit code 0, 27.39 seconds.

Creature-related coverage includes:

- `creature_stats_test`: five difficulty serialization profiles, legacy/missing difficulty defaults, Hardcore completion metadata, hostile AI definitions, natural-spawn catalog weights/groups and habitat coverage.
- `mode_policy_test`: game mode × spawn source matrix, Peaceful/Hardcore rules, population caps, aquatic/amphibious/aerial/lava habitat gates, aerial-in-fluid rejection and safe load-position recovery.
- `creature_spawn_selection_test`: weighted habitat selection, exclusion of aerial creatures over liquid and lava-species selection at a lava point.
- `creature_activity_steering_test`, `influence_hit_math_test` and `player_clearance_test`: movement/combat/collision regressions.

## Headless runtime smoke runs

All runs used the Debug executable with `bin` as the working directory. No screen capture was used.

| Scenario | Result |
|---|---|
| Peaceful, forced AI ticks, 360 in-game frames | `exit_code=0`, `ingame_ok`; Survival/Peaceful; player alive; 52 saved natural creatures, including 2 pre-existing hostile creatures; no increase in hostile count. |
| Normal hostile AI, forced AI ticks, 360 in-game frames | `exit_code=0`, `ingame_ok`; Survival/Normal; player died during the run; 5 hostile creatures; 25 natural creatures saved. |
| Reload completed Hardcore world, 60 in-game frames | `exit_code=0`, `ingame_ok`; Survival/Hardcore; `player_dead=1`, `hardcore_dead=1`; 5 natural creatures saved. |

Current saved fixture counts agree with those reports: `CodexPeacefulSmoke` has 52/52 natural creatures, `CodexHostileAiSmoke` 25/25 and `CodexHardcoreSmoke` 5/5. The Hardcore fixture still stores `hardcore_dead=true`.

## Manual in-game verification

**Not performed.** The user is currently unable to run the manual check. Creative palette/command interaction, the representative habitat walkthrough and the visible death-screen controls remain unverified interactively. The headless smoke runs and automated policy tests do not replace those visual/input checks.
