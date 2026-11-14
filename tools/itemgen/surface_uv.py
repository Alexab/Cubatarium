"""Deterministic surface-island packing with texel density and mip gutters."""
from __future__ import annotations
from dataclasses import dataclass
import math

from itemgen.mesh import Face


@dataclass(frozen=True)
class Island:
    name: str
    role: str
    rect: tuple[int,int,int,int]
    uv: tuple[tuple[float,float], ...]


def pack(faces: list[Face], *, size: int, texels_per_metre: float, gutter: int = 4, directions=None):
    """Project each face in its own orthonormal frame; never stretch to a tile.

    Islands are not silently rescaled, rotated or mirrored. Overflow is an
    authoring error: change the explicit atlas size or density in the recipe.
    """
    if size not in (128,256,512) or gutter < 2 or not math.isfinite(texels_per_metre) or texels_per_metre <= 0:
        raise ValueError("Invalid atlas dimensions, gutter or texel density")
    if len({f.name for f in faces}) != len(faces):
        raise ValueError("Surface names must be unique")
    directions=directions or {}
    if not isinstance(directions,dict): raise ValueError('UV directions must map material names to vectors')
    for direction in directions.values():
        if not isinstance(direction,(list,tuple)) or len(direction)!=3 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in direction) or sum(v*v for v in direction)<1e-10:
            raise ValueError('UV direction must be a finite nonzero vector')
    projected=[]
    for face in faces:
        n=face.normal()
        edge=tuple(face.points[1][i]-face.points[0][i] for i in range(3))
        if face.role in directions:
            direction=directions[face.role]
            dot=sum(direction[i]*n[i] for i in range(3))
            aligned=tuple(direction[i]-dot*n[i] for i in range(3))
            if sum(v*v for v in aligned)>1e-10: edge=aligned
        length=math.sqrt(sum(x*x for x in edge))
        u=tuple(x/length for x in edge)
        v=(n[1]*u[2]-n[2]*u[1],n[2]*u[0]-n[0]*u[2],n[0]*u[1]-n[1]*u[0])
        points=[(sum(p[i]*u[i] for i in range(3)),sum(p[i]*v[i] for i in range(3))) for p in face.points]
        low=tuple(min(p[i] for p in points) for i in range(2))
        high=tuple(max(p[i] for p in points) for i in range(2))
        w,h=(max(1,math.ceil((high[i]-low[i])*texels_per_metre)+1) for i in range(2))
        projected.append((face,points,low,w,h))
    projected.sort(key=lambda row: (-row[4],-row[3],row[0].name))
    free=[(0,0,size,size)]
    islands=[]
    for face,points,low,w,h in projected:
        width,height=w+2*gutter,h+2*gutter
        candidates=[(min(r[2]-width,r[3]-height),max(r[2]-width,r[3]-height),r[1],r[0],r) for r in free if r[2]>=width and r[3]>=height]
        if not candidates:
            raise ValueError(f"UV atlas overflow at {face.name}; requested {size}px")
        _,_,y,x,_=min(candidates)
        uv=tuple(((x+gutter+.5+(a-low[0])*texels_per_metre)/size,
                  (y+gutter+.5+(b-low[1])*texels_per_metre)/size) for a,b in points)
        islands.append(Island(face.name,face.role,(x+gutter,y+gutter,w,h),uv))
        # MaxRects split: reclaim the tall side spaces left by long tool shafts.
        split=[]
        for fx,fy,fw,fh in free:
            if x>=fx+fw or x+width<=fx or y>=fy+fh or y+height<=fy:
                split.append((fx,fy,fw,fh)); continue
            if x>fx: split.append((fx,fy,x-fx,fh))
            if x+width<fx+fw: split.append((x+width,fy,fx+fw-x-width,fh))
            if y>fy: split.append((fx,fy,fw,y-fy))
            if y+height<fy+fh: split.append((fx,y+height,fw,fy+fh-y-height))
        split=sorted(set(split))
        free=[r for r in split if not any(r!=s and r[0]>=s[0] and r[1]>=s[1] and r[0]+r[2]<=s[0]+s[2] and r[1]+r[3]<=s[1]+s[3] for s in split)]
    return sorted(islands,key=lambda island:island.name)
