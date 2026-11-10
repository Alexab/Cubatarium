# A21 P0 baseline AF evidence

Date: 2026-09-22  
HEAD at P0/P1 land: see `git log` for `test: A21 P0` / `fix: A21 P1`.

## Protocol

```bat
set CUBA_FLIGHT_FOG_ON=1
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --report bin/suite_reports/g1_a10_relight/a21_p1_cold.json
python -X utf8 tools/n01_v21_scorecard.py <perf.jsonl> -o bin/suite_reports/g1_a10_relight/a21_p1_cold_score.json --label a21_p1_cold
```

No-teleport west product-174657; fog ON; operator_visual remains UNTESTED (`merge_green=false` expected).

## Scorecard change (P0.4)

Input adequacy no longer requires VB/miss_stuck symptom floors. Symptom
reproduction is `diagnostic_only` under `symptom_reproduction` in the score JSON.

## Contract repro (P0.5 / P1 / P2.3)

`current_contract_repro`: **violations=0** after P1 + PreferKick pending fix.

## A/B ×5

Full 5× interleaved A/B vs anchors `27beca1c` / `dd7871ab` requires independent
world copies ([A21_REPLAY_POSE_FIXTURES.md](A21_REPLAY_POSE_FIXTURES.md)) and is
tracked as ongoing soak — single cold proxy run is the step gate; spread stats
fill in as repeats complete.

## Honesty

AF proxy ≠ CLOSED. Manual operator eye required for visual CLOSED.
