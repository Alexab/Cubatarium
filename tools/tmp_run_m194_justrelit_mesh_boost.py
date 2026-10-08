from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "bin" / "config.json"
USERS = ROOT / "bin" / "worlds" / "World_164" / "users.json"
REPORT = ROOT / "bin" / "suite_reports" / "engine_refactor" / "relight_lifecycle_product174657_20260930m194_justrelit_mesh_boost.json"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


config_original = CONFIG.read_bytes()
users_original = USERS.read_bytes()
print("config original sha256:", sha256(config_original), flush=True)
print("users original sha256:", sha256(users_original), flush=True)

try:
    config = json.loads(config_original)
    config["render_distance_chunks"] = 8
    config.setdefault("render", {})["distance_fog"] = False
    config["render"]["altitude_adaptive_fog"] = False
    CONFIG.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")

    env = os.environ.copy()
    env["CUBA_VISUAL_BLACK_TRACE"] = "1"
    env["CUBA_FLIGHT_MOVE_SPEED_SCALE"] = "1"
    env["CUBA_FLIGHT_REQUIRE_VISIBLE"] = "1"
    env["CUBATARIUM_RELIGHT_AUDIT"] = "1"
    command = [
        sys.executable,
        str(ROOT / "tools" / "flight_sim_run.py"),
        "--world", "World_164",
        "--scenario", "product-174657",
        "--visible",
        "--fly-phase-sec", "90",
        "--stop-phase-sec", "20",
        "--idle-sec", "15",
        "--warmup-sec", "5",
        "--build-dir", "bin",
        "--phase-id", "m194_justrelit_mesh_boost",
        "--report", str(REPORT.relative_to(ROOT)).replace("\\", "/"),
        "--process-timeout", "600",
    ]
    print("starting visible M194 Release hit-lifecycle flight", flush=True)
    result = subprocess.run(command, cwd=ROOT, env=env, check=False)
    print("flight runner exit:", result.returncode, flush=True)
finally:
    CONFIG.write_bytes(config_original)
    USERS.write_bytes(users_original)
    print("config restored sha256:", sha256(CONFIG.read_bytes()), flush=True)
    print("users restored sha256:", sha256(USERS.read_bytes()), flush=True)

sys.exit(result.returncode if "result" in locals() else 1)


