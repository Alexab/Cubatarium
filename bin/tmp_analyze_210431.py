import json
from pathlib import Path

p = Path("bin/logs/perf_20260919-210431_66120.jsonl")
pers = []
for line in p.read_text(encoding="utf-8").splitlines():
    if not line.startswith("{"):
        continue
    r = json.loads(line)
    if r.get("kind") == "period":
        pers.append(r)

print("periods", len(pers), "file", p.name)


def f(r, k):
    v = r.get(k)
    try:
        return float(v) if v is not None else None
    except (TypeError, ValueError):
        return None


print("\n=== timeline ===")
for i, r in enumerate(pers):
    print(
        f"{i:2d} cx={r.get('focus_cx')} cz={r.get('focus_cz')} "
        f"y={r.get('player_y')} spd={r.get('movement_speed')} "
        f"wall={f(r,'wall_ms')} streamer={f(r,'streamer_update_ms')} "
        f"phase={f(r,'world_streaming_phase_ms')} emerge={f(r,'mesh_emerge_ms')} "
        f"stream={f(r,'stream_ms')} gen={f(r,'streaming_gen_ms')} "
        f"io={f(r,'streaming_io_ms')}"
    )

print("\n=== TOP wall ===")
tops = sorted(enumerate(pers), key=lambda t: f(t[1], "wall_ms") or 0, reverse=True)[:10]
for i, r in tops:
    print(
        f"i={i} wall={f(r,'wall_ms')} streamer={f(r,'streamer_update_ms')} "
        f"phase={f(r,'world_streaming_phase_ms')} emerge={f(r,'mesh_emerge_ms')} "
        f"stream={f(r,'stream_ms')} gen={f(r,'streaming_gen_ms')} "
        f"io={f(r,'streaming_io_ms')} dirty={r.get('dirty_n')} "
        f"vb={r.get('visible_black_focus_n')} miss={r.get('focus_missing_mesh')} "
        f"unfin={r.get('unfinished_visual')} y={r.get('player_y')} "
        f"cx={r.get('focus_cx')} spd={r.get('movement_speed')}"
    )

# med/max
print("\n=== med/max ===")
for k in [
    "wall_ms",
    "streamer_update_ms",
    "world_streaming_phase_ms",
    "mesh_emerge_ms",
    "stream_ms",
    "streaming_gen_ms",
    "streaming_io_ms",
    "dirty_n",
    "visible_black_focus_n",
]:
    vals = [f(r, k) for r in pers if f(r, k) is not None]
    if not vals:
        print(k, "MISSING")
        continue
    vals.sort()
    print(f"{k}: med={vals[len(vals)//2]:.2f} max={max(vals):.2f} n={len(vals)}")

# end: periods where wall>200 or streamer>200
print("\n=== hang-class periods (wall|streamer|phase > 200) ===")
for i, r in enumerate(pers):
    w, s, ph = f(r, "wall_ms"), f(r, "streamer_update_ms"), f(r, "world_streaming_phase_ms")
    if (w and w > 200) or (s and s > 200) or (ph and ph > 200):
        print(
            f"i={i} wall={w} streamer={s} phase={ph} emerge={f(r,'mesh_emerge_ms')} "
            f"y={r.get('player_y')} cx={r.get('focus_cx')} spd={r.get('movement_speed')} "
            f"dirty={r.get('dirty_n')} loads={r.get('stream_loads')} "
            f"async_q={r.get('async_queued')} disk={r.get('disk_complete')}"
        )

# underwater segment: y < sea ~63?
print("\n=== underwater-ish (y < 54) ===")
uw = [ (i,r) for i,r in enumerate(pers) if f(r,'player_y') is not None and f(r,'player_y') < 54 ]
print("count", len(uw))
for i,r in uw[:15]:
    print(
        f"i={i} y={r.get('player_y')} cx={r.get('focus_cx')} "
        f"vb={r.get('visible_black_focus_n')} dirty={r.get('dirty_n')} "
        f"fog_debt={r.get('fog_hole_debt')} fog_rd={r.get('fog_pull_in_rd')} "
        f"overlay? keys with overlay={[k for k in r if 'overlay' in k]}"
    )

# dump all keys containing stream/load/gen for last hang
if tops:
    i, r = tops[0]
    print("\n=== top hang period ALL numeric-ish keys ===")
    for k in sorted(r.keys()):
        v = r[k]
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            if abs(float(v)) > 0.01 or k in ("wall_ms", "streamer_update_ms"):
                print(f"  {k}={v}")
