# Sysreset v6 AF evidence (fog ON)

Date: 2026-09-21  
Commits: `f7b41580` geom-stale dark reject; `de956e11` FaceDebt SoftDefer+cap2  
SoT regress: `perf_20260921-145008_25236.jsonl`  
Gates: [SYSRESET_OPERATOR_AF_GATES.md](SYSRESET_OPERATOR_AF_GATES.md)  
**AF ≠ manual** — claim CLOSED only after operator eye on this build.

## AF suite

| Run | perf | scorecard |
|---|---|---|
| cold | `bin/logs/perf_20260921-151620_28476.jsonl` | `sysreset_v6_cold_score.json` |
| warm | `bin/logs/perf_20260921-151822_26068.jsonl` | `sysreset_v6_warm_score.json` |
| dive | `bin/logs/perf_20260921-152018_29624.jsonl` | `sysreset_v6_dive_score.json` |

## Gate matrix vs 145008

| Gate | 145008 | cold | warm | dive | Verdict |
|---|---|---|---|---|---|
| west | COVERED | COVERED | COVERED | COVERED | PASS |
| eye-proxy stale_visual mid ≤1 | 1 | **1** | **0** | **3** | cold/warm PASS; dive OPEN |
| dark_face_stale_near max ≪498 | **498** | **86** | **87** | **87** | PASS (Δ↓≥82%) |
| VB fly med ≤45 trend ↓ | 45 | 36.5 | 32 | 26.5 | trend ↓ (AF≠manual caution) |
| unfinished max ≤70 | 70 | 67 | **77** | **77** | cold PASS; warm/dive OPEN |
| kick max ≤20 | 1.7 | 6.8 | 7.0 | 4.1 | KEEP |
| prior_lit med ≤400 | 8 | 6 | 7 | 8 | KEEP |
| flip/dual | 0 | 0 | 0 | 0 | PASS |
| opaque_cull_skipped | 1 | 1 | 1 | 1 | hitch C KEEP |
| PreferKick | ≡0 | ≡0 | ≡0 | ≡0 | OPEN |
| merge_green | false | false | false | false | OPEN (manual eye) |

## Wins

- **Black block faces (v6 target):** `dark_face_stale_near` max 498→≤87; med 37.5→≤1.
- Eye-proxy stale_visual mid ≤1 on cold/warm; west COVERED all runs.
- VB fly med ↓ vs 145008; fully_dark stalled med ↓ (cold 9→2).
- hitch C / prior_lit / kick / flip KEEP.

## Still OPEN

- Dive eye-proxy stale_visual mid=3 (>1).
- unfinished max warm/dive 77 (>70).
- PreferKick ≡0; sky-through unfinished residual.
- AF adequacy `vb_too_low_for_product_class` — do **not** treat AF VB alone as CLOSED.
- Operator blacks+sky+dark faces: **manual re-flight required**.

## Operator note

Build: `f7b41580`+`de956e11`. Manual SoT path same as 145008 (World_164 west). Confirm black block faces gone; sky/chunk blacks still suspected OPEN.
