"""Validate anatomical item sections; convert bounds with the physical fit."""
import copy
import math


def prepare_sections(rows,nodes,scale,centre,slots):
    if not isinstance(rows,list):raise ValueError('Wear sections must be a list')
    result=copy.deepcopy(rows);assigned=set()
    for row in result:
        if not isinstance(row,dict) or set(row)-{'node','bones','slot','region','low','high','euler_deg'}:
            raise ValueError('Unknown wear section field')
        slot=row.get('slot')
        if isinstance(slot,bool) or not isinstance(slot,int) or not 0<=slot<4 or ('head','chest','legs','feet')[slot] not in slots:
            raise ValueError('Wear section is incompatible with the armor slot')
        if row.get('node') not in nodes:raise ValueError('Wear section references unknown node')
        bones=row.get('bones')
        if not isinstance(bones,list) or not bones or any(not isinstance(b,str) or not b for b in bones):raise ValueError('Wear section needs bone names')
        for bone in bones:
            if (slot,bone) in assigned:raise ValueError('Duplicate wear bone assignment')
            assigned.add((slot,bone))
        if row.get('region','slot') not in ('slot','full','waist'):raise ValueError('Unknown wear region')
        euler=row.get('euler_deg',[0,0,0])
        if not isinstance(euler,list) or len(euler)!=3 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in euler):raise ValueError('Invalid wear orientation')
        for key in ('low','high'):
            values=row.get(key)
            if not isinstance(values,list) or len(values)!=3 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in values):raise ValueError('Invalid wear reference bounds')
        if any(a>=b for a,b in zip(row['low'],row['high'])):raise ValueError('Empty wear reference volume')
        for key in ('low','high'):row[key]=[(row[key][i]-centre[i])*scale for i in range(3)]
    return result
