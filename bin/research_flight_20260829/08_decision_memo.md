# 08 — Decision memo (flight perf)

## FP0 outcome

- Baseline 081522 analyzed; ghost Dirty CLOSED (`skip_orphan=0`).
- Primary: witness retarget (med 12) + FM starvation (`schedule_ok` med 1).
- FP gates added to `flight_sim_phase_gate.py`.

## Next

Implement FP1–FP5 per `07_roadmap.md`; verify with no-teleport autofly after each phase.
