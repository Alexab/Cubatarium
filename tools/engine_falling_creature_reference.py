"""Identity-joined physical light at actual falling creature draw samples.
No RGB, GPU primitive visibility or universal latency guarantee is asserted.
"""
from __future__ import annotations
import json
import math
import re
from pathlib import Path

def evaluate(scene: dict, logs: list[Path]) -> dict:
    spec = scene.get('falling_creature')
    if not spec:
        return dict(status='UNTESTED', reason='missing_falling_creature_fixture')
    try:
        identity = spec['identity']
        assert type(identity) is int and identity > 0
        required = [spec[k] for k in ('minimum_samples', 'minimum_sections', 'minimum_vertical_span')]
        assert all((type(v) is int for v in required))
        assert required[0] >= 3 and required[1] >= 2 and (required[2] >= 8)
        lo, hi = scene['chunk_x_bounds']
        zlo, zhi = (scene['plane_bounds']['z_low'], scene['plane_bounds']['z_high'])
        assert type(lo) is int and type(hi) is int and (lo < hi)
        assert all((type(v) in (int, float) and math.isfinite(v) for v in (zlo, zhi))) and zlo < zhi
        speed, ymin, ymax = [spec[k] for k in ('minimum_fall_speed', 'min_probe_y', 'max_probe_y')]
        assert all((type(v) in (int, float) and math.isfinite(v) for v in (speed, ymin, ymax)))
        assert speed >= 30 and ymin < ymax
    except (KeyError, AssertionError, TypeError):
        return dict(status='FAIL', reason='invalid_falling_creature_fixture')
    epoch = None
    paints, rows = ({}, [])
    faults, examples = (0, [])

    def fault(reason):
        nonlocal faults
        faults += 1
        if len(examples) < 8:
            examples.append(reason)
    for path in sorted(logs):
        with path.open(encoding='utf-8', errors='replace') as stream:
            for line in stream:
                if '[FirstWorldFrameWitness]' in line and epoch is None:
                    try:
                        marker = json.loads(line[line.index('{'):])
                        if marker.get('phase') == 'first_world_paint':
                            epoch = marker['world_epoch']
                            if type(epoch) is not int or epoch < 0:
                                raise ValueError('epoch')
                    except (ValueError, KeyError, TypeError):
                        fault('malformed_first_paint')
                elif '[CameraLightSample]' in line or '[SceneLightSample]' in line:
                    fields = dict(re.findall('\\b(\\w+)=([^\\s]+)', line))
                    if '[CameraLightSample]' in line:
                        try:
                            match = re.fullmatch('\\((-?\\d+),(-?\\d+),(-?\\d+)\\)', fields['coord'])
                            if not match:
                                raise ValueError('camera coordinate')
                            paints[int(fields['world_epoch']), int(fields['frame_epoch'])] = tuple(map(int, match.groups()))
                        except (KeyError, ValueError):
                            fault('malformed_paint')
                    elif fields.get('consumer') == 'creature':
                        rows.append(fields)
    samples, frames, sections, heights, seen = (0, set(), set(), [], {})
    missing_identity = skipped = 0
    independent = set()
    for row in rows:
        try:
            key = (int(row['world_epoch']), int(row['frame_epoch']))
            if key[0] != epoch:
                continue
            if not row.get('consumer_identity') or int(row['consumer_identity']) == 0:
                missing_identity += 1
                continue
            if int(row['consumer_identity']) != identity:
                continue

            def triple(name, converter):
                text = row[name]
                if not (text.startswith('(') and text.endswith(')')):
                    raise ValueError(name)
                values = tuple(map(converter, text[1:-1].split(',')))
                if len(values) != 3 or not all((math.isfinite(v) for v in values)):
                    raise ValueError(name)
                return values
            cell, coord = (triple('cell', int), triple('coord', int))
            velocity = triple('velocity', float)
            if min(key) < 0 or coord != tuple((v // 16 for v in cell)):
                raise ValueError('coordinate')
            if not ymin <= cell[1] <= ymax or velocity[1] > -speed or (not lo * 16 <= cell[0] < (hi + 1) * 16) or (not zlo < cell[2] < zhi):
                skipped += 1
                continue
            if key in seen:
                if seen[key] != row:
                    fault('contradictory_actor_frame')
                continue
            seen[key] = row
            physical, ready, state, sky, block, incarnation = [int(row[k]) for k in ('physical', 'ready', 'state', 'sky', 'block_light', 'field_incarnation')]
            if physical not in (0, 1) or ready not in (0, 1) or state not in range(5) or (not 0 <= sky <= 15) or (not 0 <= block <= 15) or (incarnation < 0):
                raise ValueError('field')
        except (KeyError, TypeError, ValueError):
            fault('malformed_target_motion_sample')
            continue
        samples += 1
        frames.add(key)
        sections.add(coord)
        heights.append(cell[1])
        if key not in paints:
            fault('falling_sample_without_world_paint')
        elif max(abs(coord[0] - paints[key][0]), abs(coord[2] - paints[key][2])) > 1:
            independent.add(key)
        if not physical or not ready or state != 2 or (incarnation == 0):
            fault('falling_creature_without_current_computed_field')
        elif sky != 15:
            fault('falling_creature_direct_sky_mismatch')
    span = max(heights) - min(heights) if heights else 0
    coverage = dict(first_paint=epoch is not None, actor_identity=missing_identity == 0, moving_samples=samples >= required[0], moving_frames=len(frames) >= required[0], moving_sections=len(sections) >= required[1], vertical_span=span >= required[2], outside_camera_neighborhood=len(independent) >= required[0])
    return dict(schema='engine-falling-creature-reference.v1', status='FAIL' if faults else 'PASS' if all(coverage.values()) else 'UNTESTED', reason='identity_joined_falling_creature_fields', identity=identity, samples=samples, frames=len(frames), sections=len(sections), vertical_span=span, independent_frames=len(independent), missing_identity_rows=missing_identity, skipped=skipped, faults=faults, examples=examples, missing_coverage=[k for k, v in coverage.items() if not v], scope='declared saved actor identity, measured downward speed, multiple sections and canonical sky15 field at actual draw samples; no GPU issue, RGB or universal delivery guarantee')
