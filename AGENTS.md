# Repository working instructions

- Build and run the Windows desktop application with the `Release` configuration only. Do not build or launch `Debug`; a prior Debug startup hung, and Release is the required configuration for desktop verification.
- For desktop builds, use `cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8`.
- Run `tools/flight_sim_run.py` against the Release executable in `bin/Cubatarium.exe` for world loading and visual checks.
