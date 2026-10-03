import json
import os
import glob


def parse_perf(path: str):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = [ln for ln in f.read().splitlines() if ln.strip()]
    objs = []
    bad = 0
    for ln in lines:
        try:
            objs.append(json.loads(ln))
        except Exception:
            bad += 1
    spikes = [o for o in objs if o.get("kind") == "spike"]
    shutdowns = [o for o in objs if o.get("kind") == "shutdown"]
    return objs, spikes, shutdowns, bad


def main():
    logs_dir = r"E:\Work\Home\Cubatarium\bin\logs"
    files = glob.glob(os.path.join(logs_dir, "perf_*.jsonl"))
    if not files:
        print("No perf_*.jsonl found")
        return 2
    latest = sorted(files, key=lambda p: os.path.getmtime(p), reverse=True)[0]
    print("latest:", os.path.basename(latest))
    objs, spikes, shutdowns, bad = parse_perf(latest)
    print("lines:", len(objs), "bad_json_lines:", bad, "spikes:", len(spikes))
    if shutdowns:
        s = shutdowns[-1]
        print("shutdown: max_wall_ms=", s.get("max_wall_ms"), "max_phys_ms=", s.get("max_phys_ms"))

    fields = [
        "wall_ms",
        "phys_ms",
        "do_movement_ms",
        "world_streaming_phase_ms",
        "ensure_collision_ms",
        "creature_tick_ms",
        "camera_move_ms",
        "camera_ground_support_ms",
        "camera_locomotion_ms",
        "camera_horiz_move_ms",
        "camera_sync_ms",
        "physics_substeps",
        "underfeet_reason",
        "underfeet_stage",
        "underfeet_has_mesh",
        "underfeet_pending_light",
        "underfeet_need",
        "apply_n",
        "relight_apply_n",
        "pending_light",
        "pending_light_focus",
        "relight_fifo_n",
        "relight_fifo_dropped",
    ]
    # print last few spikes
    for s in spikes[-5:]:
        print("--- spike ---")
        for k in fields:
            if k in s:
                v = s[k]
                if isinstance(v, (int, float)) and "ms" in k:
                    print(f"{k}={v:.3f}")
                else:
                    print(f"{k}={v}")
        print("focus=(", s.get("focus_cx"), ",", s.get("focus_cz"), ")",
              "player=(", s.get("player_x"), ",", s.get("player_z"), ")")


if __name__ == "__main__":
    raise SystemExit(main())

