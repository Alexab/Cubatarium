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


def pack(faces: list[Face], *, size: int, texels_per_metre: float, gutter: int = 4):
    """Project each face in its own orthonormal frame; never stretch to a tile.

    Islands are not silently rescaled, rotated or mirrored. Overflow is an
    authoring error: change the explicit atlas size or density in the recipe.
    """
    if size not in (128,256,512) or gutter < 2 or not math.isfinite(texels_per_metre) or texels_per_metre <= 0:
        raise ValueError("Invalid atlas dimensions, gutter or texel density")
    if len({f.name for f in faces}) != len(faces):
        raise ValueError("Surface names must be unique")
    projected=[]
    for face in faces:
        n=face.normal()
        edge=tuple(face.points[1][i]-face.points[0][i] for i in range(3))
        length=math.sqrt(sum(x*x for x in edge))
        u=tuple(x/length for x in edge)
        v=(n[1]*u[2]-n[2]*u[1],n[2]*u[0]-n[0]*u[2],n[0]*u[1]-n[1]*u[0])
        points=[(sum(p[i]*u[i] for i in range(3)),sum(p[i]*v[i] for i in range(3))) for p in face.points]
        low=tuple(min(p[i] for p in points) for i in range(2))
        high=tuple(max(p[i] for p in points) for i in range(2))
        w,h=(max(1,math.ceil((high[i]-low[i])*texels_per_metre)+1) for i in range(2))
        projected.append((face,points,low,w,h))
    projected.sort(key=lambda row: (-row[4],-row[3],row[0].name))
    x=y=row_height=0
    islands=[]
    for face,points,low,w,h in projected:
        width,height=w+2*gutter,h+2*gutter
        if x+width>size:
            x=0; y+=row_height; row_height=0
        if width>size or y+height>size:
            raise ValueError(f"UV atlas overflow at {face.name}; requested {size}px")
        uv=tuple(((x+gutter+.5+(a-low[0])*texels_per_metre)/size,
                  (y+gutter+.5+(b-low[1])*texels_per_metre)/size) for a,b in points)
        islands.append(Island(face.name,face.role,(x+gutter,y+gutter,w,h),uv))
        x+=width; row_height=max(row_height,height)
    return sorted(islands,key=lambda island:island.name)
