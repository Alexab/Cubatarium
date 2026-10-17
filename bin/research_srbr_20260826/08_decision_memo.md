# 08 — Decision memo

**SRBR 20260826 · Streaming + Render bottleneck research**  
HEAD `1f3f7675` (P17) · eye baseline `9f502dd5` (P13) · SoT manual `112418`

## 1. Диагноз (5 предложений)

На длинной land-стоянке (`112418`, ~30 мин, keep=169) чёрное и FPS не сходятся: VB≈107, unfinished≈70, stream≈41. SoftDefer и discard уже зелёные (P17 закрыл storm). Главный механизм — Dirty-очередь забита **ghost-координатами без `HasChunk`** (`skip_orphan`≈54 при `schedule_ok`≈6), из‑за чего FirstMesh почти не drain’ится. Параллельно ticketed VB (no_ticket≈6) = unlit_hidden/PL≈62 не выедается. Sticky miss nh=0 под ногами `(-2,0,4)` висит весь stand.

## 2. Почему P13 казался лучше глазом

P13 (`170807`): stand stream≈19.5, unf≈27, VB≈95 — ниже текущего плато. Remesh protect lit-settle (KEEP) drain’ил FullyDark без сегодняшнего ghost/ticketed lock. Post-P13 (P15–P17) вылечил SoftDefer thrash и discard, но не вернул consumer effectiveness P13 на long stand.

## 3. Что уже закрыто

- SoftDefer KEEP (floor≈2, SiteB≪5, keep=169) — не root.
- `mesh_discarded_late` / stale storm — cured (0.002 vs 0.47).
- Publish keep-until-replace / miss_undrawn hold — держать.

## 4. Что делать сейчас

1. **Package P0:** запрет Dirty без HasChunk + prune ghosts + гарантированный FM для sticky miss nh≤1 HasChunk.  
2. Затем **Package P1:** ticketed VB / unlit consume на stand (не Capture++).

## 5. Чего не делать

- Не трогать SoftDefer MaxAge / Capture floors / P13 remesh protect «на удачу».
- Не возвращать P14 F5 deadband / MaxAge ratchet.
- Не принимать teleport autofly (`fz-validate`, `land-cruise`, teleport `fz-inring`) как DoD.
- Не оптимизировать SoftDefer empty scan ради FPS (0.3ms).

## 6. Следующий manual протокол

- Cold/resume World_164, land keep≥160, cruise → stand ≥180s (лучше ≥10 мин как 112418).
- Без teleport для gate-of-record.
- Прогнать: SoftDefer audit + stand gates vs `100413`/`112418` + eye.
- Успех P0: skip_orphan≤10, miss_frac↓, unf/VB вниз, stream≤30, SoftDefer KEEP.

## Артефакты

| File | Role |
| --- | --- |
| `00_index.md` | SoT + no-teleport policy |
| `01_ownership_map.md` | signal ownership |
| `02_baseline_delta.md` | P13→P17 KEEP/SUSPECT |
| `03_telemetry_pack.md` | segment tables |
| `04_hotspot_matrix.md` | H1–H10 + forensics |
| `05_best_practice_scorecard.md` | industry gaps |
| `06_arch_options.md` | A/B/C variants |
| `07_roadmap.md` | P0/P1/P2 packages |
