"""Throw research: deterministic scalar tuning and swept-volume experiments.

Not a gameplay implementation. Native collision/carry/animation acceptance is
recorded separately. All masses are gameplay units, not claims about biology.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
DT=1/60
GRAVITY=20.0

def hands(mass, size, strength=10, carrier_height=1.8):
    dims=sorted(size,reverse=True)
    if mass>2*strength or dims[0]>carrier_height*1.2 or dims[1]>carrier_height*.8:
        return 0
    return 1 if mass<=strength*.5 and dims[0]<=carrier_height*.67 and dims[1]<=carrier_height*.28 else 2

def launch_speed(mass,size,strength=10,charge=1):
    if hands(mass,size,strength)==0:
        return 0.0
    return (12+strength)/math.sqrt(1+mass/4)/(1+.25*max(size)**2)*(.35+.65*max(0,min(1,charge)))

def range_flat(speed, angle=math.pi/4):
    x,y=0.,0.;vx,vy=speed*math.cos(angle),speed*math.sin(angle)
    for step in range(3600):
        nx=x+vx*DT;ny=y+vy*DT-GRAVITY*DT*DT*.5
        if ny<0 and step:
            return x+(nx-x)*y/(y-ny)
        x,y=nx,ny;vy-=GRAVITY*DT
    raise ValueError('flight failed to settle')

def sweep(start,delta,half,center,other_half,other_delta=(0,0,0)):
    """Linear segment vs Minkowski-expanded AABB using relative movement."""
    enter,leave=0.,1.;normal=[0.,0.,0.]
    for axis in range(3):
        origin=start[axis]-center[axis]
        movement=delta[axis]-other_delta[axis]
        extent=half[axis]+other_half[axis]
        if abs(movement)<1e-12:
            if abs(origin)>extent:return None
            continue
        a=(-extent-origin)/movement;b=(extent-origin)/movement
        sign=-1 if movement>0 else 1
        if a>b:a,b=b,a
        if a>enter:
            enter=a;normal=[0.,0.,0.];normal[axis]=sign
        leave=min(leave,b)
        if enter>leave:return None
    return (enter,normal) if 0<=enter<=1 else None

def damage(mass,speed,hardness=1):
    return min(50.,.03*.5*mass*max(0,speed*speed-3*3)*hardness)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--report',type=Path,default=ROOT/'docs/research/throw-2026-11-13/prototype.json')
    args=parser.parse_args()
    profiles=[('small_item',.2,(.2,.2,.2)),('compact_item',2.,(.4,.3,.2)),
              ('bulky_light',2.,(1.,1.,1.)),('wood_block',4.,(1.,1.,1.)),
              ('stone_block',12.,(1.,1.,1.)),('small_creature',2.,(.45,.55,.5)),
              ('medium_creature',11.,(.8,.8,1.2)),('too_heavy',40.,(.8,.8,1.2)),
              ('too_large',2.,(3.,1.,1.))]
    rows=[]
    for name,mass,size in profiles:
        speed=launch_speed(mass,size)
        rows.append(dict(id=name,mass=mass,size=size,hands=hands(mass,size),speed=round(speed,4),
                         range_flat_45=round(range_flat(speed),4) if speed else 0,
                         candidate_wall_damage=round(damage(mass,speed),4)))
    checks={
      'larger_same_mass_reduces_range':rows[2]['range_flat_45']<rows[1]['range_flat_45'],
      'heavier_same_size_reduces_range':rows[4]['range_flat_45']<rows[3]['range_flat_45'],
      'small_creature_one_hand':rows[5]['hands']==1,
      'medium_creature_two_hands':rows[6]['hands']==2,
      'heavy_rejected':rows[7]['hands']==0,
      'large_rejected':rows[8]['hands']==0,
      'rest_has_no_damage':damage(12,0)==0,
      'gentle_contact_has_no_damage':damage(12,2.9)==0,
      'hard_impact_damages_thrown_creature':damage(11,8)>0,
    }
    collision_cases=[]
    for render_fps in (30,60,144):
        for speed in (10.,50.,100.,400.):
            # One physics step can cross a thin wall entirely. Its full volume
            # must still produce time of impact; render FPS is deliberately absent.
            start=(-.5,1.,0.);delta=(speed*DT,0.,0.)
            hit=sweep(start,delta,(.1,.1,.1),(0.,1.,0.),(.02,2.,2.))
            expected=speed*DT>=.38
            collision_cases.append(dict(render_fps=render_fps,speed=speed,hit=hit is not None,expected=expected))
    checks['thin_wall_sweeps']=all(r['hit']==r['expected'] for r in collision_cases)
    moving=sweep((-1,0,0),(2,0,0),(.1,.1,.1),(1,0,0),(.1,.1,.1),(-2,0,0))
    checks['moving_target_relative_sweep']=moving is not None and abs(moving[0]-.45)<1e-6
    checks['off_axis_miss']=sweep((-1,3,0),(2,0,0),(.1,.1,.1),(0,0,0),(.1,.1,.1)) is None
    result=dict(schema='throw-research-prototype.v1',status='offline_only_native_pending',fixed_dt=DT,gravity=GRAVITY,
                tuning_status='candidate_not_native_accepted',profiles=rows,collision_cases=collision_cases,checks=checks,
                passed=all(checks.values()),limitations=['No runtime scene, animation or locomotion validation',
                'AABB linear sweep; rotating shapes require conservative proxy',
                'Render-FPS rows verify shared fixed-step arithmetic only; no frame scheduler is exercised'])
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(report=str(args.report),passed=result['passed'],checks=len(checks))))
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
