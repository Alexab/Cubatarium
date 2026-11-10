---
name: Era45 Enter Perf Opts
overview: "Backlog from Era44 enter_perf_audit.md — candidates only, no implementation until metrics confirm."
todos: []
isProject: false
---

# Era45+: enter-load optimization backlog

Source: [enter_perf_audit.md](../../docs/streaming/enter_perf_audit.md) (World_174 manual run + Era44 instrumentation).

| Candidate | Condition | Impact (est.) | Risk |
|-----------|-----------|---------------|------|
| **Era45a: Adaptive gate iterations** | R2 confirmed (gate_drain >40% wall) | −20–40% gate wall | Low |
| **Era45b: Coop→gate relight handoff** | R1 confirmed, >30% duplicate columns | −10–30 s enter | Medium |
| **Era45c: Capture store hit rate** | `mesh_emerge_prep_missing_ms` dominates profile | −emerge prep tail | Medium |
| **Era45d: GPU apply batching under enter** | `gpu_pending` tail + gpu_finish_delta low | −gpu_pending tail | Medium |
| **Era45e: Partial band finalize policy** | R5 confirmed, completed≈0 | −fifo churn | High |
| **Era45f: V4 unified scheduler slice** | residual zoo after Era44 budgets | architectural | High |

**Gate:** each item needs `[EnterWarmup] profile` dominant % and/or jsonl proof from autoload before scheduling.
