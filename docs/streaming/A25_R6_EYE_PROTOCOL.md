# A25 R6 — Eye / frame budget acceptance protocol

Date: 2026-09-22  
Status: **operator_visual=UNTESTED** ⇒ `merge_green=false`

## Protocol (do not skip)

1. Cold + warm west cruise on independent world copies (same seed hash).
2. ≥5 interleaved repeats; fixed resolution/power/background.
3. Record run manifest (SHA, exe hash, flags, GL, route hash).
4. Gates per run:
   - A24 safety: `near_focus_holes` periods>0 == 0; `dirty_dropped/period` ≤ ~800
   - End-of-flight black / debt (after holes stable)
   - Frame: P95 ≤16.7 ms, P99 ≤33.3 ms (startup/teleport separate)
   - A/B vs `dcc02e27` binary on same workload
5. Human eye west cruise: no unexplained holes/blacks/wrong materials in scored area.
6. AF fog scorecard is **proxy only** — never substitutes eye.

## Current cycle result

Eye not executed in this automation cycle. Matrix remains UNTESTED. AF gates and honesty docs are landed for when the operator runs the protocol.

## A26 N6 note

Successor honesty doc: [`A26_N6_ACCEPTANCE.md`](A26_N6_ACCEPTANCE.md). `merge_green` remains false.
