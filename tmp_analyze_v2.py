import json, subprocess, sys
from pathlib import Path

def analyze(report, tag):
    logs = sorted(
        Path("bin/logs").glob("Cubatarium.exe*.INFO*"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    info = logs[0]
    print("===", tag, "info", info.name)
    return subprocess.call(
        [
            sys.executable,
            "tools/AnalyzePhase56Scorecard.py",
            "--report",
            report,
            "--info",
            str(info),
            "--baseline-manual",
            "bin/logs/perf_20260906-192816_24828.jsonl",
            "--teleport",
            "false",
            "--tag",
            tag,
            "--expect-product-red",
        ]
    )

# restore pos for next
uj = Path("bin/worlds/World_164/users.json")
data = json.loads(uj.read_text(encoding="utf-8"))
data["Username"]["position"] = [120.0, 57.312599182128906, 56.0]
uj.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")

sys.exit(analyze("bin/suite_reports/phase56_v2_flyheavy.json", "v2-fly"))
