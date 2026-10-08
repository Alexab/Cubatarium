import json
import os
import glob


def latest_perf_path():
    logs_dir = r"E:\Work\Home\Cubatarium\bin\logs"
    files = glob.glob(os.path.join(logs_dir, "perf_*.jsonl"))
    if not files:
        return None
    return sorted(files, key=lambda p: os.path.getmtime(p), reverse=True)[0]


def main():
    path = latest_perf_path()
    if not path:
        print("no perf")
        return 2
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        spikes = [json.loads(ln) for ln in f if ln.strip() and ln.strip().startswith("{") and '"kind"' in ln and json.loads(ln).get("kind") == "spike"]

    if not spikes:
        print("no spikes")
        return 1

    def stat(arr):
        arr2 = sorted(arr)
        n = len(arr2)
        return {
            "min": arr2[0],
            "p50": arr2[n // 2],
            "max": arr2[-1],
        }

    underfeet_reason = [s.get("underfeet_reason", 0) for s in spikes]
    underfeet_has_mesh = [s.get("underfeet_has_mesh", 0) for s in spikes]
    underfeet_pending_light = [s.get("underfeet_pending_light", 0) for s in spikes]
    do_movement = [s.get("do_movement_ms", 0.0) for s in spikes]
    phys = [s.get("phys_ms", 0.0) for s in spikes]
    pending_light = [s.get("pending_light", 0) for s in spikes]
    relight_apply_n = [s.get("relight_apply_n", 0) for s in spikes]
    fifo_drop = [s.get("relight_fifo_dropped", 0) for s in spikes]

    from collections import Counter

    c_reason = Counter(underfeet_reason)
    c_has_mesh = Counter(underfeet_has_mesh)

    print("latest:", os.path.basename(path))
    print("spikes:", len(spikes))
    print("underfeet_reason dist:", dict(sorted(c_reason.items())))
    print("underfeet_has_mesh dist:", dict(sorted(c_has_mesh.items())))
    print("underfeet_pending_light spikes ratio:", sum(1 for v in underfeet_pending_light if v == 1), "/", len(spikes))
    print("do_movement_ms stats:", stat(do_movement))
    print("phys_ms stats:", stat(phys))
    print("pending_light stats:", stat(pending_light))
    print("relight_apply_n stats:", stat(relight_apply_n))
    print("relight_fifo_dropped stats:", stat(fifo_drop))


if __name__ == "__main__":
    raise SystemExit(main())

