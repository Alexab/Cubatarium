# Interim eye-safe baseline (post wall-diet bisect)

**Tip freeze:** `7a25581b` (wall-diet A6 docs) — **not** eye-safe (manual `200927`).

**Interim working config (pre N01 v2):**

| Piece | State |
|---|---|
| A2 row frustum | ON (KEEP) |
| A3 structural CooldownKey | ON (KEEP) |
| A1 atomic publish `58f65ffe` | OFF (B4 FAIL) |
| A4 prep_sched telem | ON |
| Spawn ring | eager (Bisect B3) until S3 cache |

**Confirm flight:** `perf_20260914-205626_1296.jsonl` — no holes/swap/blink; sticky mid blacks only.

Follow-on: `.cursor/plans/n01_rework_mid-black.plan.md`.
