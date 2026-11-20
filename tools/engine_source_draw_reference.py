"""Action-bound GPU field and independent RGB reference for opaque floor edits.

Stone/glowstone replacement preserves the analytic floor. Source removal to
AIR dispatches to the independent declared-voxel five-face hole reference.
"""
from __future__ import annotations
import copy
import json
import math
from pathlib import Path
import tempfile

from engine_bound_field_evidence import read_bound_field, uint
from engine_fixture_reference import near_cave_surface
from engine_floor_light_reference import evaluate as floor_evaluate, expected_floor_light
from engine_rgb_reference import evaluate as rgb_evaluate, number


def evaluate_records(scene, rows, logs, *, action_frame, world_epoch, cell,
                     expected, replacement, initial_extra_emitters=()):
    if expected=='glowstone' and replacement=='air':
        from engine_source_hole_draw_reference import evaluate_records as evaluate_hole
        return evaluate_hole(scene,rows,logs,action_frame=action_frame,world_epoch=world_epoch,
            cell=cell,expected=expected,replacement=replacement,initial_extra_emitters=initial_extra_emitters)
    def result(status, reason, **data):
        return dict(schema='engine-source-draw-reference.v1', status=status, reason=reason,
            scope='One verified opaque floor source edit, actual same-frame GPU-bound field and independent constant-texture framebuffer RGB; no AIR-hole/wet/transparency/full-backend/latency acceptance', **data)
    try:
        if (not uint(action_frame, positive=True) or not uint(world_epoch, positive=True) or
                expected not in ('stone','glowstone') or replacement not in ('stone','glowstone') or
                expected == replacement):
            return result('UNTESTED','unsupported_source_draw_action')
        if (not isinstance(cell, list) or len(cell)!=3 or any(type(v) is not int for v in cell) or
                cell[1:] != [-49,51] or scene['cave_bounds']['floor'] != -48.5 or
                scene.get('expected_eye_in_fluid') is not False or not scene.get('rgb_fixture')):
            raise ValueError('unsupported geometry')
        sources=[tuple(e) for e in scene['lighting_fixture']['emitters']]+[tuple(e) for e in initial_extra_emitters]
        if (len(set(sources))!=len(sources) or not sources or
                any(len(e)!=3 or any(type(v) is not int for v in e) or e[1:]!=(-49,51) for e in sources) or
                (tuple(cell) in sources)!=(expected=='glowstone')):
            raise ValueError('invalid initial source contract')
        scenes=[copy.deepcopy(scene),copy.deepcopy(scene)]
        after=set(sources)-{tuple(cell)}
        if replacement=='glowstone': after.add(tuple(cell))
        if not after: return result('UNTESTED','no_remaining_reference_emitters')
        for s,emitters in zip(scenes,(sources,sorted(after))):
            s['lighting_fixture']['emitters']=[list(e) for e in emitters]
        counts=[dict(rows=0,source_effect=0,field_checked=0,field_faults=0,field_unjoined=0),
                dict(rows=0,source_effect=0,field_checked=0,field_faults=0,field_unjoined=0)]
        examples=[]
        unordered=0
        with tempfile.TemporaryDirectory(prefix='engine-source-draw-') as temp:
            paths=[Path(temp)/'before.jsonl',Path(temp)/'after.jsonl']
            streams=[p.open('w',encoding='utf-8') for p in paths]
            try:
                for row in rows:
                    if row.get('kind')!='renderer_pixel_probe': continue
                    frame=row.get('frame_epoch')
                    if not uint(frame,positive=True): raise ValueError('missing paint identity')
                    if frame==action_frame:
                        unordered+=1; continue
                    phase=int(frame>action_frame)
                    counts[phase]['rows']+=1
                    streams[phase].write(json.dumps(row)+'\n')
                    bound=read_bound_field(row)
                    if bound is None: continue
                    if bound['world_epoch']!=world_epoch:
                        raise ValueError('foreign GPU world')
                    origin=[row.get('renderer_pixel_ray_origin_'+c) for c in 'xyz']
                    direction=[row.get('renderer_pixel_ray_direction_'+c) for c in 'xyz']
                    if (not all(number(v) for v in origin+direction) or direction[1]>=-1e-6 or
                            abs(sum(v*v for v in direction)-1)>.01): continue
                    depth=near_cave_surface(scene,origin,direction)
                    floor_depth=(scene['cave_bounds']['floor']-origin[1])/direction[1]
                    if depth is None or abs(depth-floor_depth)>.001: continue
                    hit=[a+b*depth for a,b in zip(origin,direction)]
                    x,z=math.floor(hit[0]+.5),math.floor(hit[2]+.5)
                    lo,hi=scene['chunk_x_bounds']
                    if abs(hit[0]-x)>.45 or abs(hit[2]-z)>.45 or not lo*16+16<=x<(hi+1)*16-16: continue
                    levels=[expected_floor_light(s,x,z) for s in scenes]
                    actual_depth=row.get('renderer_pixel_opaque_hit_distance')
                    material=376 if [x,-49,z] in scenes[phase]['lighting_fixture']['emitters'] else 560
                    surface_ok=(number(actual_depth) and abs(actual_depth-depth)<=.05+depth*.001 and
                        row.get('renderer_pixel_opaque_surface_valid')==1 and
                        row.get('renderer_pixel_opaque_vertex_light_face_index')==4 and
                        row.get('renderer_pixel_opaque_vertex_light_block_id')==material and
                        row.get('renderer_pixel_opaque_drawable')==1 and
                        row.get('renderer_pixel_opaque_draw_ready')==1 and
                        row.get('renderer_pixel_opaque_preview_shader_override')==0)
                    if not surface_ok:
                        counts[phase]['field_unjoined']+=1
                        continue
                    counts[phase]['field_checked']+=1
                    fault=bound['packed']!=levels[phase]<<4
                    counts[phase]['field_faults']+=fault
                    if fault and len(examples)<8:
                        examples.append(dict(frame=frame,cell=[x,-49,z],expected=levels[phase]<<4,
                            actual=bound['packed'],surface_ok=surface_ok,bound=bound))
                    if not fault and levels[0]!=levels[1]:
                        counts[phase]['source_effect']+=1
            finally:
                for stream in streams: stream.close()
            components={}
            for name,s,path in zip(('before','after'),scenes,paths):
                components[name]=dict(field=floor_evaluate(s,path),rgb=rgb_evaluate(s,path,logs))
        statuses=[v['status'] for c in components.values() for v in c.values()]
        missing=[name+'_source_effect' for name,c in zip(('before','after'),counts) if c['source_effect']<8]
        missing += [name+'_joined_gpu_surfaces' for name,c in zip(('before','after'),counts) if c['field_unjoined']]
        status='FAIL' if 'FAIL' in statuses or any(c['field_faults'] for c in counts) else 'UNTESTED' if 'UNTESTED' in statuses or missing else 'PASS'
        return result(status,'frame_bound_source_draw_transport',action_frame=action_frame,
            world_epoch=world_epoch,counts=dict(zip(('before','after'),counts)),
            unordered_action_rows=unordered,components=components,missing_coverage=missing,examples=examples)
    except (ValueError,KeyError,TypeError,AttributeError,OSError,OverflowError):
        return result('FAIL','malformed_source_draw_evidence')


def evaluate(scene, artifacts: Path, *, initial_extra_emitters=()):
    """Revalidate actual CPU/action/disk proof before using its action epoch."""
    from engine_source_camera_reference import evaluate as camera_evaluate
    from engine_rgb_reference import json_rows
    try:
        camera=camera_evaluate(scene,artifacts,initial_extra_emitters=initial_extra_emitters)
        if camera['status']!='PASS':
            return dict(schema='engine-source-draw-reference.v1',status=camera['status'],
                        reason='missing_verified_source_camera_contract',camera=camera)
        read=lambda n:json.loads((artifacts/n).read_text(encoding='utf-8'))
        entry=read('bot-route.json')['world_actions']['entries'][0]
        if 'replacement' not in entry:
            return dict(schema='engine-source-draw-reference.v1',status='UNTESTED',reason='missing_source_mutation')
        action=read('world-actions.json')
        logs=sorted((artifacts/'logs').glob('*.log.INFO.*'))
        for log in logs:
            with log.open(encoding='utf-8',errors='replace') as stream:
                for line in stream:
                    marker='[RgbReferenceMaterial] '
                    if marker in line:
                        witness=json.loads(line.split(marker,1)[1])
                        if Path(witness['world_folder']).resolve()!=Path(action['world_folder']).resolve():
                            raise ValueError('foreign runtime material world')
        report=read('flight_sim_report.json')
        return evaluate_records(scene,json_rows(Path(report['perf_jsonl'])),logs,
            action_frame=camera['action_frame'],world_epoch=camera['world_epoch'],cell=entry['cell'],
            expected=entry['expected'],replacement=entry['replacement'],
            initial_extra_emitters=initial_extra_emitters)
    except FileNotFoundError:
        return dict(schema='engine-source-draw-reference.v1',status='UNTESTED',reason='missing_source_draw_artifacts')
    except (ValueError,KeyError,TypeError,AttributeError,OSError,OverflowError):
        return dict(schema='engine-source-draw-reference.v1',status='FAIL',reason='malformed_source_draw_artifacts')
