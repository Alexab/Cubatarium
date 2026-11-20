"""Action-bound GPU and normal-color RGB proof for one declared floor hole."""
from __future__ import annotations

import math

from engine_bound_field_evidence import read_bound_field, uint
from engine_rgb_reference import (material_contract, number, RgbContractError,
                                  shade_dry_floor, valid_material_witness)
from engine_source_hole_reference import HoleReference


def evaluate_records(scene, rows, logs, *, action_frame, world_epoch, cell,
                     expected, replacement, initial_extra_emitters=()):
    def result(status, reason, **data):
        return dict(schema='engine-source-hole-draw-reference.v1', status=status, reason=reason,
            scope='One verified glowstone-to-AIR action: declared-voxel light transport, ordinary floor and five new hole faces, same-frame GPU binding and independently witnessed constant opaque normal-color RGB; no roof/wall/whole-screen/wet/transparency/backend/latency acceptance', **data)
    try:
        if (expected!='glowstone' or replacement!='air' or
                not uint(action_frame,positive=True) or not uint(world_epoch,positive=True)):
            return result('UNTESTED','unsupported_hole_source_action')
        sources=scene['lighting_fixture']['emitters']+list(initial_extra_emitters)
        if (not isinstance(cell,list) or len(cell)!=3 or any(type(v) is not int for v in cell) or
                cell not in sources):
            raise ValueError('missing declared initial source')
        models=[HoleReference(scene,sources),
                HoleReference(scene,[e for e in sources if e!=cell],cell)]
        materials,witnesses=material_contract(scene,logs)
        counts=[dict(rows=0,field_checked=0,field_faults=0,field_unjoined=0,missing_bound=0,
                     source_effect=0,rgb_checked=0,rgb_faults=0,unwitnessed=0,geometry_faults=0)
                for _ in range(2)]
        epochs=[set(),set()]; cells=[set(),set()]; levels=[set(),set()]
        emitters=[set(),set()]; field_faces=set(); rgb_faces=set()
        draw_sources=[{},{}]; examples=[]; unordered=0
        for row in rows:
            if row.get('kind')!='renderer_pixel_probe': continue
            frame=row.get('frame_epoch')
            if not uint(frame,positive=True): raise ValueError('missing paint identity')
            if frame==action_frame:
                unordered+=1; continue
            phase=int(frame>action_frame); count=counts[phase]
            count['rows']+=1
            bound=read_bound_field(row)
            if bound is not None and bound['world_epoch']!=world_epoch:
                raise ValueError('foreign GPU world')
            origin=[row.get('renderer_pixel_ray_origin_'+c) for c in 'xyz']
            direction=[row.get('renderer_pixel_ray_direction_'+c) for c in 'xyz']
            surfaces=[model.surface(origin,direction) for model in models]
            surface=surfaces[phase]
            if surface is None: continue
            level=models[phase].block_level(surface.air)
            depth=row.get('renderer_pixel_opaque_hit_distance')
            geometry_ok=(number(depth) and abs(depth-surface.distance)<=.05+surface.distance*.001 and
                row.get('renderer_pixel_opaque_surface_valid')==1 and
                row.get('renderer_pixel_opaque_vertex_light_face_index')==surface.face and
                row.get('renderer_pixel_opaque_vertex_light_block_id')==surface.material)
            count['geometry_faults']+=not geometry_ok
            presentation_ok=(row.get('renderer_pixel_opaque_drawable')==1 and
                row.get('renderer_pixel_opaque_draw_ready')==1 and
                row.get('renderer_pixel_opaque_preview_shader_override')==0)
            field_fault=False
            if bound is None:
                count['missing_bound']+=1
            elif not geometry_ok or not presentation_ok:
                count['field_unjoined']+=1
            else:
                count['field_checked']+=1
                field_fault=bound['packed']!=level<<4
                count['field_faults']+=field_fault
                epochs[phase].add(frame); cells[phase].add(surface.solid); levels[phase].add(level)
                kind=row.get('renderer_pixel_opaque_light_source_kind')
                if type(kind) is not int or kind not in (1,2):
                    raise ValueError('missing actual draw kind')
                draw_sources[phase][str(kind)]=draw_sources[phase].get(str(kind),0)+1
                if not field_fault:
                    if surface.material==376: emitters[phase].add(surface.solid)
                    if surface.hole: field_faces.add(surface.face)
                    other=surfaces[1-phase]
                    if other is not None and models[1-phase].block_level(other.air)!=level:
                        count['source_effect']+=1
            # The shader's dry opaque color curve has no directional face
            # multiplier. Face-dependent UVs do not change constant six-face
            # texels. Independent geometry and face AIR determine the level.
            rgb_fault=False
            if level>0:
                material=materials[surface.material]
                witness=witnesses.get((frame,surface.material))
                if witness is None:
                    count['unwitnessed']+=1
                else:
                    hit=[a+b*surface.distance for a,b in zip(origin,direction)]
                    camera=[row.get('renderer_pixel_camera_pos_'+c) for c in 'xyz']
                    if not all(number(v) for v in camera): raise ValueError('missing render camera')
                    predicted,_,_=shade_dry_floor(row,material['rgba'],level,
                        math.dist(camera,hit),math.hypot(hit[0]-camera[0],hit[2]-camera[2]))
                    actuals=[]
                    for name in ('renderer_pixel_pretransparent_rgba','renderer_pixel_rgba'):
                        packed=row.get(name)
                        if not uint(packed,32): raise ValueError('missing framebuffer RGBA')
                        actuals.append([(packed>>s)&255 for s in (24,16,8,0)])
                    error=max(abs(a-b) for actual in actuals for a,b in zip(actual,predicted))
                    rgb_fault=(not geometry_ok or not valid_material_witness(witness,material) or
                        row.get('renderer_pixel_marker_mode') is not False or
                        row.get('renderer_pixel_marker_visible') is not False or error>2)
                    count['rgb_checked']+=1; count['rgb_faults']+=rgb_fault
                    if not rgb_fault and presentation_ok and surface.hole:
                        rgb_faces.add(surface.face)
            if (field_fault or rgb_fault or not geometry_ok) and len(examples)<12:
                examples.append(dict(frame=frame,solid=surface.solid,face=surface.face,
                    hole=surface.hole,expected_depth=surface.distance,actual_depth=depth,
                    expected_packed=level<<4,actual_packed=bound['packed'] if bound else None,
                    geometry_ok=geometry_ok,field_fault=field_fault,rgb_fault=rgb_fault))
        missing=[]
        for phase,name in enumerate(('before','after')):
            c=counts[phase]
            coverage=dict(field_samples=c['field_checked']>=25,rgb_samples=c['rgb_checked']>=25,
                epochs=len(epochs[phase])>=3,cells=len(cells[phase])>=8,levels=len(levels[phase])>=3,
                emitter_surface=bool(emitters[phase]),source_effect=c['source_effect']>=8,
                requested_bound_fields=c['missing_bound']==0,joined_gpu_surfaces=c['field_unjoined']==0,
                material_witnesses=c['unwitnessed']==0)
            missing += [name+'_'+key for key,ok in coverage.items() if not ok]
        missing += ['after_gpu_hole_face_'+str(f) for f in range(5) if f not in field_faces]
        missing += ['after_rgb_hole_face_'+str(f) for f in range(5) if f not in rgb_faces]
        fault=any(c['field_faults'] or c['rgb_faults'] or c['geometry_faults'] for c in counts)
        return result('FAIL' if fault else 'UNTESTED' if missing else 'PASS',
            'frame_bound_source_hole_transport',action_frame=action_frame,world_epoch=world_epoch,
            counts=dict(zip(('before','after'),counts)),unordered_action_rows=unordered,
            field_hole_faces=sorted(field_faces),rgb_hole_faces=sorted(rgb_faces),
            draw_sources=dict(zip(('before','after'),draw_sources)),
            missing_coverage=missing,examples=examples)
    except RgbContractError as error:
        return result('FAIL',str(error))
    except (ValueError,KeyError,TypeError,AttributeError,OSError,OverflowError):
        return result('FAIL','malformed_source_hole_draw_evidence')
