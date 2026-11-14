"""Fail-closed checks for the item-only glTF subset before publication/import.

This is a resource contract, not artistic or native acceptance. In particular,
more triangles or a valid atlas never certify a model's visual quality.
"""
from pathlib import Path
import hashlib
import json
import math
import numpy as np
from PIL import Image
from itemgen.raster import load_primitives


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def relative_file(folder, name):
    if not isinstance(name,str) or not name or ':' in name or '\\' in name or Path(name).is_absolute():
        raise ValueError(f'Expected relative resource URI: {name}')
    base=folder.resolve(); path=(base/name).resolve()
    if not path.is_relative_to(base) or path==base or not path.is_file():
        raise ValueError(f'Missing or escaping resource: {name}')
    return path


def validate_model(folder, *, budget):
    folder=Path(folder); g=json.loads((folder/'model.gltf').read_text(encoding='utf-8'))
    def reject_extensions(value):
        if isinstance(value,dict):
            if 'extensions' in value:raise ValueError('glTF extensions are unsupported')
            for key,child in value.items():
                if key!='extras':reject_extensions(child)
        elif isinstance(value,list):
            for child in value:reject_extensions(child)
    reject_extensions(g)
    if g.get('asset',{}).get('version')!='2.0':raise ValueError('Expected glTF 2.0')
    if any(key in g for key in ('skins','animations','cameras','extensions')) or g.get('extensionsRequired') or g.get('extensionsUsed'):
        raise ValueError('Skins, animation clips and extensions are unsupported; use named visual drivers')
    resources={'model.gltf'}; buffers=[]
    for row in g['buffers']:
        path=relative_file(folder,row['uri']); data=path.read_bytes()
        if len(data)!=row['byteLength']:raise ValueError('Buffer byteLength mismatch')
        resources.add(path.relative_to(folder.resolve()).as_posix());buffers.append(data)
    def at(rows,index):
        if isinstance(index,bool) or not isinstance(index,int) or not 0<=index<len(rows):raise ValueError('Invalid nonnegative glTF index')
        return rows[index]
    def accessor(index, shape, kinds=(5126,)):
        row=at(g['accessors'],index); view=at(g['bufferViews'],row['bufferView'])
        if row.get('sparse') or row.get('normalized') or row['type']!=shape or row['componentType'] not in kinds:
            raise ValueError('Unsupported accessor type, normalization or sparse payload')
        dtype=np.dtype({5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[row['componentType']])
        width={'VEC3':3,'VEC2':2,'SCALAR':1}[shape]; count=row['count']
        start=view.get('byteOffset',0); offset=row.get('byteOffset',0); size=view['byteLength']; stride=view.get('byteStride',width*dtype.itemsize)
        if any(isinstance(v,bool) or not isinstance(v,int) for v in (start,offset,size,stride,count)) or min(start,offset,size)<0 or count<=0 or stride<width*dtype.itemsize or offset%dtype.itemsize or start%dtype.itemsize or stride%dtype.itemsize:
            raise ValueError('Invalid accessor layout')
        data=at(buffers,view['buffer'])
        if start+size>len(data) or offset+(count-1)*stride+width*dtype.itemsize>size:
            raise ValueError('Accessor exceeds bufferView')
        result=np.ndarray((count,width),dtype=dtype,buffer=data,offset=start+offset,strides=(stride,dtype.itemsize)).copy()
        if not np.isfinite(result).all():raise ValueError('Non-finite vertex attribute')
        return result
    for image in g['images']:
        if set(image)-{'name','uri','mimeType','extras'}:raise ValueError('Only external PNG albedo images are supported')
        path=relative_file(folder,image['uri'])
        if path.suffix.lower()!='.png':raise ValueError('Item albedo must be PNG')
        with Image.open(path) as im:
            im.load()
            if im.mode not in ('RGB','RGBA') or max(im.size)>4096:raise ValueError('Invalid albedo format or dimensions')
        resources.add(path.relative_to(folder.resolve()).as_posix())
    for texture in g['textures']:
        at(g['images'],texture['source'])
        sampler=at(g['samplers'],texture['sampler'])
        if any(sampler.get(key,default)!=expected for key,default,expected in [('magFilter',9729,9729),('minFilter',9987,9987),('wrapS',10497,33071),('wrapT',10497,33071)]):
            raise ValueError('Item subset requires linear/trilinear clamp sampler; convert other sampling explicitly')
    material_names=[m.get('name') for m in g['materials']]
    if any(not isinstance(name,str) or not name for name in material_names) or len(set(material_names))!=len(material_names):raise ValueError('Unique named materials are required')
    for material in g['materials']:
        if material.get('doubleSided') or material.get('alphaMode','OPAQUE')!='OPAQUE' or any(key in material for key in ('normalTexture','occlusionTexture','emissiveTexture','extensions')):
            raise ValueError('Only opaque albedo materials with scalar properties are supported')
        pbr=material['pbrMetallicRoughness']
        if 'metallicRoughnessTexture' in pbr:raise ValueError('Metallic/roughness maps are unsupported')
        info=pbr['baseColorTexture'];at(g['textures'],info['index'])
        if info.get('texCoord',0)!=0 or 'extensions' in info:raise ValueError('Only untransformed TEXCOORD_0 is supported')
        values=[*pbr.get('baseColorFactor',[1,1,1,1]),*material.get('emissiveFactor',[0,0,0]),pbr.get('metallicFactor',1),pbr.get('roughnessFactor',1)]
        if len(pbr.get('baseColorFactor',[1,1,1,1]))!=4 or len(material.get('emissiveFactor',[0,0,0]))!=3 or any(isinstance(v,bool) or not isinstance(v,(float,int)) or not math.isfinite(v) or not 0<=v<=1 for v in values):
            raise ValueError('Invalid linear material factors')
    triangles=0
    for mesh in g['meshes']:
        if 'weights' in mesh:raise ValueError('Morph mesh weights are unsupported')
        for primitive in mesh['primitives']:
            if primitive.get('mode',4)!=4 or 'targets' in primitive or set(primitive['attributes'])!={'POSITION','NORMAL','TEXCOORD_0'}:
                raise ValueError('Only triangles with POSITION, NORMAL and TEXCOORD_0 are supported')
            attrs=primitive['attributes']; pos=accessor(attrs['POSITION'],'VEC3'); normal=accessor(attrs['NORMAL'],'VEC3'); uv=accessor(attrs['TEXCOORD_0'],'VEC2')
            indices=accessor(primitive['indices'],'SCALAR',(5121,5123,5125)).reshape(-1)
            if len(pos)!=len(normal) or len(pos)!=len(uv) or len(indices)%3 or indices.max()>=len(pos):raise ValueError('Attribute or triangle index mismatch')
            if np.max(np.abs(np.linalg.norm(normal,axis=1)-1))>1e-3:raise ValueError('Authored normals must be unit length')
            if np.any(uv<0) or np.any(uv>1):raise ValueError('Atlas UVs must be in 0..1')
            tri=pos[indices.reshape(-1,3)]; cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0])
            if np.any(np.linalg.norm(cross,axis=1)<1e-12):raise ValueError('Degenerate triangle')
            if np.any(np.sum(cross*normal[indices.reshape(-1,3)].mean(axis=1),axis=1)<=0):raise ValueError('Triangle winding disagrees with normals')
            at(g['materials'],primitive['material'])
            triangles+=len(indices)//3
    names=set(); visited=set()
    def visit(index):
        if index in visited:raise ValueError('Cyclic or shared scene node')
        node=at(g['nodes'],index);visited.add(index);name=node.get('name','')
        if not isinstance(name,str) or not name or name in names:raise ValueError('Unique named nodes are required')
        names.add(name)
        if 'mesh' in node:at(g['meshes'],node['mesh'])
        if set(node)-{'name','children','mesh','matrix','translation','rotation','scale','extras'}:raise ValueError('Unsupported node feature')
        if 'matrix' in node:
            m=np.array(node['matrix'],dtype=float)
            if m.shape!=(16,) or any(k in node for k in ('translation','rotation','scale')) or not np.isfinite(m).all() or np.linalg.det(m.reshape(4,4))<=1e-8 or not np.allclose(m[[3,7,11,15]],[0,0,0,1],atol=1e-7):raise ValueError('Invalid node matrix')
        else:
            t=np.array(node.get('translation',[0,0,0]));r=np.array(node.get('rotation',[0,0,0,1]));s=np.array(node.get('scale',[1,1,1]))
            if t.shape!=(3,) or r.shape!=(4,) or s.shape!=(3,) or not all(np.isfinite(v).all() for v in (t,r,s)) or abs(np.linalg.norm(r)-1)>1e-3 or np.any(s<=0):raise ValueError('Invalid node TRS')
        for child in node.get('children',[]):visit(child)
    for index in at(g['scenes'],g.get('scene',0))['nodes']:visit(index)
    if len(visited)!=len(g['nodes']):raise ValueError('Unreachable scene nodes are unsupported')
    if not triangles or triangles>budget:raise ValueError(f'Triangle budget exceeded or empty: {triangles}/{budget}')
    load_primitives(folder) # Includes world transforms, texture decode and real UV/indices.
    return dict(triangles=triangles,resources=sorted(resources),nodes=sorted(names))
