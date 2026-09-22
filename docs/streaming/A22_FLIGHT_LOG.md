# A22 flight log (visible AF)

Protocol: `CUBA_FLIGHT_FOG_ON=1`, World_164, `product-174657` (+warm/dive), **`--visible` required**.

---

## Baseline S0 (pre-S1 code)

- Date: 2026-09-22; SHA: `c204a05d` (+ uncommitted flight_sim `--visible` os fix)
- Perf: `bin/logs/perf_20260922-135347_34376.jsonl`
- Scenario: cold product-174657 **visible=yes**
- Metrics: VB fly med **25.5**; mid FDstalled **13**; dual-lane FAIL (stalled); eye-proxy PASS; input adequacy PASS
- Soft: `vb_without_pending_light_focus_sec=52` (H-Kick/H-Light confirmed)
- Spikes: emerge 28, stream 11, fluid_map 11
- end_gate: FAIL debt last3 **8/8/8** (prefer_kick=0, admit=4); `a22_s0_visible_cold_end_gate.json`
- Verdict: baseline reproduces pending_light starvation while VB present; AF VB lower than manual 133440 (~54) but same class of heal failure.
- **Plan delta:** H-Light/H-Kick confirmed on visible AF. Proceed S1 (PendingLight keep + force_stale remesh; no MarkDirty-before-relight).

---

## Tooling fix (prerequisite)

- `tools/flight_sim_run.py`: removed nested `import os` under `if not args.visible` that caused `UnboundLocalError` when `--visible` (blocked A22 Loop).
