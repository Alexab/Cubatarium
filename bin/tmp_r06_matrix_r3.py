from pathlib import Path

p = Path("docs/streaming/AUDIT16_S9_CLOSURE_MATRIX.md")
text = p.read_text(encoding="utf-8")

# Update track line / append R1-R2 results if not present
marker = "### Follow-on 2026-09-19: R06 remesh Dirty bisect results"
if marker in text:
    raise SystemExit("results section already present")

append = """

### Follow-on 2026-09-19: R06 remesh Dirty bisect results

| Step | SHA | AF | Mid VB med (vs 161124 87.5) | Mid FD stalled med (vs 60.5) | Note |
|---|---|---|---|---|---|
| R1 no-op remesh | `54ddfd83` | cold exit 2; adequacy+eye PASS | **62** | **44** | confirms remesh→Dirty/blacks; SoftDeferHeld≡0 |
| R2 overlay-only | `6a07c8d7` | warm exit 2; adequacy+eye PASS (cold eye flake once) | **76** | **51** | peer remesh iff `BoundaryOverlay` face bit; dual/flip≡0; fog≡4 |

| Gate | Status | Cause / note |
|---|---|---|
| R06 overlay water walls | **OPEN** coded partial | overlay-only remesh landed; operator dive **UNTESTED**; AF ≠ pixels |
| remesh-on-coverage flood | **mitigated** | R1 proved; R2 narrower than pre-bisect; VB/FD still better than 161124, not as low as R1 no-op |
| SoftDefer-for-holes / SoftDeferHeld | **KEEP** | held≡0 on SoT; not the black mechanism |
| dual-lane / mid FullyDark stalled | **OPEN** | AF exit 2 OK |
| merge_green | **false** | operator UNTESTED + dual-lane OPEN |
"""

# Flip R06 row if still REOPEN without OPEN coded partial - keep REOPEN in E3 table is fine
p.write_text(text.rstrip() + "\n" + append + "\n", encoding="utf-8", newline="\n")
print("appended results")
