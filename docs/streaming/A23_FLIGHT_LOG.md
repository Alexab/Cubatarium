# A23 flight log (visible AF)

Protocol: `CUBA_FLIGHT_FOG_ON=1`, World_164, `product-174657` (+warm), **`--visible` required**.

No-regress vs A22 warm `142532` / S0 `135347`:
- mid FD stalled med ≤ **0** (warm) / not worse than S0 (~13 cold)
- end debt last3 ≤ **8** then → **0**
- `prefer_kick` not a gate; `pending_light_focus_n` >0 when FD>0 or Relight-in-flight
- fluid spike class not worse; eye-proxy holes/blink not worse than S0
- dirty_dropped focus-class not monotonically growing without enqueue on tail

RingReadiness stays **OFF**. CLOSED only after operator eye.

---

## No-regress table (final land)

| Run | mid FD stalled | end debt last3 | plf@end | vb_w/o plf sec | eye-proxy | notes |
|---|---:|---:|---:|---:|---|---|
| A22 S0 cold `135347` | ~13 | **8** | low | 52 | PASS | baseline |
| A22 warm `142532` | **0** | **45** | ~0 | — | blink FAIL | force_stale flood |
| A23 cold pre-D2 `145944` | 18 | **78** | **0** | 16 | PASS | PL erased while FD |
| A23 warm+D2 `151631` | **0**/1 | **43** | **14** | 0 | blink FAIL | heal owned |
| A23 D3 try `152133` | **0** | **98** | 45 | 0 | blink FAIL | **REGRESS — reverted** |
| A23 final warm `152522` | **0** | **62** | **18** | **0** | blink FAIL (holes 0) | land |

**Verdict:** mid-stall no-regress **met** (warm 0). PendingLight orphan on FD **fixed** (`vb_without_pending_light_focus_sec=0`). End debt ≤8 **not met** (62) — better honesty than A22 plf≡0, not CLOSED. D3 seam mirror/peer-invalidate **regressed** → deferred. RingReadiness **OFF**. `operator_visual=UNTESTED`.

---

## D0 instrument

- JobStage Admit stamps `desired_rev` / `source_rev` / `published_rev`
- Period JSONL `pending_light_focus_n` alias
- CaptureIncrementalTest: equal-rev hit + Invalidate miss

## D1 LightConverge bifurcate

- Rollback A22 `force_stale` on every PendingLight+FullyDark
- `ShouldHealFullyDarkWithRemesh` / `ShouldHealFullyDarkWithRelightOnly`
- Remesh: InvalidateMeshCapture + one Dirty if not already owned
- Void: NotePendingLight + Relight only

## D2 stop-tail

- `ClearPendingLightAfterMeshCommitted`: skip erase/Ready while any band FullyDark (second Ready site)
- LitDrawableCommit remains sole Ready with multi-Y sibling guard
- Dirty remesh skip when column already Dirty-owned

## D3 seam — DEFERRED after AF regress

- Shell light mirror + peer Invalidate → end debt 43→98, dirty_dropped thrash
- Reverted; ADR remains `A22_ADR_LIGHT_SEAM.md`

## D4 eye + honesty

- RingReadinessBudgetEnabled = **false**
- `operator_visual=UNTESTED`; `merge_green=false` until human west-cruise eye
- AF adequacy ≠ PASS visual
