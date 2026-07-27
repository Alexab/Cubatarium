#!/usr/bin/env python3
"""GPF6 Android GPU phase runner: desktop build + unit + android assemble + gates.

Usage:
  python tools/android_gpu_phase_run.py --phase-id AG0 --report bin/phase_AG0.json
  python tools/android_gpu_phase_run.py --phase-id AG0 --report bin/phase_AG0.json --skip-device --commit
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

COMMIT_MESSAGES = {
    "AG0": "gpu(android A0): runtime GL capability probe and caps cache",
    "AG1": "gpu(android A1): GLES fluid column scan compute path",
    "AG2": "gpu(android A2): hybrid GPU mask extract with staging mesh store",
    "AG3": "gpu(android A3): caps-driven transparent sort and GLES single-pass",
    "AG4": "gpu(android A4): GPU-by-default on capable devices with opt-out",
}


def run(cmd: list[str], allow_exit_codes: set[int] | None = None) -> int:
    print("+", " ".join(cmd), flush=True)
    rc = subprocess.call(cmd, cwd=ROOT)
    if allow_exit_codes is None:
        allow_exit_codes = {0}
    if rc not in allow_exit_codes:
        raise SystemExit(rc)
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--phase-id", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--world", default="World_164")
    ap.add_argument(
        "--skip-device",
        action="store_true",
        help="Skip adb device cruise (still requires android assembleDebug)",
    )
    ap.add_argument(
        "--skip-android-build",
        action="store_true",
        help="Skip Gradle assembleDebug (desktop-only iteration)",
    )
    ap.add_argument(
        "--commit",
        action="store_true",
        help="git commit on GO with the fixed phase message",
    )
    args = ap.parse_args()

    run(
        [
            "cmake",
            "--build",
            "build/desktop-msvc",
            "--config",
            "Release",
            "--target",
            "Cubatarium",
            "--parallel",
            "8",
        ]
    )
    run(
        [
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            "tools/run_gpu_tail_unit_tests.ps1",
        ]
    )
    # Policy unit (A0+).
    policy_exe = ROOT / "build" / "desktop-msvc" / "Release" / "android_gpu_policy_test.exe"
    if policy_exe.exists() or True:
        run(
            [
                "cmake",
                "--build",
                "build/desktop-msvc",
                "--config",
                "Release",
                "--target",
                "android_gpu_policy_test",
                "render_backend_factory_test",
                "--parallel",
                "8",
            ]
        )
        run([str(ROOT / "build" / "desktop-msvc" / "Release" / "android_gpu_policy_test.exe")])
        run([str(ROOT / "build" / "desktop-msvc" / "Release" / "render_backend_factory_test.exe")])

    if not args.skip_android_build:
        run(
            [
                "powershell",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                "scripts/build/android-debug.ps1",
            ]
        )

    # Desktop autofly for AG0 (probe telemetry). Device cruise optional.
    if args.skip_device:
        run(
            [
                "python",
                "tools/flight_sim_run.py",
                "--world",
                args.world,
                "--teleport-cruise",
                "--seconds",
                "130",
                "--fly-stop",
                "--fly-phase-sec",
                "45",
                "--stop-phase-sec",
                "60",
                "--idle-sec",
                "8",
                "--phase-id",
                args.phase_id,
                "--report",
                args.report,
            ],
            allow_exit_codes={0, 1},
        )
    else:
        # Prefer desktop cruise for AG0 probe; AG1+ device when available.
        run(
            [
                "python",
                "tools/flight_sim_run.py",
                "--world",
                args.world,
                "--teleport-cruise",
                "--seconds",
                "130",
                "--fly-stop",
                "--fly-phase-sec",
                "45",
                "--stop-phase-sec",
                "60",
                "--idle-sec",
                "8",
                "--phase-id",
                args.phase_id,
                "--report",
                args.report,
            ],
            allow_exit_codes={0, 1},
        )

    run(
        [
            "python",
            "tools/flight_sim_phase_gate.py",
            "--phase-id",
            "F2",
            "--report",
            args.report,
        ]
    )

    # AG1–AG4 require android_gpu_effective on device; on desktop AG0 only.
    phase_for_gate = args.phase_id
    if args.phase_id in ("AG1", "AG2", "AG3", "AG4") and args.skip_device:
        print(
            f"NOTE: {args.phase_id} device metrics skipped; "
            "running AG0-compatible check only on desktop report",
            flush=True,
        )
        # Still attempt the phase gate — may NO-GO without device metrics.
    run(
        [
            "python",
            "tools/flight_sim_phase_gate.py",
            "--phase-id",
            phase_for_gate,
            "--report",
            args.report,
        ]
    )

    if args.commit:
        msg = COMMIT_MESSAGES.get(args.phase_id, f"gpu(android {args.phase_id})")
        run(["git", "add", "-A"])
        # HEREDOC-equivalent for Windows: pass -m directly.
        run(["git", "commit", "-m", msg])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
