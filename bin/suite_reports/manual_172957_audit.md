# Manual 172957 (R7.1) vs 131116 / 153045

Commit: `a99d0201` fix(stream): Phase 5.7R7.1 restore SoftDefer leave-in after wall diet  
Perf: `bin/logs/perf_20260909-172957_8616.jsonl`  
Scorecard: `bin/suite_reports/manual_172957_scorecard.txt`

## Verdict

R7.1 **починил regress 153045** (SoftDefer stuck, schedule_ok, eye holes vs R7-bad).  
Vs 131116 KEEP: FPS чуть выше, SoftDefer почти 0, schedule_ok восстановлен; holes cruise ещё чуть выше KEEP; **рваность FPS хуже KEEP**.

| Metric (cruise spd>2) | 172957 R7.1 | 131116 R6 | 153045 R7-bad |
|---|---:|---:|---:|
| wall med / p90 | 139 / 188 | 143 / 195 | 127 / 177 |
| fps med / p10 | 7.17 / 5.33 | 7.00 / 5.13 | 7.88 / 5.65 |
| holes_frac | 0.54 | 0.43 | **0.63** |
| SoftDefer stuck | 0.5 | **0** | **2** |
| schedule_ok | 3.5 | 4 | **1** |
| dirty_fm | 8 | 9.5 | 5 |
| cull | 15.8 | 20.2 | 12.6 |
| stream / emerge / scene | 30 / 51 / 43 | 28 / 59 / 43 | 31 / 49 / 35 |
| column_meshing_n | **94.5** | 80 | 88 |
| fps \|Δ\| med / p90 | **2.28 / 4.90** | 1.30 / 2.76 | 2.00 / 5.27 |
| spike wall>160 | 25% | 27% | 22% |

ALL periods: fps med **7.67** vs 131116 **6.91**; SoftDefer **0**; holes_frac **0.44**.

## Плюсы (согласуется с eye)

- SoftDefer stuck почти снят (2 → 0.5/0)
- schedule_ok / dirty_fm вернулись к KEEP-классу
- holes лучше 153045; ALL holes ~0.44 (near KEEP)
- cull дешевле KEEP (−4 ms) благодаря R7 Cut A
- emerge чуть легче KEEP (−8 ms)

## Минусы

### 1) Streaming не всегда успевает
- `column_meshing_n` cruise **94.5** vs 80 — больше колонок в полёте, чем ring успевает lit/drawable
- `stream_ms` +2 vs KEEP; `discard_late` abs **251** vs 50 — churn mesh без lit-finish
- `focus_missing=1` и `miss_horiz=2` остаются product FAIL
- Это не «сломали SoftDefer», а **throughput/pacing**: генерация/меш впереди lit-completion

Без смены пайплайна (KEEP SoftDefer/FM/hide-until-lit): дешеветь hot path (emerge prep ~51, scene ~43, opaque cull/transp) и снизить spike-кадры, чтобы ring drain чаще успевал за spd~5.8.

### 2) Рваный FPS → управление
- Period-to-period `|Δfps|` med **2.28** vs KEEP **1.30** (~1.8×)
- p90 |Δfps| **4.9** vs **2.8**
- wall p90~188 / p95~216 — каждый 4–5-й cruise period >160 ms
- CV wall ≈0.27 как у KEEP, но **перепады между периодами** сильнее → ощущение рывков при WASD/look

## Куда дальше (algorithm diet, не pipeline)

1. **Spike owners** на 172957: emerge prep + scene (cull/transp/draw) — точечно дешевле без shed-under-miss / SoftDefer RemoveAt
2. **Frame pacing**: снизить долю wall>160 (сейчас 25% cruise) — это главный рычаг «рваного» управления
3. **Stream catch-up без пайплайн-ломки**: больше CPU на FM/lit при уже стабильном SoftDefer leave-in — не AbortDrip↑ / PreferKick-empty
4. Не возвращать SoftDefer outer RemoveAt и не ослаблять force DropRemesh под nh≤2
