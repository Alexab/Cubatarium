# Streaming Visualization

Этот каталог собирает техническую историю и дорожную карту по проблемам
визуализации мира при входе в новые области.

## Документы

- `ROOT_CAUSE_2026-07.md` — почему фиксы не кончаются (Era 13 / manual_1752).
- `ROOT_CAUSE_2026-08.md` — Era14 frame nest + wall-gated heal (manual_151212).
- `ERA14_POSTMORTEM.md` — KEEP/DISCARD post-1f52bdd5 и V4 execution.
- `EVOLUTION.md` — эволюция streaming/light/mesh (Era 1–14, R1–R24).
- `MEMORY_BUDGET.md` — byte-budget, overflow/expand policies, knobs, gates.
- `ARCHITECTURE_OPTIONS.md` — варианты архитектуры, Era 13–14 decisions, anti-zoo.
- `BEST_PRACTICES.md` — industry gap (Hide⇒Ticket, async floor, SoT, frame contract).
- `IMPLEMENTATION_PLAN.md` — целевая архитектура, фазы реализации и критерии
  приёмки.
- `PHASE_EXECUTION.md` — лог прогонов и memory-crisis notes (`214430` / `220018` / `221846`).

## Текущий pipeline (до V2)

```mermaid
flowchart LR
  Gen[AsyncGen / IO] --> Commit[Commit terrain column]
  Commit --> Relight[Relight queue]
  Commit --> Dirty[Dirty mesh queue]
  Relight --> MarkRelit[MarkRelitChunksForMesh]
  MarkRelit --> Dirty
  Dirty --> Mesh[AsyncMeshBuilder]
  Mesh --> Draw[Greedy draw]
  Commit -.-> Pending[PendingLightBeforeMesh]
```

Проблема: draw возможен при `PendingLight` (preview mesh), поэтому
`visual_holes=0` не означает готовность картинки.

## Целевой pipeline

```mermaid
flowchart LR
  Gen[AsyncGen / IO] --> Commit[Commit terrain column]
  Commit --> Seed[Commit-time skylight seed]
  Seed --> Relight[Relight only when needed]
  Relight --> LitReady[LitReady]
  LitReady --> Mesh[Mesh build]
  Mesh --> RenderReady[RenderReady]
  RenderReady --> Draw[Draw]
```

Целевой инвариант: `видимое = RenderReady`, а не просто `mesh существует`.
PendingLight не должен вести в Draw.
