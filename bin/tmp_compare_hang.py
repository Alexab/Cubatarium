import json
from pathlib import Path

# Compare hang anatomy: 210431 vs 185830
for name in ["perf_20260919-210431_66120.jsonl", "perf_20260919-185830_34388.jsonl"]:
    p = Path("bin/logs") / name
    pers = []
    for line in p.read_text(encoding="utf-8").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "period":
                pers.append(r)

    def f(r, k):
        v = r.get(k)
        try:
            return float(v) if v is not None else None
        except Exception:
            return None

    hang = [
        (i, r)
        for i, r in enumerate(pers)
        if (f(r, "streamer_update_ms") or 0) > 200
        or (f(r, "max_wall_ms") or 0) > 500
        or (f(r, "wall_ms") or 0) > 500
    ]
    print("====", name, "periods", len(pers), "hang-class", len(hang))
    for i, r in hang:
        print(
            f"  i={i} wall={f(r,'wall_ms')} max_wall={f(r,'max_wall_ms')} "
            f"streamer={f(r,'streamer_update_ms')} max_stream={f(r,'max_stream_ms')} "
            f"phase={f(r,'world_streaming_phase_ms')} emerge={f(r,'mesh_emerge_ms')} "
            f"y={r.get('player_y')} cx={r.get('focus_cx')} spd={r.get('movement_speed')} "
            f"loads={r.get('stream_loads')} load_cand={r.get('load_candidates')} "
            f"near_skip={r.get('near_load_skipped')} ring_block={r.get('ring_gate_blocked')} "
            f"async_q={r.get('async_queued')} disk={r.get('disk_complete')} "
            f"dirty={r.get('dirty_n')} dirty_remesh={r.get('dirty_remesh_n')}"
        )

# Key field presence on 210431 hang
r = None
p = Path("bin/logs/perf_20260919-210431_66120.jsonl")
for line in p.read_text(encoding="utf-8").splitlines():
    if not line.startswith("{"):
        continue
    row = json.loads(line)
    if row.get("kind") == "period" and float(row.get("streamer_update_ms") or 0) > 1000:
        r = row
        break
if r:
    print("\n=== load-loop telem on hang ===")
    for k in sorted(r.keys()):
        if any(
            s in k
            for s in (
                "load",
                "stream",
                "unload",
                "async",
                "disk",
                "gen",
                "complete",
                "near",
                "ring",
                "candidate",
            )
        ):
            print(f"  {k}={r[k]}")
