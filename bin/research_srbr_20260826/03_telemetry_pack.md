# SRBR 20260826 — Telemetry Pack

**Streaming + Render Bottleneck Research · пакет сравнения телеметрии**

| Поле | Значение |
| --- | --- |
| Дата | 2026-08-26 |
| HEAD (FZ27-P17) | `1f3f7675` |
| Baseline eye (P13) | `9f502dd5` |
| Режим | SoT manual logs + существующие audit-скрипты |
| Язык | русский |

## SoT логи

| ID | Путь | Роль | spikes |
| --- | --- | --- | ---: |
| **P13** eye-best | `bin/logs/perf_20260825-170807_29680.jsonl` | лучший глазной baseline | 1418 |
| **P17 bad** | `bin/logs/perf_20260826-100413_20156.jsonl` | discard storm + VB sticky | 849 |
| **SoftDefer KEEP** | `bin/logs/perf_20260826-104722_26064.jsonl` | SoftDefer PASS после P17 | 431 |
| **CUR** (current) | `bin/logs/perf_20260826-112418_14164.jsonl` | long stand never-heal | 5323 |

## Методы / скрипты

```text
python bin\tmp_audit_100413_stand.py bin\logs\perf_20260826-112418_14164.jsonl bin\logs\perf_20260826-100413_20156.jsonl
python bin\tmp_audit_ne_frontier_stand.py bin\logs\perf_20260826-112418_14164.jsonl bin\logs\perf_20260826-104722_26064.jsonl
python bin\tmp_audit_112418.py
python bin\tmp_audit_112418_stand.py
python bin\research_srbr_20260826\scripts\segment_medians.py
```

Сырые выводы: `bin/research_srbr_20260826/raw/`.

Сегменты:
- **ENTER** — первые 80 spike
- **MOVING** — кадры со сменой `(player_x, player_z)`
- **STAND** — trailing land stand `keep_cols>=160` (без смены позиции)
- **LATE** — последние 40 spike STAND

Медианы и `/spike` rates — как в audit-скриптах.

---

## Вердикт (кратко)

| Gate | CUR (112418) | SoftDefer (104722) |
| --- | --- | --- |
| SoftDefer KEEP (floor≤8, SiteB≤5, keep≥160, capture≥3, PL drop≈0) | **PASS** | **PASS** |
| P17 stand vs 100413 (discard/VB/unf/stream) | **FAIL** | — (не цель gate) |

**Суть:** discard storm P17 погашен (discard/spike ~0.002 vs 0.465), SoftDefer KEEP держится. Но long-stand **не лечит** visual hole: `unfinished_visual`~70, `visible_black_focus`~107, `stream_ms`~41, sticky `focus_missing_mesh=1`, dominant skip = **orphan** (~54–56).

---

## 1. SoftDefer KEEP gates

Критерии (из `00_index.md` / `tmp_audit_ne_frontier_stand.py`):
`floor/spike ≤8` · `SiteB retarget ≤5` · `stuck med ≤5` · `stale late ≤20` · `capture ≥3` · `PL drop/spike ≤0.5` · `keep ≥160`

### CUR `112418` stand n=5094

| Gate | got | порог | статус |
| --- | ---: | --- | --- |
| floor/spike | 1.97 | ≤8 | PASS |
| SiteB capture_retarget/spike | 0.07 | ≤5 | PASS |
| stuck med | 5.0 | ≤5 | PASS |
| stale late | 0.0 | ≤20 | PASS |
| capture_bg_cap_n | 3.0 | ≥3 | PASS |
| PL drop/spike | 0.0 | ≤0.5 | PASS |
| keep_cols | 169 | ≥160 | PASS |
| **OVERALL SoftDefer KEEP** | | | **PASS** |

Info: legacy witness/spike=1.26 · SiteA ingress/spike=1.18 · unfinished late=70 · vb_no_ticket=6.5

### SoftDefer SoT `104722` stand n=371

| Gate | got | порог | статус |
| --- | ---: | --- | --- |
| floor/spike | 2.08 | ≤8 | PASS |
| SiteB capture_retarget/spike | 0.03 | ≤5 | PASS |
| stuck med | 1.0 | ≤5 | PASS |
| stale late | 0.0 | ≤20 | PASS |
| capture_bg_cap_n | 3.0 | ≥3 | PASS |
| PL drop/spike | 0.0 | ≤0.5 | PASS |
| keep_cols | 169 | ≥160 | PASS |
| **OVERALL SoftDefer KEEP** | | | **PASS** |

Info: unfinished=83 · vb_no_ticket=4.0

**Вывод SoftDefer:** оба лога в KEEP-зоне. Floor/SiteB не регрессируют относительно SoftDefer SoT; проблема CUR — не SoftDefer storm, а never-heal backlog.

---

## 2. P17 stand gates (vs `100413`)

Скрипт: `tmp_audit_100413_stand.py` · CUR stand=5094 · SoT stand=650

| Gate | CUR | SoT 100413 | порог | статус |
| --- | ---: | ---: | --- | --- |
| mesh_discarded_late/spike | **0.00196** | 0.465 | ≤0.05 | **PASS** |
| vb_focus late | **107** | 99 | ≤50% SoT (=49.5) | **FAIL** |
| unf late vs early | 70.5 / 69.0 | 65 / 57 | late < early (или оба ≤15) | **FAIL** |
| stale late (dark_face_stale_near) | **0** | 1755 | ≤200 | **PASS** |
| stream_ms med | **40.94** | 41.01 | ≤25 | **FAIL** |
| keep | 169 | 169 | ≥160 | **PASS** |
| SiteB ≤5 | 0.074 | 0.031 | ≤5 | **PASS** |
| floor ≤8 | 1.97 | 2.26 | ≤8 | **PASS** |
| **OVERALL P17 stand** | | | | **FAIL** |

**Интерпретация:** discard/stale storm P17 исправлен. Visual + stream budget на stand **не** улучшились до eye/heal цели (VB sticky, unfinished не падает, stream~41ms).

---

## 3. Сравнение сегментов: P13 vs P17bad vs CUR

Источник: `scripts/segment_medians.py`. Значения — медианы (кроме `/spike`).

### 3.1 ENTER (first 80)

| Метрика | P13 | P17bad | CUR | Δ CUR−P13 |
| --- | ---: | ---: | ---: | ---: |
| wall_ms | 120.0 | 131.3 | 142.7 | +22.7 |
| stream_ms | 16.4 | 24.8 | 42.0 | +25.6 |
| mesh_emerge_ms | 46.0 | 72.5 | 59.3 | +13.3 |
| mesh_dirty_tick_ms | 22.0 | 20.0 | 32.6 | +10.6 |
| unfinished_visual | 35.5 | 26.0 | 63.5 | +28.0 |
| column_loaded_no_mesh_n | 35.5 | 27.0 | 63.0 | +27.5 |
| chunk_meshed_unlit_hidden | 19.0 | 28.0 | 72.5 | +53.5 |
| visible_black_focus_n | 83.0 | 91.0 | 118.0 | +35.0 |
| visible_black_no_ticket_n | 22.0 | 15.5 | 8.0 | −14.0 |
| pending_light_focus | 33.5 | 30.0 | 73.5 | +40.0 |
| focus_missing_mesh | 1.0 | 1.0 | 1.0 | 0 |
| dirty_fm_n | 53.0 | 53.0 | 46.0 | −7.0 |
| dirty_remesh_n | 23.0 | 40.0 | 18.5 | −4.5 |
| skip_orphan | 45.0 | 50.0 | 43.0 | −2.0 |
| mesh_discarded_late /spike | 0.114 | 0.038 | 0.203 | +0.089 |
| SoftDefer floor /spike | 5.80 | 2.11 | 2.22 | −3.58 |
| SoftDefer SiteB /spike | 0.0 | 0.038 | 0.025 | +0.025 |
| opaque_refs_cpu_vis | 616 | 271 | 582 | −34 |
| keep_cols | 169 | 169 | 169 | 0 |

### 3.2 MOVING

| Метрика | P13 | P17bad | CUR | Δ CUR−P13 |
| --- | ---: | ---: | ---: | ---: |
| wall_ms | 117.7 | 131.9 | 140.6 | +22.9 |
| stream_ms | 14.4 | 21.1 | 22.7 | +8.3 |
| mesh_emerge_ms | 47.0 | 75.4 | 74.2 | +27.2 |
| mesh_dirty_tick_ms | 23.6 | 23.4 | 29.8 | +6.2 |
| unfinished_visual | 33.0 | 22.0 | 34.0 | +1.0 |
| column_loaded_no_mesh_n | 34.0 | 23.0 | 34.0 | 0 |
| chunk_meshed_unlit_hidden | 17.0 | 16.0 | 28.0 | +11.0 |
| visible_black_focus_n | 72.5 | 72.0 | 86.0 | +13.5 |
| visible_black_no_ticket_n | 27.5 | 13.5 | 14.0 | −13.5 |
| pending_light_focus | 23.5 | 19.5 | 31.0 | +7.5 |
| focus_missing_mesh | 1.0 | 1.0 | 1.0 | 0 |
| dirty_fm_n | 56.0 | 53.0 | 53.0 | −3.0 |
| dirty_remesh_n | 24.0 | 20.0 | 16.0 | −8.0 |
| skip_orphan | 50.0 | 50.0 | 50.0 | 0 |
| mesh_discarded_late /spike | 0.061 | 0.079 | 0.087 | +0.026 |
| SoftDefer floor /spike | 7.18 | 3.57 | 3.43 | −3.75 |
| SoftDefer SiteB /spike | 0.0 | 0.178 | 0.068 | +0.068 |
| opaque_refs_cpu_vis | 613.5 | 330 | 664.5 | +51 |
| keep_cols | 169 | 169 | 169 | 0 |

### 3.3 STAND (land keep≥160 trailing)

| Метрика | P13 | P17bad | CUR | Δ CUR−P13 |
| --- | ---: | ---: | ---: | ---: |
| n spikes | 1330 | 650 | **5094** | — |
| wall_ms | **107.6** | 131.9 | 142.2 | +34.6 |
| stream_ms | **19.5** | 41.0 | 40.9 | +21.4 |
| mesh_emerge_ms | **54.0** | 48.2 | 73.1 | +19.1 |
| mesh_dirty_tick_ms | **18.9** | 26.0 | 24.8 | +5.9 |
| unfinished_visual | **27.0** | 59.0 | 70.0 | +43.0 |
| column_loaded_no_mesh_n | **29.0** | 59.0 | 70.0 | +41.0 |
| chunk_meshed_unlit_hidden | **20.0** | 56.0 | 62.0 | +42.0 |
| visible_black_focus_n | **95.0** | 99.0 | 107.0 | +12.0 |
| visible_black_no_ticket_n | 40.0 | 5.0 | 6.0 | −34.0 |
| pending_light_focus | **20.0** | 56.0 | 62.0 | +42.0 |
| focus_missing_mesh | 1.0 | 1.0 | 1.0 | sticky |
| dirty_fm_n | 57.0 | 54.5 | 64.0 | +7.0 |
| dirty_remesh_n | 48.0 | 17.0 | 48.0 | 0 |
| skip_orphan | 50.0 | 45.0 | **54.0** | +4.0 |
| mesh_discarded_late /spike | 0.012 | **0.465** | **0.002** | −0.010 |
| SoftDefer floor /spike | 6.71 | 2.26 | **1.97** | −4.74 |
| SoftDefer SiteB /spike | 0.0 | 0.031 | 0.074 | +0.074 |
| opaque_refs_cpu_vis | 439 | 698 | 461 | +22 |
| keep_cols | 169 | 169 | 169 | 0 |

### 3.4 LATE stand (last 40)

| Метрика | P13 | P17bad | CUR | Δ CUR−P13 |
| --- | ---: | ---: | ---: | ---: |
| wall_ms | 108.4 | 126.1 | 138.0 | +29.6 |
| stream_ms | **19.3** | 40.6 | 42.0 | +22.7 |
| mesh_emerge_ms | 55.1 | 53.7 | 80.2 | +25.1 |
| mesh_dirty_tick_ms | 17.8 | 34.1 | 28.0 | +10.2 |
| unfinished_visual | **27.0** | 65.0 | 70.5 | +43.5 |
| column_loaded_no_mesh_n | 30.0 | 64.0 | 71.0 | +41.0 |
| chunk_meshed_unlit_hidden | 22.0 | 62.0 | 62.0 | +40.0 |
| visible_black_focus_n | 95.0 | 99.0 | 107.0 | +12.0 |
| visible_black_no_ticket_n | 39.0 | 4.0 | 6.0 | −33.0 |
| pending_light_focus | 22.0 | 62.5 | 62.0 | +40.0 |
| focus_missing_mesh | 1.0 | 1.0 | 1.0 | sticky |
| dirty_fm_n | 59.5 | 55.5 | 69.5 | +10.0 |
| dirty_remesh_n | 48.0 | 15.0 | 48.0 | 0 |
| skip_orphan | 52.0 | 45.0 | **56.0** | +4.0 |
| mesh_discarded_late /spike | 0.0 | **0.590** | **0.0** | 0 |
| SoftDefer floor /spike | 4.77 | 2.49 | 2.13 | −2.64 |
| SoftDefer SiteB /spike | 0.0 | 0.051 | 0.103 | +0.103 |
| opaque_refs | 440 | 698.5 | 462 | +22 |
| keep_cols | 169 | 169 | 169 | 0 |

---

## 4. CUR long-stand эволюция (`tmp_audit_112418_stand.py`)

Cruise = spikes `0:229` · Stand = `229:` (n=5094) @ xz=(-32,74)

| Окно | wall | stream | emerge | dirty_tick | unf | VB | no_ticket | PL | dirty_fm | dirty_remesh | discard/s | SiteB | floor |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| CRUISE | 139.9 | 29.3 | 72.5 | 31.1 | 36 | 92 | 13 | 34 | 51 | 18 | 0.070 | 0.031 | 1.88 |
| STAND early 0–200 | 148.2 | 43.4 | 85.3 | 28.1 | 66.5 | 107 | 8 | 61 | 60 | 48 | 0.0 | 0.075 | 1.98 |
| STAND mid 2000–2200 | 145.0 | 39.8 | 69.6 | 24.4 | 68 | 107 | 5 | 60 | 63 | 48 | 0.0 | 0.070 | 1.68 |
| STAND late −200 | 135.5 | 42.4 | 74.9 | 27.0 | 71 | 107 | 7 | 62 | 64.5 | 48 | 0.0 | 0.096 | 2.54 |
| STAND all | 142.2 | 40.9 | 73.1 | 24.8 | 70 | 107 | 6 | 62 | 64 | 48 | 0.002 | 0.074 | 1.97 |

**Never-heal:** early→late unfinished 66.5→71, VB 107→107, miss_frac=1.0. Stand длится ~5094 spike без закрытия дыры.

### Late skip composition (med)

| skip reason | med |
| --- | ---: |
| pipeline | 0 |
| snapshot | 0 |
| softdefer | 0 |
| locked | 0 |
| **orphan** | **54** |
| remesh_starve | 0 |
| other | 0 |

Dominant: `mesh_dirty_schedule_skip_orphan_n` ≈ 54–56 при `schedule_ok`~5–6 и caps `first=6 / remesh=2`.

### Sticky miss (CUR stand)

- miss_sticky_frac ≈ **1.0**
- top miss coords: `(-2,0,4,nh=0)` ×2519 · `(-1,0,5,nh=1)` ×1765 · `(-3,1,4,nh=1)` ×692
- underfeet_need 2609/5094; reason mostly 4/5; при need доминирует miss nh=0

---

## 5. Сводка метрик (required checklist)

| Метрика | P13 STAND | P17bad STAND | CUR STAND | заметка |
| --- | ---: | ---: | ---: | --- |
| wall_ms | 107.6 | 131.9 | 142.2 | CUR хуже P13 |
| stream_ms | 19.5 | 41.0 | 40.9 | CUR≈P17bad, ≫P13 |
| mesh_emerge_ms | 54.0 | 48.2 | 73.1 | CUR хуже обоих |
| mesh_dirty_tick_ms | 18.9 | 26.0 | 24.8 | умеренно выше P13 |
| unfinished_visual | 27 | 59 | 70 | never-heal |
| column_loaded_no_mesh_n | 29 | 59 | 70 | = unfinished path |
| chunk_meshed_unlit_hidden | 20 | 56 | 62 | PL/unlit backlog |
| visible_black_focus_n | 95 | 99 | 107 | FAIL vs 50% SoT |
| visible_black_no_ticket_n | 40 | 5 | 6 | ticket path OK-ish |
| pending_light_focus | 20 | 56 | 62 | backlog |
| focus_missing_mesh | 1 | 1 | 1 | sticky all |
| dirty_fm_n | 57 | 54.5 | 64 | |
| dirty_remesh_n | 48 | 17 | 48 | P17bad starved remesh |
| skip_orphan | 50 | 45 | 54 | dominant skip |
| mesh_discarded_late rate | 0.012 | **0.465** | **0.002** | P17 storm FIXED |
| SoftDefer floor | 6.71 | 2.26 | 1.97 | KEEP PASS |
| SoftDefer SiteB | 0 | 0.031 | 0.074 | KEEP PASS |
| opaque_refs | 439 | 698 | 461 | |
| keep_cols | 169 | 169 | 169 | KEEP PASS |

---

## 6. PASS/FAIL матрица

| Проверка | Результат |
| --- | --- |
| SoftDefer KEEP — CUR | **PASS** |
| SoftDefer KEEP — SoftDefer SoT 104722 | **PASS** |
| P17 stand gates — CUR vs 100413 | **FAIL** (vb / unf heal / stream_ms) |
| discard storm cured vs P17bad | **PASS** (0.002 ≪ 0.05 ≪ 0.465) |
| eye parity vs P13 stand (stream/unf/VB) | **FAIL** (stream +21ms, unf +43, VB +12) |
| long-stand heal (early→late unf↓) | **FAIL** (69→70.5, plateau) |

---

## 7. Выводы для research

1. **SoftDefer не регресс:** floor/SiteB/capture/keep в KEEP на CUR и SoftDefer SoT.
2. **P17 discard storm закрыт** на CUR; stale near тоже 0 (vs 1755 на 100413).
3. **Bottleneck сместился:** high `stream_ms` + `mesh_emerge_ms` + orphan skip wall при sticky miss nh∈{0,1}; visual backlog (`unf`/`PL`/`unlit_hidden`/`VB`) не сходит даже за 5k stand spikes.
4. **P13 остаётся eye SoT** по stand: stream~19, unf~27, emerge~54 — CUR далеко.
5. Следующий hotspot-анализ: orphan skip consumer / pending light drain / miss nh=0 underfeet path — не Capture++ и не SoftDefer floor tighten.

---

## Артефакты

| Файл | Содержание |
| --- | --- |
| `scripts/segment_medians.py` | ENTER/MOVING/STAND/LATE медианы |
| `raw/segment_medians.txt` | вывод helper |
| `raw/audit_112418.txt` | forensics windows |
| `raw/audit_112418_stand.txt` | stand evolution + skips |
| `raw/p17_stand_gates.txt` | P17 gate dump |
| `raw/softdefer_keep_104722.txt` | SoftDefer KEEP dump |
