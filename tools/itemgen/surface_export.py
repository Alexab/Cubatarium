"""glTF 2.0 export of named item surfaces with real normals and unique UVs."""
from __future__ import annotations

import json
import math
from pathlib import Path
import struct

from itemgen.surface_uv import pack
from itemgen.surface_paint import bake


def write_asset(destination: Path, item_id, faces, materials, *, size, density, overlays=None, presentation=None, nodes=None, face_nodes=None, bindings=None):
    for material in materials.values():
        emissive=material.get('emissive',[0,0,0])
        if not isinstance(emissive,list) or len(emissive)!=3 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) or not 0<=v<=1 for v in emissive):
            raise ValueError('Emissive factor must contain three values in 0..1')
    islands=pack(faces,size=size,texels_per_metre=density,directions={role:m['uv_direction'] for role,m in materials.items() if 'uv_direction' in m})
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
    nodes=nodes or {}; face_nodes=face_nodes or {}
    order=[]; visiting=set()
    def visit(name):
        if name in order: return
        if name in visiting: raise ValueError('Cyclic authored nodes')
        if name not in nodes: raise ValueError(f'Unknown authored node: {name}')
        visiting.add(name)
        parent=nodes[name].get('parent','')
        if parent: visit(parent)
        visiting.remove(name);order.append(name)
    for name in sorted(nodes): visit(name)
    order=['']+order
    for name in face_nodes.values():
        if name not in order: raise ValueError(f'Part references missing node: {name}')
    node_defs=[dict(name=item_id)]
    for name in order[1:]:
        pivot=nodes[name]['pivot']; parent=nodes[name].get('parent','')
        parent_pivot=nodes[parent]['pivot'] if parent else (0,0,0)
        node_defs.append(dict(name=name,translation=[pivot[i]-parent_pivot[i] for i in range(3)]))
    for index,name in enumerate(order[1:],1):
        parent=order.index(nodes[name].get('parent',''))
        node_defs[parent].setdefault('children',[]).append(index)
    meshes=[]
    for node_index,node_name in enumerate(order):
        primitives=[]
        pivot=nodes[node_name]['pivot'] if node_name else (0,0,0)
        for role in roles:
            positions=[]; normals=[]; uv=[]; indices=[]
            for face in faces:
                if face.role!=role or face_nodes.get(face.name,'')!=node_name: continue
                base=len(positions)
                positions.extend(tuple(p[i]-pivot[i] for i in range(3)) for p in face.points)
                normals.extend([face.normal()]*len(face.points))
                uv.extend(lookup[face.name].uv)
                for i in range(1,len(face.points)-1): indices.extend((base,base+i,base+i+1))
            if not positions: continue
            primitives.append(dict(attributes=dict(POSITION=accessor(positions,3,5126),NORMAL=accessor(normals,3,5126),TEXCOORD_0=accessor(uv,2,5126)),indices=accessor([(i,) for i in indices],1,5125),material=roles.index(role),mode=4))
        if primitives:
            node_defs[node_index]['mesh']=len(meshes)
            meshes.append(dict(name=node_name or item_id,primitives=primitives))
    gltf=dict(asset=dict(version='2.0',generator='cubatarium-itemgen-v3'),
              buffers=[dict(uri='model.bin',byteLength=len(blob))],bufferViews=views,accessors=accessors,
              images=[dict(uri='textures/diffuse.png')],samplers=[dict(magFilter=9729,minFilter=9987,wrapS=33071,wrapT=33071)],
              textures=[dict(source=0,sampler=0)],
              materials=[dict(name=role,emissiveFactor=materials[role].get('emissive',[0,0,0]),pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),metallicFactor=materials[role].get('metallic',0),roughnessFactor=materials[role].get('roughness',.85))) for role in roles],
              meshes=meshes,nodes=node_defs,scenes=[dict(nodes=[0])],scene=0,
              extras=dict(authoring_source='cubatarium-surface-design',design_version=3,presentation=presentation or {},visual_bindings=bindings or []))
    (destination/'model.bin').write_bytes(blob)
    (destination/'model.gltf').write_text(json.dumps(gltf,indent=2)+'\n',encoding='utf-8')
    (destination/'uv_layout.json').write_text(json.dumps(dict(size=size,density=density,islands=[dict(name=i.name,role=i.role,rect=i.rect,uv=i.uv) for i in islands]),indent=2)+'\n',encoding='utf-8')
    return gltf
