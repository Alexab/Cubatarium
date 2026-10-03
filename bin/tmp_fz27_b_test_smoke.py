#!/usr/bin/env python3
"""FZ2.7 Plan B test smoke — run unit/integration tests."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin" / "Release"
TESTS = [
    "mark_relit_characterization_test",
    "mesh_light_stale_policy_test",
    "relight_install_planner_test",
    "mark_relit_integration_test",
    "miss_first_mesh_class_test",
    "chunk_mesh_revision_test",
    "frame_streaming_budget_test",
]


def main():
    failed = []
    for name in TESTS:
        exe = BIN / f"{name}.exe"
        if not exe.exists():
            print(f"SKIP missing {exe}")
            continue
        print(f"RUN {name}")
        rc = subprocess.call([str(exe)], cwd=str(BIN))
        if rc != 0:
            failed.append(name)
    if failed:
        print("FAILED:", ", ".join(failed))
        return 1
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
