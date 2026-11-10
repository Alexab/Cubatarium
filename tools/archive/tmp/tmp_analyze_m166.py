from __future__ import annotations

import json
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
perf_path = ROOT / "bin/logs/perf_20260929-233110_35024.jsonl"
log_path = ROOT / "bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260929-233106.35024"
report_path = ROOT / "bin/suite_reports/engine_refactor/relight_lifecycle_product174657_20260929m166_stale_light_cancel_lease.json"

rows = [json.loads(line) for line in perf_path.read_text(encoding="utf-8").splitlines() if line.startswith("{")]
print("perf_kinds", dict(Counter(r.get("kind", "missing") for r in rows)))
unique = {}
for row in rows:
    if row.get("kind") == "renderer_pixel_probe":
        unique[(row["renderer_pixel_probe_id"], row["renderer_pixel_x"], row["renderer_pixel_y"])] = row
samples = list(unique.values())
depth = [r for r in samples if r.get("renderer_pixel_opaque_surface_valid")]
matched = [r for r in depth if r.get("renderer_pixel_opaque_vertex_light_valid")]
live = [r for r in matched if r.get("renderer_pixel_opaque_live_face_light_valid")]


def packed(r):
    return (round(r["renderer_pixel_opaque_vertex_block_light"] * 15) << 4) | round(r["renderer_pixel_opaque_vertex_sky_light"] * 15)


stale_zero = [r for r in live if packed(r) == 0 and r.get("renderer_pixel_opaque_live_face_light_packed", 0) != 0 and r.get("renderer_pixel_opaque_field_light_rev", 0) > r.get("renderer_pixel_opaque_published_light_rev", 0)]
hit_chunks = {(r.get("renderer_pixel_opaque_chunk_x"), r.get("renderer_pixel_opaque_chunk_y"), r.get("renderer_pixel_opaque_chunk_z")) for r in stale_zero}

events = defaultdict(list)
all_counts = Counter()
marker = "[RelightAudit] "
for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
    if marker not in line:
        continue
    msg = line.split(marker, 1)[1]
    kind = msg.split(" ", 1)[0]
    if kind not in {"slice_handoff", "stale_plan", "dirty_admit", "stale_light_debt"}:
        continue
    coord = re.search(r"coord=\((-?\d+),(-?\d+),(-?\d+)\)", msg)
    if not coord:
        continue
    c = tuple(map(int, coord.groups()))
    row = {k: v for k, v in re.findall(r"([A-Za-z_]+)=([^\s]+)", msg)}
    row["kind"] = kind
    row["coord"] = c
    row["message"] = msg
    all_counts[kind] += 1
    if c in hit_chunks:
        events[c].append(row)

print("pixels", {"unique": len(samples), "depth": len(depth), "matched": len(matched), "live": len(live), "stale_zero_lit": len(stale_zero), "hit_chunks": len(hit_chunks)})
print("probe_fields", {k: v for k, v in (samples[0] if samples else {}).items() if "probe" in k and ("id" in k or "period" in k or "time" in k)})
print("stale_samples_by_probe", dict(sorted(Counter(r.get("renderer_pixel_probe_id") for r in stale_zero).items())))
latest_probe = max((r.get("renderer_pixel_probe_id", 0) for r in samples), default=0)
latest_stale = [r for r in stale_zero if r.get("renderer_pixel_probe_id") == latest_probe]
print("latest_probe_stale_samples", latest_probe, [{k: r.get(k) for k in ("renderer_pixel_x", "renderer_pixel_y", "renderer_pixel_opaque_chunk_x", "renderer_pixel_opaque_chunk_y", "renderer_pixel_opaque_chunk_z", "renderer_pixel_opaque_field_light_rev", "renderer_pixel_opaque_published_light_rev", "renderer_pixel_opaque_live_face_light_packed")} for r in latest_stale])
for kind in ("mesh_schedule_trace", "job_trace", "visual_black_trace", "visual_lifecycle_trace"):
    target_rows = [r for r in rows if r.get("kind") == kind and (r.get("cx"), r.get("cy"), r.get("cz")) == (-13, 2, -3)]
    print("target_trace", kind, "rows", len(target_rows), "keys", sorted(target_rows[0]) if target_rows else [])
    if kind == "job_trace":
        latest_jobs = {}
        for tr in target_rows:
            latest_jobs[tr.get("job_id")] = tr
        print("target_job_latest", [{k: tr.get(k) for k in ("job_id", "stage", "outcome", "terminal_reason", "source_rev", "source_light_rev", "published_rev", "published_light_rev", "elapsed_ms", "stage_ms", "queue_reason", "attempt_id", "created_ms")} for tr in latest_jobs.values()])
    for tr in target_rows[-8:]:
        print("target_trace_row", kind, {k: tr.get(k) for k in ("frame", "frame_id", "kind", "stage", "outcome", "cause", "flags", "mesh_work_owner_flags", "dirty_queue_index", "dirty_queue_size", "remesh_queue_index", "relight_queue_index", "focus_cx", "focus_cz", "horiz", "mesh_revision", "field_light_rev", "meshed_light_rev", "published_light_rev", "job_id", "attempt_id") if k in tr})
print("trace_rows", dict(all_counts), "hit_chunks_with_trace", len(events))
debt_rows = [r for values in events.values() for r in values if r["kind"] == "stale_light_debt"]
debt_by_chunk = defaultdict(list)
for row in debt_rows:
    debt_by_chunk[row["coord"]].append(row)
print("stale_light_debt", {"events": dict(Counter(r.get("event", "missing") for r in debt_rows)), "chunks": len(debt_by_chunk), "latest_probe_chunks": sorted({(r.get("renderer_pixel_opaque_chunk_x"), r.get("renderer_pixel_opaque_chunk_y"), r.get("renderer_pixel_opaque_chunk_z")) for r in latest_stale})})
latest_stale_coords = {(r.get("renderer_pixel_opaque_chunk_x"), r.get("renderer_pixel_opaque_chunk_y"), r.get("renderer_pixel_opaque_chunk_z")) for r in latest_stale}
for coord in sorted(latest_stale_coords):
    print("latest_hit_debt", coord, [{k: r.get(k) for k in ("event", "field_light_rev", "meshed_light_rev", "published_light_rev", "dirty", "raa", "mesh_inflight", "gpu_owned", "backlog")} for r in debt_by_chunk.get(coord, [])])
for coord in sorted(debt_by_chunk):
    values = debt_by_chunk[coord]
    last = values[-1]
    print("debt_chunk_final", coord, dict(Counter(r.get("event", "missing") for r in values)), {k: last.get(k) for k in ("event", "field_light_rev", "meshed_light_rev", "published_light_rev", "dirty", "raa", "mesh_inflight", "gpu_owned", "backlog")})
for kind in ("slice_handoff", "stale_plan", "dirty_admit"):
    selected = [r for values in events.values() for r in values if r["kind"] == kind]
    print(kind, {"rows": len(selected), "chunks": len({r['coord'] for r in selected})})
    if kind == "stale_plan":
        print("stale_plan_by_result", {k: dict(Counter(r.get(k, "missing") for r in selected)) for k in ("path", "dirty_priority", "dirty_normal", "consume", "primary_only", "focus_horiz")})
    if kind == "dirty_admit":
        print("dirty_admit_by_result", {k: dict(Counter(r.get(k, "missing") for r in selected)) for k in ("result", "priority", "horiz", "durable_owner", "invalidation_backlog")})

print("hit_chunk_latest_states")
for coord in sorted(hit_chunks):
    values = events.get(coord, [])
    row = {kind: next((r for r in reversed(values) if r["kind"] == kind), None) for kind in ("slice_handoff", "stale_plan", "dirty_admit")}
    print(coord, {kind: None if r is None else {k: r.get(k) for k in ({"slice_handoff": ("job", "installed", "primary_only", "markrelit_input", "field_light_rev", "meshed_light_rev", "dirty", "pending_light"), "stale_plan": ("path", "dirty_priority", "dirty_normal", "schedule_n", "dark", "field_light_rev", "meshed_light_rev", "dirty_before", "focus_horiz", "consume", "primary_only", "force_stale", "light_repair_once"), "dirty_admit": ("result", "priority", "horiz", "focus", "field_light_rev", "meshed_light_rev", "durable_owner", "invalidation_backlog", "dirty")}[kind])} for kind, r in row.items()})

report = json.loads(report_path.read_text(encoding="utf-8"))
print("metrics", {k: report.get("metrics", {}).get(k) for k in ("holes_rate", "fly_visible_black_max", "wall_ms_fly_med", "effective_fps_fly", "relight_apply_partial_frames", "cruise_fifo_dropped_delta", "cruise_false_clear_delta")})
dependency_rows = [r for r in rows if "mesh_dependency_applied_n" in r]
print("mesh_dependency", {key: {"sum": sum(r.get(key, 0) for r in dependency_rows), "max": max((r.get(key, 0) for r in dependency_rows), default=0), "last": dependency_rows[-1].get(key) if dependency_rows else None} for key in ("mesh_dependency_queued_n", "mesh_dependency_applied_n", "mesh_dependency_backlog_n")})
print("flight", {k: report.get(k) for k in ("pass", "process_rc", "run_outcome", "periods")})
