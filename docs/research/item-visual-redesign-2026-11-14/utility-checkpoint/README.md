# Actual-input utility acceptance

Four Creative/Survival and male-FP/female-front-TP runs retain their original
reports, requests, flight metadata and all 64 unmodified PNG frames. Normal GLFW
callbacks drive successful and refused shears/striker use, hotbar cancellation,
restoration and idle transitions. Checks cover finite identity, wear policy,
actual draw state and animation reset. `coverage.json` records exact counts.

The striker success scene temporarily declares one explicit QA combustible
material; every report verifies restoration. Bundled definitions otherwise omit
`physics.flammable`, defaulting to false. Choosing default-pack fire balance is
separate from this visual acceptance. The parser now honors explicit flags.

These are historical input-specific runs; the final native regression and exact
pixel renderer comparisons cover subsequent rendering optimizations. Full
requirement coverage is in [the final audit](../final-audit/README.md).
