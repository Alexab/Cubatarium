# Architecture audit probes — 2026-09-10

These probes diagnose the production code at `b1badb4f`. They do not patch the
engine and are not registered as passing CI tests. A nonzero result is expected
on the audited baseline; promote them into the test suite when fixing the bugs.

## Column scheduler

Run from `E:/Work/Home/Cubatarium` with the already installed LLVM/GLM:

```powershell
clang++ -std=c++17 -Isrc -Ibuild/desktop-linux/vcpkg_installed/x64-windows-static/include tools/research/ColumnSchedulerAuditRepro.cpp src/World/Streaming/ColumnFlowScheduler.cpp -o build/test-compile/column_scheduler_audit_repro.exe
if ($LASTEXITCODE -ne 0) { throw 'Repro compilation failed' }
& build/test-compile/column_scheduler_audit_repro.exe
```

Observed 2026-09-10: compilation succeeded; executable exit code **1**:

```text
VIOLATION: distinct full-width column coordinates retain distinct work
priority refresh: got=1 priority=10 cy=1
VIOLATION: same-kind urgency and preferred slice are refreshed
VIOLATION: reported live queue depth excludes superseded tickets
ABA replacement: got=1 priority=10
VIOLATION: old cancellation cannot discard a newer ticket with the same key
unused tombstone: got=0 occupied=1
VIOLATION: cancelling a nonexistent variant cannot poison a future ticket
contract violations=5
```

The first and cancellation cases expose data loss. Priority/slice refresh and
live queue depth are explicit desired scheduling contracts: the existing API
currently documents same-kind deduplication and raw heap size, so those probes
also specify the intended replacement contract. They are not claims of memory
corruption. All probes call the real scheduler implementation.

The existing `src/Test/ColumnFlowSchedulerTest.cpp` was also independently
compiled against the same scheduler on 2026-09-10 and returned exit 0:
`column_flow_scheduler_test: OK`. Passing that suite does not cover these new
counterexamples.

## Scorecard missing-input acceptance

```powershell
python -c "import sys; sys.path.insert(0, 'tools'); import AnalyzePhase57Scorecard as s; print('fidelity_missing_logs=', s.evaluate_fidelity({'periods': 1}, None, None, None)); print('product_missing_logs=', s.evaluate_product(None, None))"
```

Observed output (Python exits 0; this invocation prints the faulty decisions):

```text
fidelity_missing_logs= []
product_missing_logs= []
```

Empty failure lists are subsequently rendered as `FIDELITY_OK` / `PRODUCT_OK`
by the existing main function. This is a function-level reproduction of the
missing-input path, not a real game benchmark or an assertion that every
historical scorecard was invalid.

See the [audit](E:/Work/Home/Cubatarium/docs/streaming/ARCHITECTURE_AUDIT_2026-09-10.md)
and [remediation plan](E:/Work/Home/Cubatarium/docs/streaming/ARCHITECTURE_REMEDIATION_PLAN_2026-09-10.md).
