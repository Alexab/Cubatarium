#!/usr/bin/env python3
import json
from pathlib import Path

rep = json.loads(
    Path("bin/suite_reports/g1_a10_relight/water_walls_p0_cold.json").read_text(
        encoding="utf-8"
    )
)
# flight_sim report shape varies
print("top_keys", list(rep.keys())[:20])
sc = rep.get("scenarios") or rep.get("scenario") or {}
if not sc and "adequacy" in rep:
    sc = {"product-174657": rep}
elif isinstance(sc, dict) and "product-174657" not in sc:
    # maybe flat under results
    for k, v in rep.items():
        if isinstance(v, dict) and "adequacy" in v:
            sc = {k: v}
            break
print("scenario_keys", list(sc.keys())[:10] if isinstance(sc, dict) else type(sc))
s = sc.get("product-174657") if isinstance(sc, dict) else None
if s is None:
    # try nested
    for path in (
        ("results", "product-174657"),
        ("runs", "product-174657"),
        ("product-174657",),
    ):
        cur = rep
        ok = True
        for p in path:
            if isinstance(cur, dict) and p in cur:
                cur = cur[p]
            else:
                ok = False
                break
        if ok and isinstance(cur, dict):
            s = cur
            break
if s is None:
    # dump structure
    def walk(o, pref="", depth=0):
        if depth > 3:
            return
        if isinstance(o, dict):
            for k, v in list(o.items())[:30]:
                print(f"{pref}{k}: {type(v).__name__}")
                if k in (
                    "adequacy",
                    "eye_proxy_stop_line",
                    "dual_lane_stop_line",
                    "metrics",
                ) or (isinstance(v, dict) and depth < 2):
                    walk(v, pref + "  ", depth + 1)

    walk(rep)
    raise SystemExit(1)

eye = s.get("eye_proxy_stop_line") or {}
adeq = s.get("adequacy") or {}
dual = s.get("dual_lane_stop_line") or {}
perf = eye.get("perf_path")
print("perf", perf)
print("adequacy_pass", adeq.get("adequacy_pass"))
print("west", eye.get("west_route_coverage"))
print("eye_pass", eye.get("eye_proxy_stop_line_pass"))
print("dual_pass", dual.get("dual_lane_stop_line_pass"))
print("stale_mid", eye.get("mesh_apply_stale_visual_mid_med"))
print("blink", eye.get("effective_holes_blink_rate"))
print("incomplete", eye.get("publication_incomplete_material_mid_med"))
print("oom", eye.get("publication_oom_retain_mid_med"))
# wall/fog may be at top of scenario
for k in (
    "wall_ms_fly_med",
    "wall_ms_med",
    "fog_pull_in_rd_fly_med",
    "fog_pull_in_rd_changes",
):
    if k in s:
        print(k, s[k])
    elif k in rep:
        print(k, rep[k])

if perf:
    import subprocess
    import sys

    out = "bin/suite_reports/g1_a10_relight/water_walls_p0_cold_score.json"
    r = subprocess.run(
        [
            sys.executable,
            "-X",
            "utf8",
            "tools/n01_v21_scorecard.py",
            perf,
            "-o",
            out,
            "--label",
            "water_walls_p0_cold",
        ],
        check=False,
    )
    print("scorecard_exit", r.returncode)
    if r.returncode == 0:
        score = json.loads(Path(out).read_text(encoding="utf-8"))
        m = score.get("metrics") or score
        for k in (
            "wall_ms_fly_med",
            "wall_ms_med",
            "fog_pull_in_rd_fly_med",
            "pass_dual_backend_same_coord_n_mid_med",
            "pass_dual_backend_same_coord_n_fly_med",
            "pubver_changed_without_fresh_mid_med",
            "mesh_apply_stale_visual_mid_med",
            "publication_incomplete_material_mid_med",
            "publication_oom_retain_mid_med",
            "visible_black_focus_fly_med",
        ):
            if k in m:
                print("score", k, m[k])
        # also search nested
        def find(d, key, path=""):
            if isinstance(d, dict):
                if key in d:
                    print("found", path + key, d[key])
                for kk, vv in d.items():
                    find(vv, key, path + kk + ".")

        for key in (
            "pass_dual_backend_same_coord_n",
            "pubver_changed_without_fresh",
            "fog_pull_in_rd",
        ):
            find(score, key)
