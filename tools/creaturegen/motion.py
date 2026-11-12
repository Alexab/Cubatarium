"""Offline contact correction for explicit, keyed authored rigs.

Keeps the lowest sole/toe on the contact plane at authored key times. It does
not infer anatomy or synthesize the gait: rotations and phase remain authored.
"""
from __future__ import annotations

import math
from bisect import bisect_right


def sample_track(track, time):
    """Sample an authored channel as glTF does; rotations use shortest SLERP."""
    times=track['times'];values=track['values']
    index=max(0,min(len(times)-2,bisect_right(times,time)-1))
    if len(times)==1:index=0
    other=min(index+1,len(times)-1)
    span=times[other]-times[index]
    t=max(0,min(1,(time-times[index])/span)) if span else 0
    if track.get('path','rotation')!='rotation':
        return [a+(b-a)*t for a,b in zip(values[index],values[other])]
    from creaturegen.export_gltf import _quaternion_euler
    a=_quaternion_euler(values[index]);b=_quaternion_euler(values[other])
    dot=sum(x*y for x,y in zip(a,b))
    if dot<0:b=tuple(-v for v in b);dot=-dot
    if dot>.9995:
        q=[x+(y-x)*t for x,y in zip(a,b)]
    else:
        theta=math.acos(max(-1,min(1,dot)));den=math.sin(theta)
        q=[(x*math.sin((1-t)*theta)+y*math.sin(t*theta))/den for x,y in zip(a,b)]
    length=math.sqrt(sum(v*v for v in q))
    return [v/length for v in q]


def rotate_quaternion(point,q):
    x,y,z=point;qx,qy,qz,qw=q
    tx,ty,tz=2*(qy*z-qz*y),2*(qz*x-qx*z),2*(qx*y-qy*x)
    return [x+qw*tx+qy*tz-qz*ty,y+qw*ty+qz*tx-qx*tz,z+qw*tz+qx*ty-qy*tx]


def rotate(point, degrees):
    x,y,z = point
    for axis, angle in enumerate(degrees):
        c,s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
        if axis == 0: y,z = y*c-z*s,y*s+z*c
        elif axis == 1: x,z = x*c+z*s,-x*s+z*c
        else: x,y = x*c-y*s,x*s+y*c
    return [x,y,z]


def apply_ground_contact(model):
    by_name = {b.name:b for b in model.bones}
    for name, clip in model.motion.get("clips", {}).items():
        contact = clip.get("contact")
        if not contact:
            continue
        root = by_name[contact["root"]]
        tracks = {(t["bone"],t.get("path","rotation")):t for t in clip["tracks"]}
        root_track = tracks.get((root.name,"translation"))
        if root_track is None:
            raise ValueError(f"{name}: contact correction requires an explicit root translation track")
        times = root_track["times"]
        geometry=contact.get("geometry","effectors")
        if geometry not in ("effectors","skinned_mesh"):
            raise ValueError(f"{name}: unknown contact geometry {geometry!r}")
        if geometry=="skinned_mesh":
            samples=contact.get('samples',len(times))
            if type(samples) is not int or not 2<=samples<=2049:
                raise ValueError(f"{name}: contact samples must be an integer in 2..2049")
            duration=max(t['times'][-1] for t in clip['tracks'])
            times=[round(duration*i/(samples-1),8) for i in range(samples)]
        else:
            for t in clip["tracks"]:
                if t["times"] != times:
                    raise ValueError(f"{name}: contact-corrected tracks must use the same key times")
        contact_bones=list(by_name) if geometry=="skinned_mesh" else contact["bones"]
        corrected = []
        for frame in range(len(times)):
            sampled={key:sample_track(t,times[frame]) for key,t in tracks.items()} if geometry=='skinned_mesh' else {}
            def pose_point(bind, bone):
                p=list(bind)
                while bone:
                    pivot=bone.pivot
                    local=[p[i]-pivot[i] for i in range(3)]
                    scale=tracks.get((bone.name,"scale"))
                    if scale:
                        value=sampled[(bone.name,'scale')] if sampled else scale['values'][frame]
                        local=[local[i]*value[i] for i in range(3)]
                    rotation=tracks.get((bone.name,"rotation"))
                    if rotation:
                        local=rotate_quaternion(local,sampled[(bone.name,'rotation')]) if sampled else rotate(local,rotation['values'][frame])
                    p=[local[i]+pivot[i] for i in range(3)]
                    translation=tracks.get((bone.name,"translation"))
                    if translation and bone.name!=root.name:
                        parent=by_name.get(bone.parent)
                        rest=[pivot[i]-(parent.pivot[i] if parent else 0) for i in range(3)]
                        value=sampled[(bone.name,'translation')] if sampled else translation['values'][frame]
                        p=[p[i]+value[i]*16-rest[i] for i in range(3)]
                    bone=by_name.get(bone.parent)
                return p
            lowest = math.inf
            for foot_name in contact_bones:
                foot = by_name[foot_name]
                for cube in foot.cubes:
                    for ix in (0,1):
                        for iy in (0,1):
                            for iz in (0,1):
                                p = [cube.origin[i]+cube.size[i]*a for i,a in enumerate((ix,iy,iz))]
                                if cube.shape:
                                    if "side_profile" in cube.shape:
                                        corner = cube.shape["side_profile"][iy+iz*2]
                                        p[1] = cube.origin[1]+cube.size[1]*corner[0]
                                        p[2] = cube.origin[2]+cube.size[2]*corner[1]
                                        widths = cube.shape.get("side_width",[1,1])
                                        center = cube.origin[0]+cube.size[0]*.5
                                        p[0] = center+(p[0]-center)*widths[iy]
                                    elif "planform" in cube.shape:
                                        q = cube.shape["planform"]
                                        corner = q[ix+iz*2]
                                        p[0] = cube.origin[0]+cube.size[0]*corner[0]
                                        p[2] = cube.origin[2]+cube.size[2]*corner[1]
                                        if "planform_y" in cube.shape:
                                            p[1] = cube.origin[1]+cube.size[1]*cube.shape["planform_y"][iy][ix+iz*2]
                                    else:
                                        front = cube.shape.get("front_scale",[1,1])
                                        back = cube.shape.get("back_scale",[1,1])
                                        for axis in (0,1):
                                            center = cube.origin[axis]+cube.size[axis]*.5
                                            p[axis] = center+(p[axis]-center)*(front[axis] if iz else back[axis])
                                        dx,dy = cube.shape.get("front_offset",[0,0])
                                        p[0] += dx*iz
                                        p[1] += dy*iz
                                        if "top_scale" in cube.shape:
                                            for axis, factor in zip((0,2),cube.shape["top_scale"]):
                                                center = cube.origin[axis]+cube.size[axis]*.5
                                                p[axis] = center+(p[axis]-center)*(1+iy*(factor-1))
                                bind=list(p)
                                posed=pose_point(bind,foot)
                                if geometry=="skinned_mesh" and cube.blend:
                                    blend=cube.blend
                                    axis={"x":0,"y":1,"z":2}[blend.get("axis","z")]
                                    lo,hi=blend["range"]
                                    t=max(0,min(1,(bind[axis]-lo)/max(hi-lo,1e-8)))
                                    a,b=blend["weights"]
                                    weight=max(0,min(1,a*(1-t)+b*t))
                                    other=pose_point(bind,by_name[blend["bone"]])
                                    posed=[posed[i]*(1-weight)+other[i]*weight for i in range(3)]
                                lowest = min(lowest,posed[1])
            if not math.isfinite(lowest):
                raise ValueError(f"{name}: no contact geometry")
            authored = sampled[(root.name,'translation')] if sampled else root_track["values"][frame]
            corrected.append([authored[0],(root.pivot[1]-lowest)/16,authored[2]])
        root_track["times"] = times
        root_track["values"] = corrected
