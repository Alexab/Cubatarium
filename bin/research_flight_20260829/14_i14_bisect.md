# I14 bisect — пролёт `204611` vs I13-C `194104`

**База:** `7c943fea` (I13-C)  
**Full I14:** uncommitted stash `i14-full-wip` → пролёт `perf_20260830-204611_35328.jsonl`  
**Метод:** телеметрическая атрибуция + code review (ручной short-fly опционален)

## Матрица фаз

| Точка | Фазы | Ожидание `stream_ms` | Подтверждение |
|-------|------|----------------------|---------------|
| P0 | I13-C | **83** | `194104` ✓ |
| P1 | +A diet hysteresis | 90–120 | гипотеза |
| P2 | +A+B ingress hold | **200+** | `204611` ≈ full ✓ |
| P3 | +A+B+C churn damp | ≤P2 | C снижает churn, не wall |
| P4 | +E speed clamp | ≤P3 | E не должен ↑ stream/frame |
| P5 | +F stop VB | stop↑ fly≈P4 | F влияет на stop |

## Атрибуция по метрикам `204611`

| Симптом | Δ vs `194104` | Вероятная фаза | Механизм |
|---------|---------------|----------------|----------|
| `stream_ms` +141% | 83→219 | **A+B** | prep untagged gap 31→89ms |
| `witness_pin_age` 426→**14** | | **B** | hold+rate-limit → retarget thrash |
| `visible_black` 34→**71** | | **B→spiral** | schedule_ok≈1, VB scan тяжелее |
| `cruise_scan_fast` off | VB>20 | **B spiral** | darkface/facing path не diet-fast |
| `effective_holes_blink` 0.01→**0.11** | | **B** | witness hop + hole oscillation |
| `emerge_spike` 13%→7% | | **B** | меньше emerge-spike, больше hole-blink |
| `chunk_not_ready` 6→1 | | **A?** | ring reuse TTL 16f — ложное «готово» |
| `heal_on_hot` 2s→**90s** | | **B** | FM/witness starvation |
| `opaque_churn` 450→362 | | **C** | единственный явный выигрыш C |

## Вердикт bisect

1. **Главный виновник: I14-B** (ingress drawable hold + rate-limit 8f)  
   - `witness_pin_age_med=14` — пин не держится, система «дёргается»  
   - `capture_retarget_blocked` не спасает: blocked и would-retarget кадры копят debt  
   - FM starvation → VB spiral → `cruise_scan_fast` всегда false при VB=71  

2. **Усилитель: I14-A** (hysteresis + ring TTL 16f)  
   - Diet формально ON (`facing=0`), но **stale ring** скрывает рост VB/pressure  
   - `untagged_gap` +58ms — работа вне sub-timers (VB full scan, dirty/unfinished resync)  

3. **I14-C/E/F** — не объясняют fly-regression; C даёт −20% opaque_churn; F бьёт stop (`stop_stream` 206ms)

**Короткий пролёт для подтверждения (опционально):**

```powershell
git stash pop  # full I14
# откатить только B в AntiFlickerPolicy.h + WorldStreaming.cpp capture block
# → ожидание: stream_ms ~100-120, pin_age ↑, VB ↓

git checkout 7c943fea -- src/  # P0
# cherry-pick / apply только patch A
# → ожидание: stream ≤110, без pin_age=14
```

## Решение

**Откатить весь I14 code** к `7c943fea`. Stash `i14-full-wip` сохранён.  
Следующая итерация: **I14b** — см. `.cursor/plans/i14b_vb_spiral.plan.md`
