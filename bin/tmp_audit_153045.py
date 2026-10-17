import json
import statistics
from pathlib import Path

path = Path("bin/logs/perf_20260909-153045_20028.jsonl")
rows = []
for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
    if not line.strip():
        continue
    try:
        rows.append(json.loads(line))
    except Exception:
        pass

# Heuristic: cruise = movement_speed > 1.5 or fly-like
def num(r, *keys, default=None):
    for k in keys:
        if k in r and r[k] is not None:
            try:
                return float(r[k])
            except Exception:
                pass
    return default

cruise = []
allp = []
for r in rows:
    if r.get("type") not in (None, "period", "frame_period") and "wall_ms" not in r and "WallMs" not in r:
        # keep periods-like
        pass
    spd = num(r, "movement_speed", "MovementSpeed", "movement_speed_med", default=0.0) or 0.0
    wall = num(r, "wall_ms", "WallMs", "app_update_ms")
    if wall is None:
        continue
    allp.append(r)
    if spd > 1.5 or r.get("segment") == "fly" or r.get("phase") == "fly":
        cruise.append(r)

# Fallback: if no spd field, take middle 60%
if not cruise and allp:
    n = len(allp)
    cruise = allp[max(0, n // 5) : max(1, (4 * n) // 5)]

def med(vals):
    vals = [v for v in vals if v is not None]
    return statistics.median(vals) if vals else None

def frac(pred):
    if not cruise:
        return None
    return sum(1 for r in cruise if pred(r)) / len(cruise)

keys_interest = [
    ("wall", ("wall_ms", "WallMs")),
    ("emerge", ("mesh_emerge_ms", "MeshEmergeMs", "emerge_ms")),
    ("scene", ("scene_ms", "SceneMs")),
    ("stream", ("stream_ms", "StreamMs", "world_streaming_phase_ms")),
    ("cull", ("scene_opaque_cull_ms", "opaque_cull_ms", "OpaqueCullMs")),
    ("cull_skip", ("opaque_cull_skipped_n", "OpaqueCullSkippedN")),
    ("prep_sched", ("prep_schedule_policy_ms", "PrepSchedulePolicyMs")),
    ("dirty_tick", ("mesh_dirty_tick_ms", "MeshDirtyTickMs")),
    ("transp", ("scene_transparent_ms", "TransparentMs")),
    ("fifo", ("relight_fifo_n", "RelightFifoN", "relight_fifo")),
    ("sched_ok", ("mesh_dirty_schedule_ok_n", "MeshDirtyScheduleOkN")),
    ("softdefer", ("softdefer_stuck_n", "SoftDeferStuckN", "soft_defer_empty_stuck_n")),
    ("holes", ("visual_holes", "VisualHoles", "unfinished_visual")),
    ("focus_miss", ("focus_missing_mesh", "FocusMissingMesh")),
    ("miss_h", ("miss_horiz", "MissHoriz")),
    ("vb", ("visible_black_focus_n", "VisibleBlackFocusN")),
    ("fm", ("dirty_fm_n", "DirtyFmN")),
    ("discard", ("mesh_discarded_late", "MeshDiscardedLate")),
    ("spd", ("movement_speed", "MovementSpeed")),
]

print(f"file={path.name} rows={len(rows)} periods_used={len(allp)} cruise={len(cruise)}")
print("sample keys:", sorted(list(allp[0].keys()))[:40] if allp else None)

for name, ks in keys_interest:
    vals = [num(r, *ks) for r in cruise]
    print(f"{name}: med={med(vals)} max={max([v for v in vals if v is not None], default=None)}")

# hole frac
hf = frac(lambda r: (num(r, "visual_holes", "VisualHoles", "unfinished_visual") or 0) > 0)
fmf = frac(lambda r: (num(r, "focus_missing_mesh", "FocusMissingMesh") or 0) > 0)
print(f"holes_frac={hf} focus_missing_frac={fmf}")
