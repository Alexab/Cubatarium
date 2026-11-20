"""Motor/look-only pre-edit observation of a declared saved RGB cave."""
from __future__ import annotations
import copy
import math


def build_route(route, scene, plan, saved_eye):
    def vector(value):
        return isinstance(value,list) and len(value)==3 and all(
            type(v) in (int,float) and math.isfinite(v) for v in value)
    try:
        if (route.get('purpose')!='round_trip' or route.get('relative') is not True or
                len(route['waypoints'])!=3 or
                any(not vector(p['position']) for p in route['waypoints']) or
                route['waypoints'][0]['position']!=route['waypoints'][2]['position'] or
                scene['air_border_fixture']['schema']!='engine-positive-air-border-fixture.v1' or
                scene['lighting_fixture']['schema']!='engine-positive-blocklight-fixture.v1' or
                not scene.get('rgb_fixture') or scene.get('expected_eye_in_fluid') is not False or
                scene['lighting_fixture'].get('material_id')!=376 or
                scene['lighting_fixture'].get('emission')!=15 or
                scene['cave_bounds']['floor']!=-48.5 or not vector(scene['starting_eye']) or not vector(saved_eye) or
                saved_eye!=scene['starting_eye'] or
                type(plan.get('after_waypoints')) is not int or plan['after_waypoints']!=1 or
                len(plan['entries'])!=1):
            raise ValueError('invalid source observation contract')
        entry=plan['entries'][0]
        if (entry['expected'] not in ('stone','glowstone') or
                entry['replacement'] not in ('stone','glowstone') or
                entry['expected']==entry['replacement'] or
                not vector(entry['cell']) or any(type(v) is not int for v in entry['cell']) or
                entry['cell'][1:]!=[-49,51] or
                math.dist([v+.5 for v in entry['cell']],saved_eye)>6):
            raise ValueError('source edit must remain in ordinary API reach')
        emitters=scene['lighting_fixture']['emitters']
        if not 2<=len(emitters)<=1024 or any(not vector(e) or
                any(type(v) is not int for v in e) or e[1:]!=[-49,51] for e in emitters):
            raise ValueError('unsupported floor sources')
        result=copy.deepcopy(route)
        result['waypoints'][0]['position']=[0,0,0]
        result['waypoints'][2]['position']=[0,0,0]
        for point in result['waypoints']:
            eye=[a+b for a,b in zip(saved_eye,point['position'])]
            nearest=sorted(emitters,key=lambda e:math.dist(eye,e))[:2]
            x=math.floor(eye[0]+.5)
            targets=[[e[0],-48.5,e[2]] for e in nearest]
            targets += [[x+dx,-48.5,51+dz] for dx,dz in
                        ((-2,-4),(0,-4),(-1,-7),(0,-7),(-2,0),(1,0),(0,0))]
            point['inspect']=True
            point['inspection_targets']=[[a-b for a,b in zip(t,saved_eye)] for t in targets]
        return result
    except (KeyError,TypeError,AttributeError,OverflowError) as error:
        raise ValueError('malformed source observation contract') from error
