"""Read-only flight audit. Period distributions are NOT per-frame percentiles.

Usage: python -X utf8 docs/streaming/audit_2026_09_21/analyze_current.py
       --output docs/streaming/audit_2026_09_21/metrics.json
"""
import argparse
import collections
import hashlib
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KEYS = '''wall_ms max_wall_ms frames movement_speed player_y focus_cx focus_cz
world_streaming_phase_ms stream_ms update_streaming_ms async_io_ms
mesh_emerge_ms mesh_emerge_prep_ms prep_recover_ms prep_refresh_pressure_ms
mesh_dirty_tick_ms mesh_dirty_schedule_ms mesh_dirty_schedule_ok_remesh_n
mesh_dirty_schedule_ok_fm_n mesh_dirty_schedule_skip_snapshot_n
mesh_gpu_kick_ms mesh_gpu_finish_ms scene_ms scene_opaque_refresh_ms
scene_opaque_cull_ms scene_transparent_ms fluid_map_cpu_ms render_total_ms
prepare_frame_ms gui_overlay_ms scene_overlays_ms scene_self_ms
visible_black_focus_n visible_black_fully_dark_stalled_n stale_vl_rev_n
fully_dark_census_n dark_face_stale_near_n near_focus_holes visual_holes
unfinished_visual softdefer_empty_owned_n prior_lit_hold_n
mark_relit_prefer_kick_n mark_relit_schedule_n mark_relit_invoked_n
pending_gpu_queued_n pending_gpu_kicked_n pending_gpu_applies_n
dirty_fm_n dirty_remesh_n dirty_n relight_fifo_n pending_light_n
column_record_shadow_stage_disagree_n frame_deadline_remaining_ms
publication_incomplete_material_n publication_oom_retain_n
publication_material_block_id_flip_n pass_dual_backend_same_coord_n
gpu_pool_used_mb gpu_pool_cap_mb pool_free_slot_n pool_fence_wait_ms
mesh_apply_stale_geom_delta mesh_apply_stale_light_delta
mesh_apply_stale_accepted_refresh_delta'''.split()
TIMES = [k for k in KEYS if k.endswith('_ms') and k != 'max_wall_ms']


def summarize(rows):
    out = {'n': len(rows)}
    for key in KEYS:
        vals = [r[key] for r in rows if isinstance(r.get(key), (float, int))]
        if vals:
            out[key] = dict(n=len(vals), min=min(vals), median=statistics.median(vals),
                            max=max(vals), last=vals[-1])
    weighted = [r for r in rows if r.get('frames', 0) > 0 and 'wall_ms' in r]
    if weighted:
        out['frame_weighted_wall_mean_ms'] = sum(r['frames'] * r['wall_ms'] for r in weighted) / sum(r['frames'] for r in weighted)
    return out


def analyze(path):
    duplicates = collections.Counter()
    conflicts = collections.Counter()

    def pairs_hook(pairs):
        row = {}
        for k, v in pairs:
            if k in row:
                duplicates[k] += 1
                if row[k] != v:
                    conflicts[k] += 1
            row[k] = v
        return row

    data = path.read_bytes()
    rows = [json.loads(line, object_pairs_hook=pairs_hook) for line in data.decode('utf-8-sig').splitlines() if line.strip()]
    periods = [r for r in rows if r.get('kind') == 'period']
    moving = [r for r in periods if 2 < r.get('movement_speed', 0) < 20]
    corridor = [r for r in moving if -3 <= r.get('focus_cx', 999) <= 6 and r.get('focus_cz') == 3 and 48 <= r.get('player_y', -999) <= 65]
    spikes = sorted([r for r in rows if r.get('kind') == 'spike'], key=lambda r: r.get('wall_ms', 0), reverse=True)[:6]
    return dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(data).hexdigest(),
                kinds=dict(collections.Counter(r.get('kind') for r in rows)),
                duplicate_keys=dict(duplicates), conflicting_duplicate_keys=dict(conflicts),
                sampled_max={k: max(r[k] for r in rows if isinstance(r.get(k), (float, int))) for k in KEYS if any(isinstance(r.get(k), (float, int)) for r in rows)},
                all_periods=summarize(periods), moving=summarize(moving), corridor=summarize(corridor),
                top_spikes=[{k: r[k] for k in TIMES + ['player_y', 'focus_cx', 'movement_speed'] if k in r} for r in spikes],
                timeline=[dict(period=i, **{k: r[k] for k in ['frames','wall_ms','max_wall_ms','focus_cx','focus_cz','player_y','movement_speed','visible_black_focus_n','visible_black_fully_dark_stalled_n','stale_vl_rev_n','dark_face_stale_near_n','unfinished_visual','near_focus_holes','mark_relit_prefer_kick_n','pending_gpu_queued_n','mark_relit_schedule_n'] if k in r}) for i,r in enumerate(periods)])


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    result = {'method': 'JSON duplicate keys inspected; last occurrence matches standard parser. Period means weighted by frames; period medians not frame P50. Movement filter 2<speed<20 excludes initial teleport; corridor additionally x=-3..6,z=3,y=48..65. No causal baseline equivalence assumed.',
              'runs': [analyze(p) for p in sorted((ROOT / 'bin/logs').glob('perf_20260921-*.jsonl'))]}
    serialized = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(serialized + '\n', encoding='utf-8')
    else:
        print(serialized)
    for run in result['runs']:
        s = run['moving']
        get = lambda key, stat='median': s.get(key, {}).get(stat)
        print(Path(run['path']).name, 'moving',s['n'], 'wall',get('wall_ms'), 'maxwall',get('max_wall_ms','max'), 'VB',get('visible_black_focus_n'), 'FDst',get('visible_black_fully_dark_stalled_n'), 'darkmax',get('dark_face_stale_near_n','max'), 'unfinishedmax',get('unfinished_visual','max'), 'duplicates_conflict',run['conflicting_duplicate_keys'])
