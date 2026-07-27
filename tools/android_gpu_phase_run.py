#!/usr/bin/env python3
"""GPF6 Android GPU phase runner: desktop build + unit + android assemble + gates.

Usage:
  python tools/android_gpu_phase_run.py --phase-id AG0 --report bin/phase_AG0.json
  python tools/android_gpu_phase_run.py --phase-id AG0 --report bin/phase_AG0.json --skip-device --commit
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

COMMIT_MESSAGES = {
    "AG0": "gpu(android A0): runtime GL capability probe and caps cache",
    "AG1": "gpu(android A1): GLES fluid column scan compute path",
    "AG2": "gpu(android A2): hybrid GPU mask extract with staging mesh store",
    "AG3": "gpu(android A3): caps-driven transparent sort and GLES single-pass",
    "AG4": "gpu(android A4): GPU-by-default on capable devices with opt-out",
}

PACKAGE = "com.cubatarium"
ACTIVITY = "com.cubatarium/.MainActivity"


def run(cmd: list[str], allow_exit_codes: set[int] | None = None) -> int:
    print("+", " ".join(cmd), flush=True)
    rc = subprocess.call(cmd, cwd=ROOT)
    if allow_exit_codes is None:
        allow_exit_codes = {0}
    if rc not in allow_exit_codes:
        raise SystemExit(rc)
    return rc


def adb_available() -> bool:
    return shutil.which("adb") is not None


def adb_devices() -> list[str]:
    if not adb_available():
        return []
    try:
        out = subprocess.check_output(
            ["adb", "devices"], cwd=ROOT, text=True, errors="replace"
        )
    except (OSError, subprocess.CalledProcessError):
        return []
    serials: list[str] = []
    for line in out.splitlines():
        line = line.strip()
        if not line or line.startswith("List of"):
            continue
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            serials.append(parts[0])
    return serials


def find_debug_apk() -> Path | None:
    apk_dir = ROOT / "platforms" / "android" / "app" / "build" / "outputs" / "apk" / "debug"
    if not apk_dir.is_dir():
        return None
    apks = sorted(apk_dir.rglob("cubatarium-*.apk"), key=lambda p: p.stat().st_mtime)
    return apks[-1] if apks else None


def run_device_smoke(apk: Path, serial: str | None) -> None:
    """Install APK and launch MainActivity briefly; pull logcat for GPU probe."""
    adb = ["adb"]
    if serial:
        adb += ["-s", serial]
    run(adb + ["install", "-r", str(apk)])
    run(adb + ["logcat", "-c"], allow_exit_codes={0, 1})
    run(adb + ["shell", "am", "force-stop", PACKAGE], allow_exit_codes={0, 1})
    run(adb + ["shell", "am", "start", "-n", ACTIVITY])
    # Give GL probe + first frame time on mid-range devices.
    time.sleep(12.0)
    log_path = ROOT / "bin" / "android_gpu_device_smoke.logcat.txt"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", errors="replace") as fh:
        subprocess.call(
            adb
            + [
                "logcat",
                "-d",
                "-t",
                "4000",
                "*:S",
                "cubatarium:V",
                "AndroidGpu:V",
                "Asset:V",
                "AndroidRuntime:E",
            ],
            cwd=ROOT,
            stdout=fh,
            stderr=subprocess.STDOUT,
        )
    print(f"NOTE: device smoke logcat -> {log_path}", flush=True)
    # Soft check: crash lines are hard fail; missing GPU tags are noted only
    # (no on-device flight_sim harness yet for AG1–AG4 metrics).
    text = log_path.read_text(encoding="utf-8", errors="replace")
    if "FATAL EXCEPTION" in text or "AndroidRuntime: FATAL" in text:
        raise SystemExit(f"device smoke: FATAL EXCEPTION in {log_path}")
    markers = ("[AndroidGpu]", "android_gpu", "AllowAndroidGpu", "ProbeOpenGL")
    if not any(m in text for m in markers):
        print(
            "NOTE: device smoke started without AndroidGpu log markers "
            "(logcat tag filter may miss native glog); install OK",
            flush=True,
        )


def run_desktop_cruise(world: str, phase_id: str, report: str) -> None:
    run(
        [
            "python",
            "tools/flight_sim_run.py",
            "--world",
            world,
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
            phase_id,
            "--report",
            report,
        ],
        allow_exit_codes={0, 1},
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--phase-id", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--world", default="World_164")
    ap.add_argument(
        "--skip-device",
        action="store_true",
        help="Skip adb install/smoke (still requires android assembleDebug unless skipped)",
    )
    ap.add_argument(
        "--skip-android-build",
        action="store_true",
        help="Skip Gradle assembleDebug (desktop-only iteration)",
    )
    ap.add_argument(
        "--device-serial",
        default="",
        help="adb serial when multiple devices are attached",
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

    # Desktop autofly always for probe/F2 gates (device has no flight_sim yet).
    run_desktop_cruise(args.world, args.phase_id, args.report)

    if not args.skip_device:
        serials = adb_devices()
        if not serials:
            print(
                "FAIL: no adb device online; pass --skip-device or connect a device",
                flush=True,
            )
            raise SystemExit(2)
        serial = args.device_serial or (serials[0] if len(serials) == 1 else "")
        if args.device_serial and args.device_serial not in serials:
            print(f"FAIL: serial {args.device_serial!r} not in {serials}", flush=True)
            raise SystemExit(2)
        if not serial and len(serials) > 1:
            print(
                f"FAIL: multiple devices {serials}; pass --device-serial",
                flush=True,
            )
            raise SystemExit(2)
        apk = find_debug_apk()
        if apk is None:
            print("FAIL: debug APK not found; run without --skip-android-build", flush=True)
            raise SystemExit(2)
        print(f"NOTE: device smoke on {serial} with {apk.name}", flush=True)
        run_device_smoke(apk, serial)
        print(
            "NOTE: AG1–AG4 device cruise metrics still need on-device flight_sim; "
            "smoke covers install + launch only",
            flush=True,
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

    phase_for_gate = args.phase_id
    if args.phase_id in ("AG1", "AG2", "AG3", "AG4") and args.skip_device:
        print(
            f"NOTE: {args.phase_id} device metrics skipped; "
            "running AG0-compatible check only on desktop report",
            flush=True,
        )
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
        run(["git", "commit", "-m", msg])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
