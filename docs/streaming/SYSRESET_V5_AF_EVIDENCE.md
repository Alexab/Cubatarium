# Sysreset v5 AF evidence (fog ON)

Date: 2026-09-21  
Commits: `e305f72f` focus admit+FD ForceDirty; `a168fef7` FaceDebt already-known remesh  
SoT regress: `perf_20260921-112357_29496.jsonl`  
Gates: [SYSRESET_OPERATOR_AF_GATES.md](SYSRESET_OPERATOR_AF_GATES.md)

## AF suite

| Run | perf | scorecard |
|---|---|---|
| cold | `bin/logs/perf_20260921-131206_21928.jsonl` | `sysreset_v5_cold_score.json` |
| warm | `bin/logs/perf_20260921-131404_24492.jsonl` | `sysreset_v5_warm_score.json` |
| dive | `bin/logs/perf_20260921-131604_11948.jsonl` | `sysreset_v5_dive_score.json` |

## Gate matrix vs 112357

| Gate | 112357 | cold | warm | dive | Verdict |
|---|---|---|---|---|---|
| west | COVERED | COVERED | COVERED | COVERED | PASS |
| VB stalled fly med ≤7.5 | 12.5 | **0** | **1.5** | **2** | PASS |
| VB fly med ≤44.5 | 44.5 | **22** | **26** | **16** | PASS (≤35 too) |
| unfinished max ≪79 | 79 | 71 | 77 | 70 | OPEN (↓ slight) |
| PreferKick >0 | ≡0 | ≡0 | ≡0 | ≡0 | OPEN (ForceDirty path live via Dirty↑) |
| dirty_fm | 57 | 133 | 90 | 135 | expected↑ (focus bypass); kick KEEP |
| prior_lit / kick / emerge | KEEP | KEEP | KEEP | KEEP | PASS |
| flip/dual | 0 | 0 | 0 | 0 | PASS |
| opaque_cull_skipped | 1 | 1 | 1 | 1 | hitch C KEEP |
| eye-proxy | PASS | PASS | PASS | PASS | PASS |
| merge_green | — | false | false | false | OPEN (operator + unfinished) |

## Wins

- Black FullyDark drain: VB stalled fly 12.5→≤2; VB fly 44.5→16–26.
- Focus admit bypass restores MarkRelit Dirty in ring without PreferKick chicken-egg.
- Hitch C / kick / emerge / prior_lit / eye-proxy KEEP.

## Still OPEN

- PreferKick N still ≡0 (pending window rare); ForceDirty-no-pending is the live escape.
- unfinished med/max still high (sky FaceDebt census residual; operator eye needed).
- dirty_fm↑ vs 112357 — monitor for kick blow-up (none observed, kick max≤8).
- merge_green blocked until operator sky/blacks CLOSED + unfinished↓.

## Operator note

Blacks: AF strongly improved vs 112357. Sky-through: unfinished still elevated — confirm with manual eye after this build.
