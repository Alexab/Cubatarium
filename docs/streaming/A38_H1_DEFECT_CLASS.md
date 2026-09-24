# A38 R6 — defect class + true-far

Extends [A37_H1_DEFECT_CLASS](A37_H1_DEFECT_CLASS.md).

Period telem (FramePerfMonitor):

- `defect_class_primary` — `ChunkDefectClass` int (GeometryMissing=0 … Unknown=7)
- `demand_unsat_geom|light|face|coverage|retain` — stop breakdown

True-far: run `product-174657-far` only after R1 queues healthy (`demand_stop_converged` / unfinished drain on cold). Precision class only if queues healthy and absolute distance ≥8192 with stable artifact gens.
