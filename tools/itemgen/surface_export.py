"""glTF 2.0 export of named item surfaces with real normals and unique UVs."""
from __future__ import annotations

import json
from pathlib import Path
import struct

from itemgen.surface_uv import pack
from itemgen.surface_paint import bake


def write_asset(destination: Path, item_id, faces, materials, *, size, density, overlays=None):
    islands=pack(faces,size=size,texels_per_metre=density)
    lookup={i.name:i for i in islands}
    destination.mkdir(parents=True,exist_ok=True)
    bake(islands,materials,size,destination/'textures/diffuse.png',overlays=overlays)
    blob=bytearray(); views=[]; accessors=[]; primitives=[]
    def accessor(rows,width,kind):
        while len(blob)%4: blob.append(0)
        start=len(blob)
        fmt='I' if kind==5125 else 'f'
        for row in rows:
            blob.extend(struct.pack('<'+fmt*width,*row))
        view=len(views)
        views.append(dict(buffer=0,byteOffset=start,byteLength=len(blob)-start))
        a=dict(bufferView=view,componentType=kind,count=len(rows),type={1:'SCALAR',2:'VEC2',3:'VEC3'}[width])
        if width==3:
            a.update(min=[min(r[i] for r in rows) for i in range(3)],max=[max(r[i] for r in rows) for i in range(3)])
        index=len(accessors); accessors.append(a); return index
    roles=sorted({f.role for f in faces})
    for role in roles:
        positions=[]; normals=[]; uv=[]; indices=[]
        for face in faces:
            if face.role!=role: continue
            base=len(positions)
            positions.extend(face.points)
            normals.extend([face.normal()]*len(face.points))
            uv.extend(lookup[face.name].uv)
            for i in range(1,len(face.points)-1): indices.extend((base,base+i,base+i+1))
        primitives.append(dict(attributes=dict(POSITION=accessor(positions,3,5126),NORMAL=accessor(normals,3,5126),TEXCOORD_0=accessor(uv,2,5126)),indices=accessor([(i,) for i in indices],1,5125),material=roles.index(role),mode=4))
    gltf=dict(asset=dict(version='2.0',generator='cubatarium-itemgen-v3'),
              buffers=[dict(uri='model.bin',byteLength=len(blob))],bufferViews=views,accessors=accessors,
              images=[dict(uri='textures/diffuse.png')],samplers=[dict(magFilter=9729,minFilter=9987,wrapS=33071,wrapT=33071)],
              textures=[dict(source=0,sampler=0)],
              materials=[dict(name=role,pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),metallicFactor=materials[role].get('metallic',0),roughnessFactor=materials[role].get('roughness',.85))) for role in roles],
              meshes=[dict(name=item_id,primitives=primitives)],nodes=[dict(mesh=0,name=item_id)],scenes=[dict(nodes=[0])],scene=0,
              extras=dict(authoring_source='cubatarium-surface-design',design_version=3))
    (destination/'model.bin').write_bytes(blob)
    (destination/'model.gltf').write_text(json.dumps(gltf,indent=2)+'\n',encoding='utf-8')
    (destination/'uv_layout.json').write_text(json.dumps(dict(size=size,density=density,islands=[dict(name=i.name,role=i.role,rect=i.rect,uv=i.uv) for i in islands]),indent=2)+'\n',encoding='utf-8')
    return gltf
