# A24 — A23 regress audit (manual 155529)

Date: 2026-09-22  
HEAD base: `a7acc856` (A23 LightConverge)  
Evidence: [`bin/logs/perf_20260922-155529_8064.jsonl`](../../bin/logs/perf_20260922-155529_8064.jsonl)

## Verdict

A23 closed mid FD stalled and PendingLight orphan, but **regressed focus holes** and **DirtyAdmit thrash**. Land is not a win. End-debt ≤8 was never met.

## Comparison

| Metric | Manual 133440 | A22 warm 142532 | Manual 155529 |
|---|---:|---:|---:|
| near_focus_holes periods>0 | 0 | 8 | **12/20** |
| dirty_dropped / period | ~790 | ~372 | **~1287** |
| opaque_cmd_on min | 43 | 15 | **22** |
| mid FD stalled | ~18 | 0 | 0 |
| plf @ tail | 0 | 0 | 6–17 |
| end debt | 35–76 | 45 | 32–40 |

Root chain: D2 keeps PL while FullyDark → RecoverUnlit `RemoveChunk` pending FD more often → holes; seamed remesh heal + admit drops → thrash; A22 force_stale removed without equal-rev replacement.

## Stop-lines (A24 AF / manual class)

Until holes are stable, **end debt is not a gate**.

1. `near_focus_holes` periods with value >0 on west corridor == **0** (same class as manual 133440).
2. `dirty_dropped / period` ≤ **~800** (not worse than 133440).
3. warm mid FD stalled med ≤ **0**.
4. `vb_without_pending_light_focus_sec` must not regress to A22 orphan class without documented reason.
5. RingReadiness stays **OFF**; CLOSED only after operator eye.

## Remediation map

- R1: disable RemoveChunk on pending FullyDark — **landed**
- R2: remesh heal `neighbors=false` + always Invalidate — **landed**
- R3: narrow equal-rev cooldown — **landed** (debt 92→37 on warm AF)
- R4: mark A23 REGRESSED; honesty docs — see A23_FLIGHT_LOG + A24_FLIGHT_LOG

## Post-fix AF (warm `165002`)

- near_focus_holes_mid_med **0** (eye-proxy holes gate)
- dirty_dropped/period ~891 (improved vs 1287; soft vs ≤800)
- end debt 37; mid FD stalled med 1; operator_visual UNTESTED
