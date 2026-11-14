"""Item-only surface geometry. Coordinates are metres, faces are outward CCW.

The module has no dependency on creature buffers or any imported asset pack.
Every face owns its UV island; authoring may deliberately reuse an island later.
"""
from __future__ import annotations

from dataclasses import dataclass
import math


@dataclass(frozen=True)
class Face:
    points: tuple[tuple[float, float, float], ...]
    role: str = "body"
    name: str = "surface"

    def normal(self):
        a, b, c = self.points[:3]
        u = [b[i]-a[i] for i in range(3)]
        v = [c[i]-a[i] for i in range(3)]
        n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        length = math.sqrt(sum(x*x for x in n))
        if length < 1e-10:
            raise ValueError(f"Degenerate face: {self.name}")
        return tuple(x/length for x in n)


def extrude(contour, depth, *, role="body", name="profile"):
    """Extrude a convex CCW XY outline along Z (concave inputs are rejected)."""
    if not math.isfinite(depth) or depth <= 0 or len(contour) < 3:
        raise ValueError("Extrusion needs a positive depth and three points")
    contour = tuple(tuple(float(x) for x in p) for p in contour)
    if any(len(p) != 2 or not all(math.isfinite(x) for x in p) for p in contour):
        raise ValueError("Invalid contour")
    for i, a in enumerate(contour):
        b, c = contour[(i+1)%len(contour)], contour[(i+2)%len(contour)]
        if (b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0]) <= 0:
            raise ValueError("Contour must be strictly convex and CCW")
    front = tuple((x,y,depth/2) for x,y in contour)
    back = tuple((x,y,-depth/2) for x,y in contour)
    result = [Face(front,role,name+'.front'), Face(tuple(reversed(back)),role,name+'.back')]
    for i in range(len(contour)):
        j = (i+1)%len(contour)
        result.append(Face((back[i],back[j],front[j],front[i]),role,name+f'.side{i}'))
    return result


def bevel_box(size, bevel, *, role="body", name="box"):
    """Eight-sided extrusion: trims the four XY corners without microgeometry."""
    x,y,z = (float(v) for v in size)
    if not all(math.isfinite(v) for v in (x,y,z,bevel)) or min(x,y,z) <= 0 or not 0 < bevel < min(x,y)/2:
        raise ValueError("Bevel must fit inside the box")
    x,y=x/2,y/2
    return extrude(((-x+bevel,-y),(x-bevel,-y),(x,-y+bevel),
                    (x,y-bevel),(x-bevel,y),(-x+bevel,y),
                    (-x,y-bevel),(-x,-y+bevel)),z,role=role,name=name)


def blade(contour, depth, edge_depth, bevel_fraction=.18, *, role='body',
          edge_role=None, name='blade'):
    """Convex blade with a thick central web and bevels to its thin edge.

    Both faces are constructed, not painted-on bevels. The web outline is a
    homothetic inset, which stays convex and preserves the authored silhouette.
    """
    extrude(contour,depth,role=role,name=name)  # Validate winding/convexity.
    if not math.isfinite(edge_depth) or not 0<edge_depth<depth:
        raise ValueError('Blade edge thickness must be below web thickness')
    if not math.isfinite(bevel_fraction) or not 0<bevel_fraction<.5:
        raise ValueError('Blade bevel fraction must be between zero and .5')
    cx=sum(p[0] for p in contour)/len(contour)
    cy=sum(p[1] for p in contour)/len(contour)
    inner=[(cx+(x-cx)*(1-bevel_fraction),cy+(y-cy)*(1-bevel_fraction)) for x,y in contour]
    front=tuple((x,y,depth/2) for x,y in inner)
    back=tuple((x,y,-depth/2) for x,y in inner)
    outer_front=tuple((x,y,edge_depth/2) for x,y in contour)
    outer_back=tuple((x,y,-edge_depth/2) for x,y in contour)
    faces=[Face(front,role,name+'.front'),Face(tuple(reversed(back)),role,name+'.back')]
    for i in range(len(contour)):
        j=(i+1)%len(contour)
        edge=edge_role or role
        faces.extend((Face((outer_front[i],outer_front[j],front[j],front[i]),edge,name+f'.bevel{i}'),
                      Face((outer_back[j],outer_back[i],back[i],back[j]),edge,name+f'.back_bevel{i}'),
                      Face((outer_back[i],outer_back[j],outer_front[j],outer_front[i]),edge,name+f'.edge{i}')))
    return faces


def shell(size, thickness, *, role="body", name="shell"):
    """Open rectangular shell with bottom, inner walls and a closed top rim."""
    x,y,z = map(float,size)
    if not all(math.isfinite(v) for v in (x,y,z,thickness)) or not 0 < thickness < min(x,y,z)/2:
        raise ValueError("Invalid shell thickness")
    outer=((-x/2,-z/2),(x/2,-z/2),(x/2,z/2),(-x/2,z/2))
    inner=tuple((a-math.copysign(thickness,a),b-math.copysign(thickness,b)) for a,b in outer)
    def ring(points,h): return tuple((a,h,b) for a,b in points)
    lo,hi=ring(outer,0),ring(outer,y)
    il,ih=ring(inner,thickness),ring(inner,y)
    faces=[Face(lo,role,name+'.bottom'),Face(tuple(reversed(il)),role,name+'.floor')]
    for i in range(4):
        j=(i+1)%4
        faces.extend((Face((lo[i],hi[i],hi[j],lo[j]),role,name+f'.outer{i}'),
                      Face((il[i],il[j],ih[j],ih[i]),role,name+f'.inner{i}'),
                      Face((hi[i],ih[i],ih[j],hi[j]),role,name+f'.rim{i}')))
    return faces


def tube(size, thickness, *, role='body', name='sleeve'):
    """Rectangular armor sleeve, open at both ends with closed wall rims."""
    x,y,z=map(float,size)
    if not all(math.isfinite(v) for v in (x,y,z,thickness)) or not 0<thickness<min(x,y,z)/2:
        raise ValueError('Invalid armor sleeve thickness')
    outer=((-x/2,-z/2),(x/2,-z/2),(x/2,z/2),(-x/2,z/2))
    inner=tuple((a-math.copysign(thickness,a),b-math.copysign(thickness,b)) for a,b in outer)
    def rim(points,h):return tuple((a,h,b) for a,b in points)
    lo,hi=rim(outer,-y/2),rim(outer,y/2)
    il,ih=rim(inner,-y/2),rim(inner,y/2)
    faces=[]
    for i in range(4):
        j=(i+1)%4
        faces.extend((Face((lo[i],hi[i],hi[j],lo[j]),role,name+f'.outer{i}'),
                      Face((il[i],il[j],ih[j],ih[i]),role,name+f'.inner{i}'),
                      Face((hi[i],ih[i],ih[j],hi[j]),role,name+f'.top{i}'),
                      Face((lo[j],il[j],il[i],lo[i]),role,name+f'.bottom{i}')))
    return faces


def transform(faces, *, offset=(0,0,0), rotation=(0,0,0), pivot=(0,0,0), taper=1.0, scale=(1,1,1)):
    """Rotate XYZ in degrees about a pivot; taper XZ from bottom to top."""
    if not all(math.isfinite(v) for values in (offset,rotation,pivot) for v in values) or not math.isfinite(taper) or taper <= 0:
        raise ValueError("Invalid transform")
    if len(scale)!=3 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) or v<=0 for v in scale):
        raise ValueError('Part scale must have three finite positive components')
    ys=[p[1] for f in faces for p in f.points]
    if not ys: return []
    low,high=min(ys),max(ys)
    def point(p):
        taper_scale=1+(taper-1)*(p[1]-low)/max(high-low,1e-8)
        q=[p[i]-pivot[i] for i in range(3)]
        q[0]*=taper_scale; q[2]*=taper_scale
        q=[q[i]*scale[i] for i in range(3)]
        for axis,degrees in enumerate(rotation):
            a,b=(axis+1)%3,(axis+2)%3
            c,s=math.cos(math.radians(degrees)),math.sin(math.radians(degrees))
            q[a],q[b]=q[a]*c-q[b]*s,q[a]*s+q[b]*c
        return tuple(q[i]+pivot[i]+offset[i] for i in range(3))
    return [Face(tuple(point(p) for p in f.points),f.role,f.name) for f in faces]


def triangulate(faces):
    """Return independent flat-shaded triangles, never averaged edge normals."""
    result=[]
    for f in faces:
        n=f.normal()
        for i in range(1,len(f.points)-1):
            result.append((f.name,f.role,(f.points[0],f.points[i],f.points[i+1]),n))
    return result


def revolve(profile, *, sides=8, role='body', name='turned'):
    """Faceted solid from increasing (Y, radius) rings; explicit flat end caps."""
    if not isinstance(sides,int) or not 3<=sides<=16 or len(profile)<2:
        raise ValueError('Revolve requires 3..16 sides and two rings')
    if any(len(p)!=2 or not all(math.isfinite(v) for v in p) or p[1]<=0 for p in profile):
        raise ValueError('Revolve rings need finite positive radii')
    if any(a[0]>=b[0] for a,b in zip(profile,profile[1:])):
        raise ValueError('Revolve rings must increase along Y')
    rings=[tuple((r*math.cos(2*math.pi*i/sides),y,r*math.sin(2*math.pi*i/sides)) for i in range(sides)) for y,r in profile]
    result=[Face(rings[0],role,name+'.bottom'),Face(tuple(reversed(rings[-1])),role,name+'.top')]
    for level,(low,high) in enumerate(zip(rings,rings[1:])):
        for i in range(sides):
            j=(i+1)%sides
            result.append(Face((low[i],high[i],high[j],low[j]),role,name+f'.ring{level}.{i}'))
    return result


def ring(outer, inner, depth, *, sides=8, role='body', name='loop'):
    """Hollow faceted XY loop, including both rims and inner/outer walls."""
    if not all(math.isfinite(v) for v in (outer,inner,depth)) or not 0<inner<outer or depth<=0 or not isinstance(sides,int) or not 3<=sides<=16:
        raise ValueError('Invalid hollow ring dimensions')
    def points(radius,z): return tuple((radius*math.cos(i*2*math.pi/sides),radius*math.sin(i*2*math.pi/sides),z) for i in range(sides))
    of,ob=points(outer,depth/2),points(outer,-depth/2)
    inf,ib=points(inner,depth/2),points(inner,-depth/2)
    result=[]
    for i in range(sides):
        j=(i+1)%sides
        for suffix,p in (('front',(of[i],of[j],inf[j],inf[i])),('back',(ob[j],ob[i],ib[i],ib[j])),('outer',(ob[i],ob[j],of[j],of[i])),('inner',(ib[i],inf[i],inf[j],ib[j]))):
            result.append(Face(p,role,name+f'.{suffix}{i}'))
    return result
