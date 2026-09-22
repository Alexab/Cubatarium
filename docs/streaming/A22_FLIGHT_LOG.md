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

## S1 visible (PendingLight keep + force_stale)

### S1.1 no-MarkDirty before relight — REGRESSED
- Warm `141239`: end debt **33** (S0 was 8); prefer_kick still 0.
- **Plan delta:** H-Light partial — removing MarkDirty hurt drain. Restore MarkDirty+sky (iter2).

### S1.2 MarkDirty restored + keep pending + force_stale
- Cold `141650` visible: adequacy PASS; mid FD stalled med **1** (score mid_corridor); dual-lane mid stalled 21 FAIL; end debt **60**; prefer_kick=0; dirty_dropped grew without enqueue.
- Fluid still dominant spikes (15).
- **Plan delta:** PendingLight keep alone insufficient; escalate to S2 sole demand writer + S5 fluid. Max 2 S1 iters used.

---
