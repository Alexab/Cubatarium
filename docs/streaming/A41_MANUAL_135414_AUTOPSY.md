# A41 manual 135414 — почему DoD не достигнут

Date: 2026-09-24  
Perf: `bin/logs/perf_20260924-135414_29876.jsonl` (post–SLA remint exe)  
Score: `bin/suite_reports/a41/manual_135414_score.json`  
Prior plateau: [132520](A41_MANUAL_132520.md)

## Verdict

SLA remint **сработал** (`mark_relit_schedule` med=2 / last=4; на 132520 last=0).  
Цели плана всё равно FAIL: remint кладёт Dirty, но **LightRepair + PendingLight SoftDefer снимает remesh Dirty с drawable FD**, а ClearPending не отпускает PL пока FD/`stale` живы → dual unlit и PL drain невозможны.

## DoD checklist (135414)

| DoD / Gate8 | Result | Evidence |
|---|---|---|
| mesh holes `periods_gt0→0` | **PASS** | holes gt0=0/23 (было 8/25 на 132520) |
| SoftDefer-without-ticket | **PASS** | held=0, empty=0, `vb_no_ticket=0` |
| Kick / PreferKick | **PASS** | kick≈0 (1 period max=1) |
| Hide⇒ticket | **PASS** | no_ticket=0; fd_repair+stalled cover VB |
| dual unlit≤15 | **FAIL** | med=12, **last=55**, max=58 |
| PL drain / demand_stop | **FAIL** | PL last=71, stop=0 |
| LegalDark terminal | **FAIL** | legal_dark=0 весь полёт |
| eye / merge_green | **FAIL** | VB last=108, FD debt=108 |

## vs 132520 (эффект SLA)

| | 132520 | 135414 |
|---|---|---|
| holes last | 1 | **0** |
| mark_relit_schedule last | 0 | **4** |
| skip_snapshot last | 350 | **0** (cruise med 114) |
| dirty last | 377 | 60 |
| unlit last | 26 | **55** (хуже на стопе) |
| PL last | 62 | 71 |
| VB last | 70 | 108 |
| fifo last | 0 | 15 |

SLA разблокировал clock remint; **не** разблокировал clear FD/PL.

## Root cause rank

### R1 — SoftDefer снимает remesh Dirty при PL (главный)

1. LightRepair ⇒ `erase_pending_light=false` (`MarkRelitInstall.cpp`).
2. `DeferMeshUntilLit` / MeshLitGate: `pending_light` ⇒ SoftDefer ON; для FullyDark unlit-preview запрещён.
3. Schedule (`ChunkMeshCache.cpp` ~6595–6635): drawable + SoftDefer + `!StarveRemeshForHoles` → **`Dirty.RemoveAt`**.
4. На 135414 holes=0 → hole-starve fallthrough **не** спасает remesh.
5. SLA каждые 3s снова Invalidate+Dirty → SoftDefer снова RemoveAt → `dirty_dropped`↑, FD остаётся.

### R2 — ClearPending ↔ FD / vertex-stale

`any_fully_dark && !legal_settled` и `any_stale_dark` (`ChunkHasStaleDarkFaces`) блочат clear.  
Open_sky LightRepair никогда не ставит `legal_dark_settled` → PL не drain, пока remesh не уберёт FD (а remesh убивается R1).

### R3 — Dual stale taxonomy

MarkRelit `still_stale` = только `meshed_light < field`.  
Oracle/draw: FullyDark* → `StaleVertexLight` (`dark_face_stale_near` last=1725).  
Equal-rev open_sky выглядит «LightRepair once», а census — вечный stale VL.

### R4 — Dual unlit метрика = pending-dark

`ChunkMeshedUnlit` ≈ `FocusDarkMesh` ≈ sticky + `CountPendingDarkFocusMeshes` — не LegalDark-hidden.  
PL↑ ⇒ unlit↑. Dual≤15 требует PL drain / lit publish, не отдельный unlit-heal.

### R5 — Capture budget (вторично)

Cruise: `skip_snapshot` med=114, `schedule_ok` med=2. На стопе skip=0, но SoftDefer RemoveAt уже доминирует.

## Что план уже закрыл

- VisualObligation enum / classify / sole Ready writer  
- open_sky → LightRepair (не LegalDark)  
- SLA remint clock (после 132520)  
- SoftDefer Hold requires ticket owner  
- Kick OFF  

## Что план ещё не закрыл (разрыв DoD)

Контракт **«LightRepair keep PL until Published»** конфликтует с **SoftDefer RemoveAt remesh Dirty while PL**: remesh — единственный путь к Published для equal-rev FD, и он снимается из очереди.

Нужен ownership-исключение (диагноз, не Kick):

- remesh Dirty под `visual_obligation==LightRepair` **не** SoftDefer-RemoveAt (fallthrough schedule), **или**
- MeshLitGate: LightRepair remesh exempt from pending SoftDefer (cap/coalesce), **или**
- terminal: после N SLA без progress — иной owner (не mass MarkDirty flood).

Cave LegalDark path не виноват здесь (`legal_dark=0` — open_sky west).

## Next (вне симптоматики)

1. Unit: SoftDefer + PL + drawable FD + LightRepair ⇒ Dirty **не** RemoveAt / schedule path reachable.  
2. Prod: LightRepair remesh exempt или gate `ShouldSchedule*UnderSoftDefer` для LightRepair.  
3. Re-eye + scale=1 AF после фикса. Ring OFF.
