# 04 — Hotspot matrix (flight 081522)

## H1 Witness retarget thrash — CONFIRMED

- `softdefer_capture_retarget_n` med **12**/frame on cruise spikes
- `relight_completed_n` med **0** during cruise
- Fix: FP1 pin-until-lit, block better_horiz hop nh≤2

## H2 FM consumer starvation — CONFIRMED

- `mesh_admission_mode` = HoleDrain (3) but `schedule_ok` med **1**
- `dirty_n` med **53**, `dirty_fm_n` med **0**
- Fix: FP2 FM floor + skip-reason audit

## H3 Ticketed VB plateau — CONFIRMED

- `visible_black_focus_n` med **73**, stable (no blink)
- `visible_black_no_ticket_n` med ~40
- Fix: FP3 cruise ticketed consume

## H4 Ghost Dirty — CLOSED (SRBR-P0)

- `skip_orphan` = **0** on 081522 (was 54 on 112418)

## H5 stream_ms budget — CONFIRMED

- med **88** vs budget **24**; dominant spike class **stream** (264/265)

## RC mapping

| RC | Status | Phase |
| --- | --- | --- |
| RC1 witness | CONFIRMED | FP1 |
| RC2 throughput | CONFIRMED | FP2 |
| RC3 ticketed VB | CONFIRMED | FP3 |
| RC4 multi-authority | SUSPECT | FP4 |
| RC5 backpressure | CONFIRMED | FP5 |
