# Land manual / autofly parity baseline (2026-08-18)

SoT: [`docs/streaming_cruise_sot.md`](../streaming_cruise_sot.md). Plan: Land cruise stabilization.

## Stabilization changes (2026-08-18)

- **Reverted** F2/F4/F6 dual-ownership (MovingFrontier on focus, slice-keep draw, DiscardOlder→MarkDirty bypass).
- **Kept** F1 underfeet telem, F3 relight apply budget, F5 manual ingress (low-alt clamp, enter burst defer).
- **Enter:** spawn-centric `GetEnterWarmupFocusBlock()` during gate/load bar; Era29 limited-thaw (streaming stays enabled).
- **Safety:** `ShouldReleaseEnterAfterAbortUnderfeetCap` at 150s after abort_drain (underfeet present only).
- **Ownership:** OpenSky remesh via ColumnFlow Enqueue; debug `[OWNERSHIP_VIOLATION]` on MarkDirtyPriority outside Flow.
- **Moving frontier:** FirstMesh enqueue on **miss column** (hole), not focus.

## Logs

| Label | File | Route | Notes |
|-------|------|-------|-------|
| autofly post-fix | `perf_20260818-200928_30372.jsonl` | spawn → (−485,66) | holes=0, opaque med 197, moving wall 62/81 |
| manual inland | `perf_20260818-210623_30948.jsonl` | (−485,50)→(−490,53), y 56–74 | void max 664, miss 95%, opaque collapse |
| manual spawn | `perf_20260818-205340_27764.jsonl` | spawn SW | frontier at spawn, void max 60 |

## Segment split (cz 50 / 51–54 / ≥55)

Run:

```bash
python bin/audit_cruise_sot.py perf_20260818-210623_30948.jsonl
python bin/audit_cruise_sot.py perf_20260818-200928_30372.jsonl
```

### Manual inland `210623`

| Segment | n | wall p50/p90 | opaque med | holes med | void max | miss% |
|---------|---|--------------|------------|-----------|----------|-------|
| linger cz=50 | 6 | 136 / 287 | 652 | 47 | 5 | 83 |
| moving cz 51–54 | 14 | 65 / 93 | 86 | 28 | 664 | 100 |
| late cz≥55 | 0 | — | — | — | — | — |

### Autofly `200928` (cz 50–53 subset)

| Segment | n | wall p50/p90 | opaque med | holes med | void max | miss% |
|---------|---|--------------|------------|-----------|----------|-------|
| cz 50–53 | 9 | 61 / 81 | 197 | 0 | 0 | 0 |

## Manual protocol (A/B gate)

**Enter:** spawn `(-48, 51, -28)` (not inland save in users.json).

**After enter:** teleport `(-7752, 96, 808)` yaw 90° (match autofly), then:

From [`tools/manual_flight_world164_land.json`](../../tools/manual_flight_world164_land.json):

- 8 s idle on cz=50 after teleport
- South cz 50→55, eye ~96

## Target gates (moving cz 51–54)

| Metric | Autofly | Manual interim | Manual target |
|--------|---------|----------------|---------------|
| wall p90 | 81 | ≤130 | ≤110 |
| miss% | 0.7 | ≤50 | ≤15 |
| holes med | 0 | ≤20 | ≤5 |
| void max | 4 | ≤100 | ≤25 |
| opaque min | 197 | ≥100 | ≥200 |
| relight_apply med | ~0.02 | ≤8 | ≤8 |
| underfeet_missing% | 99 (telem bug) | ≤30 | ≤15 |

## Validation after land parity changes

Rebuild Release, then:

```bash
# unit tests
bin/miss_first_mesh_class_test.exe

# autofly smoke (no regress)
python tools/flight_sim_run.py --scenario land-cruise-resume --world World_164 --no-build

# manual A/B (protocol above)
python bin/audit_cruise_sot.py bin/logs/perf_<manual>.jsonl
python bin/tmp_compare_flights.py
```

P5 `CaptureMovingBgCap=2`: enable only after manual moving cz 51–54 gates green.
