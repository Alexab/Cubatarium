# RingReadinessBudget notes (A21 P7)

- `EvaluateRingReadinessBudget` is pure; outputs include hysteresis fields.
- Feature flag `RingReadinessBudgetEnabled()` defaults **OFF**.
- One read site: `EffectiveLitRingOrBaseline` in column SoT classification
  (`World.cpp`). Enabling the flag without a tick that updates
  `RingReadinessLastOutputs` keeps baseline 4 until Evaluate is wired to a
  frame census (follow-on).
- **Fog ≠ hole-close:** `fog_pull_hint` may soften quality transitions only.
  It must not be treated as proof that a missing/wrong material is acceptable
  inside the tested visible area.
