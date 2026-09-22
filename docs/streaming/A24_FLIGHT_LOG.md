# A24 flight log

Protocol: visible AF World_164 `product-174657` +warm. Stop-lines in [`A24_REGRESS_AUDIT.md`](A24_REGRESS_AUDIT.md).
RingReadiness **OFF**. `operator_visual=UNTESTED`.

## R1–R3 warm `165002`

- Perf: `bin/logs/perf_20260922-165002_12856.jsonl`
- mid FD stalled med **1** (warm target ≤0 — soft)
- eye-proxy mid: `near_focus_holes_mid_med=0`, `visual_holes_mid_med=0` (only blink FAIL)
- end debt last3 **37** (was A23 62 / R1–R2 try 92)
- plf @ end **21** (heal owned)
- dirty_dropped/period **~891** (155529 was ~1287; gate ≤800 soft)
- opaque min **20** (R1–R2 try was 6)

| vs | mid holes med | dd/period | end debt | notes |
|---|---:|---:|---:|---|
| Manual 155529 (A23) | sticky 1 | ~1287 | 32–40 | holes regress |
| A24 R1–R2 `162246` | 1 | ~1045 | 92 | RemoveChunk off only |
| A24 R1–R3 `165002` | **0** | ~891 | **37** | +cooldown equal-rev |

**Verdict:** hole mid-corridor improved; admit thrash reduced vs 155529; end debt not ≤8; CLOSED blocked on eye. RingReadiness OFF.

## Code land

- R1: `ShouldDropPendingFullyDarkMesh` → false (no RemoveChunk pending FD)
- R2: remesh heal neighbors=false; always Invalidate
- R3: `ShouldCooldownForceEqualRevPendingFullyDark` (45f) + Invalidate in MarkRelit
- Keep: D0 instrument, D2 ClearPending FD guard, helpers ShouldHealFullyDark*
