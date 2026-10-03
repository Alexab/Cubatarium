#!/usr/bin/env python3
"""Fail if a Streaming *Policy.h lacks a BUDGET_MS annotation.

Perf-root P4: new policy headers must declare their cost budget so we do not
re-enter the heuristic treadmill without attribution.

Accepted forms (anywhere in the file, usually near the top):
  // BUDGET_MS: 2.0
  /* BUDGET_MS: 0.5 */
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY_DIR = ROOT / "src" / "World" / "Streaming"
BUDGET_RE = re.compile(r"BUDGET_MS\s*:\s*[0-9.]+", re.I)

# Exempt tiny/non-heuristic headers that are not diet/cadence policies.
EXEMPT = {
    "ColumnDesiredStage.h",
    "ColumnEmergeBump.h",
    "ColumnEmergeState.h",
    "ColumnFlowExecutor.h",
    "ColumnFlowMeshOwnership.h",
    "ColumnFlowScheduler.h",
    "ColumnJobGraph.h",
    "ColumnRecord.h",
    "ColumnTicketMap.h",
    "ColumnVisualSnapshot.h",
    "EnterSessionPhase.h",
    "WorldStreaming.h",
    "ChunkEmergeCoordinator.h",
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--annotate-missing", action="store_true",
                    help="Print suggested BUDGET_MS stubs for missing files")
    args = ap.parse_args()
    missing = []
    for path in sorted(POLICY_DIR.glob("*Policy.h")):
        if path.name in EXEMPT:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if not BUDGET_RE.search(text):
            missing.append(path)
    if missing:
        if not args.quiet:
            print("Policy headers missing BUDGET_MS annotation:")
            for p in missing:
                print(" ", p.relative_to(ROOT))
                if args.annotate_missing:
                    print("    // BUDGET_MS: 0.0  // TODO: measure via Tracy")
        return 1
    if not args.quiet:
        print("All policy headers declare BUDGET_MS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
