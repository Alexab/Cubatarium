import json
from pathlib import Path

root = Path(__file__).resolve().parent
files = [
    "cold1.json",
    "cold2.json",
    "cold3.json",
    "cold4.json",
    "cold5.json",
    "warm1.json",
    "warm2.json",
    "far1.json",
]


def find(obj, key, depth=0):
    if depth > 8 or not isinstance(obj, dict):
        return None
    if key in obj:
        return obj[key]
    for v in obj.values():
        if isinstance(v, dict):
            r = find(v, key, depth + 1)
            if r is not None:
                return r
    return None


rows = []
for name in files:
    p = root / name
    if not p.exists():
        rows.append({"file": name, "missing": True})
        print(f"{name:12} MISSING")
        continue
    d = json.loads(p.read_text(encoding="utf-8"))
    metrics = d.get("metrics") or {}
    git = find(d, "git_sha")
    dirty = find(d, "dirty_diff_hash")
    empty_pass = find(d, "empty_world_stop_line_pass")
    enter_pass = find(d, "enter_dirty_residual_stop_line_pass")
    a24_pass = find(d, "a24_safety_stop_line_pass")
    eye_pass = find(d, "eye_proxy_stop_line_pass")
    dual_pass = find(d, "dual_lane_stop_line_pass")
    adeq_pass = find(d, "adequacy_pass")
    nfh = find(d, "near_focus_holes_periods_gt0")
    opaque = find(d, "opaque_cmd_on_med")
    unfinished = metrics.get("unfinished_visual") or find(d, "unfinished_visual")
    vb = metrics.get("visible_black_focus_n") or find(d, "visible_black_focus_n")
    dirty_drop = find(d, "dirty_dropped_per_period")
    far_blocks = find(d, "far_distance_blocks")
    far_flight = find(d, "far_flight")
    cps = find(d, "far_checkpoints_reached")
    demand = find(d, "demand_stop_converged")
    fluid_max = metrics.get("fly_fluid_map_cpu_max")
    src_mm = find(d, "pub_reject_source_mismatch")
    row = {
        "file": name,
        "git_sha": (str(git)[:8] if git else None),
        "dirty": dirty,
        "empty": empty_pass,
        "enter": enter_pass,
        "a24": a24_pass,
        "nfh_gt0": nfh,
        "eye_proxy": eye_pass,
        "dual": dual_pass,
        "adequacy": adeq_pass,
        "opaque_med": opaque,
        "unfinished_med": unfinished,
        "vb_med": vb,
        "dirty_drop_med": dirty_drop,
        "far_flight": far_flight,
        "far_blocks": far_blocks,
        "far_cps": cps,
        "demand_stop": demand,
        "fluid_map_max": fluid_max,
        "src_mismatch": src_mm,
    }
    rows.append(row)
    print(
        f"{name:12} dirty={dirty} empty={empty_pass} enter={enter_pass} "
        f"a24={a24_pass} nfh={nfh} eye={eye_pass} dual={dual_pass} "
        f"opaque={opaque} uf={unfinished} vb={vb} drop={dirty_drop} "
        f"far={far_flight}/{far_blocks} cps={cps}"
    )

out = {
    "head": "177e0739",
    "operator_visual": "UNTESTED",
    "merge_green": False,
    "note": (
        "warm1 invalid (cache_mode cold despite --warmup-sec); "
        "far1 far_flight=false (688 << 8192 checkpoint)"
    ),
    "runs": rows,
}
(root / "matrix_summary.json").write_text(
    json.dumps(out, indent=2), encoding="utf-8"
)
print("wrote", root / "matrix_summary.json")
