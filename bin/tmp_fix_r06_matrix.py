from pathlib import Path

p = Path("docs/streaming/AUDIT16_S9_CLOSURE_MATRIX.md")
lines = p.read_text(encoding="utf-8").splitlines()
out = []
for i, line in enumerate(lines):
    out.append(line)
    if (
        line.startswith(
            "| merge_green | **false** | operator UNTESTED + dual-lane OPEN |"
        )
        and i > 360
    ):
        break

text = "\n".join(out)
old_closed = (
    "| R06 overlay water walls | **CLOSED** coded | "
    "remesh+hide; operator dive **UNTESTED** |"
)
new_reopen = (
    "| R06 overlay water walls | **REOPEN** | "
    "coded CLOSED premature; manual 161124 blacks+walls; SoftDeferHeld≡0 |"
)
if old_closed in text:
    text = text.replace(old_closed, new_reopen, 1)

append = """

### Follow-on 2026-09-19: R06 remesh Dirty bisect (manual 161124)

Start HEAD: `60202710`. Black-clean anchor (pre remesh-on-coverage risk): `27beca1c`.
Manual SoT: `bin/logs/perf_20260919-161124_66972.jsonl` (tex OK: dual_backend≡0 / block_id_flip≡0; blacks + water/black walls remain).

| Signal | 161124 fact |
|---|---|
| `softdefer_held_n` / empty_* | **≡0** — SoftDefer erase **not** the active black mechanism |
| `dirty_remesh_n` | med~66 max~86 |
| VB focus / FullyDark stalled | med~55 max~206 / med~46 max~166 |
| Hypothesis | first-drawable sea remesh (`d4e2f085`+) -> Dirty remesh backlog -> FullyDark stalled blacks; hide KEEP; SoftDefer-for-holes ban KEEP |
| R06 overlay water walls | **REOPEN** (vs premature coded CLOSED) |

Track: R1 no-op remesh callback -> AF; R2 overlay-only remesh (peer `BoundaryOverlay.active`); R3 matrix honesty.
"""

p.write_text(text.rstrip() + append + "\n", encoding="utf-8", newline="\n")
print("wrote", p, "lines", len(p.read_text(encoding="utf-8").splitlines()))
