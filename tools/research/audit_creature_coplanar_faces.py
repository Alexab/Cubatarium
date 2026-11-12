"""Find same-facing coplanar box surfaces in authored rest geometry.

Overlapping solids are useful at joints; overlapping visible faces with the
same normal cause unstable texture ownership. This is a diagnostic, not art QA.
"""
from pathlib import Path
import argparse
import itertools
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from creaturegen.anatomy import build_model
from creaturegen.pipeline import load_recipe


def overlaps(model):
    findings=[]
    for a,b in itertools.combinations(model.cubes(),2):
        for axis in range(3):
            others=[i for i in range(3) if i!=axis]
            if any(min(a.origin[i]+a.size[i],b.origin[i]+b.size[i])-max(a.origin[i],b.origin[i])<=1e-5 for i in others):
                continue
            for upper in (False,True):
                pa=a.origin[axis]+(a.size[axis] if upper else 0)
                pb=b.origin[axis]+(b.size[axis] if upper else 0)
                if abs(pa-pb)<1e-5:
                    findings.append((a.name,b.name,'xyz'[axis],pa,'+' if upper else '-'))
    return findings


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--species',required=True)
    args=parser.parse_args()
    for sid in args.species.split(','):
        found=overlaps(build_model(load_recipe(sid)))
        print(sid,found or 'no same-facing overlapping planes')
