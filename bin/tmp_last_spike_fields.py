import json
import os
import glob


def latest_perf_path():
    logs_dir = r"E:\Work\Home\Cubatarium\bin\logs"
    files = glob.glob(os.path.join(logs_dir, "perf_*.jsonl"))
    return sorted(files, key=lambda p: os.path.getmtime(p), reverse=True)[0]


def main():
    path = latest_perf_path()
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        spikes = [json.loads(ln) for ln in f if ln.strip() and '"kind":"spike"' in ln]
    s = spikes[-1]
    keys = [
        "wall_ms","phys_ms","do_movement_ms","world_streaming_phase_ms",
        "pending_light","pending_light_focus","relight_apply_n","relight_fifo_dropped",
        "underfeet_reason","underfeet_stage","underfeet_draw_ok","underfeet_has_mesh","underfeet_pending_light",
        "visual_holes","near_focus_holes","unfinished_visual","focus_not_render_ready","focus_pressure",
        "opaque_cmd_total","opaque_cmd_on","opaque_draw_n","opaque_draw_cmds",
    ]
    print("latest file:", os.path.basename(path))
    for k in keys:
        if k in s:
            print(k, s[k])


if __name__ == "__main__":
    raise SystemExit(main())

