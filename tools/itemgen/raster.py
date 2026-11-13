"""Deterministic orthographic triangle rasterizer for item asset QA.

Reads actual indexed glTF geometry, node transforms, UVs and base-color maps.
This is an offline geometry check; native FP/TP capture remains a separate gate.
"""
from __future__ import annotations
import base64
import io
import json
import math
from pathlib import Path
import numpy as np
from PIL import Image


def load_primitives(item_dir: Path) -> list[dict]:
    gltf = json.loads((item_dir / 'model.gltf').read_text(encoding='utf-8'))
    def resource(uri):
        if uri.startswith('data:'):
            return base64.b64decode(uri.split(',', 1)[1])
        resolved = (item_dir / uri).resolve()
        if not resolved.is_relative_to(item_dir.resolve()):
            raise ValueError('glTF resource escapes asset directory')
        return resolved.read_bytes()
    buffers = [resource(b['uri']) for b in gltf.get('buffers', [])]
    components = {5120: 'i1', 5121: 'u1', 5122: '<i2', 5123: '<u2', 5125: '<u4', 5126: '<f4'}
    widths = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
    def accessor(index):
        a = gltf['accessors'][index]
        if 'sparse' in a:
            raise ValueError('sparse accessor requires explicit QA support')
        view = gltf['bufferViews'][a['bufferView']]
        dtype = np.dtype(components[a['componentType']])
        width = widths[a['type']]
        offset = view.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = view.get('byteStride', width * dtype.itemsize)
        arr = np.ndarray((a['count'], width), dtype=dtype, buffer=buffers[view['buffer']],
                         offset=offset, strides=(stride, dtype.itemsize)).copy()
        if a.get('normalized') and a['componentType'] != 5126:
            info = np.iinfo(dtype)
            arr = np.maximum(-1, arr.astype(float) / info.max)
        return arr
    def image(index):
        img = gltf['images'][index]
        if 'uri' in img:
            blob = resource(img['uri'])
        else:
            v = gltf['bufferViews'][img['bufferView']]
            start = v.get('byteOffset', 0)
            blob = buffers[v['buffer']][start:start + v['byteLength']]
        return np.asarray(Image.open(io.BytesIO(blob)).convert('RGBA'), dtype=float) / 255
    images = {}
    def local_matrix(node):
        if 'matrix' in node:
            return np.asarray(node['matrix'], dtype=float).reshape(4, 4).T
        x, y, z, w = node.get('rotation', [0, 0, 0, 1])
        r = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                      [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                      [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        m = np.eye(4)
        m[:3, :3] = r @ np.diag(node.get('scale', [1, 1, 1]))
        m[:3, 3] = node.get('translation', [0, 0, 0])
        return m
    parts = []
    def mesh(index, matrix):
        for prim in gltf['meshes'][index]['primitives']:
            if prim.get('mode', 4) != 4:
                raise ValueError('only triangle primitives are accepted')
            pos = accessor(prim['attributes']['POSITION']).astype(float)
            positions = (matrix @ np.c_[pos, np.ones(len(pos))].T).T[:, :3]
            indices = accessor(prim['indices']).reshape(-1).astype(int) if 'indices' in prim else np.arange(len(pos))
            if len(indices) % 3 or not np.isfinite(positions).all():
                raise ValueError('invalid triangle payload')
            uv = accessor(prim['attributes']['TEXCOORD_0']) if 'TEXCOORD_0' in prim['attributes'] else np.zeros((len(pos), 2))
            if indices.size and (indices.min()<0 or indices.max()>=len(positions)):
                raise ValueError('triangle index outside vertex buffer')
            if uv.shape!=(len(pos),2) or not np.isfinite(uv).all():
                raise ValueError('invalid UV payload')
            mat = gltf.get('materials', [{}])[prim.get('material', 0)]
            pbr = mat.get('pbrMetallicRoughness', {})
            factor = np.asarray(pbr.get('baseColorFactor', [1, 1, 1, 1]))
            tex = None
            if 'baseColorTexture' in pbr:
                source = gltf['textures'][pbr['baseColorTexture']['index']]['source']
                if source not in images:
                    images[source] = image(source)
                tex = images[source]
            parts.append(dict(positions=positions, indices=indices.reshape(-1, 3), uv=uv,
                              texture=tex, factor=factor, role=mat.get('name', ''),
                              alpha_cutoff=mat.get('alphaCutoff', .5),
                              double_sided=mat.get('doubleSided', False)))
    nodes = gltf.get('nodes', [])
    def visit(index, parent, ancestry):
        if index in ancestry:
            raise ValueError('cyclic scene nodes')
        node = nodes[index]
        matrix = parent @ local_matrix(node)
        if 'mesh' in node:
            mesh(node['mesh'], matrix)
        for child in node.get('children', []):
            visit(child, matrix, ancestry | {index})
    if nodes:
        scenes = gltf.get('scenes', [])
        roots = scenes[gltf.get('scene', 0)]['nodes'] if scenes else [i for i in range(len(nodes)) if not any(i in n.get('children', []) for n in nodes)]
        for node in roots:
            visit(node, np.eye(4), set())
    else:
        for i in range(len(gltf.get('meshes', []))):
            mesh(i, np.eye(4))
    if not parts:
        raise ValueError('empty glTF scene')
    return parts


def render(primitives: list[dict], *, mode='color', size=160, yaw=35., pitch=-25.) -> Image.Image:
    ya, pi = math.radians(yaw), math.radians(pitch)
    ry = np.array([[math.cos(ya), 0, -math.sin(ya)], [0, 1, 0], [math.sin(ya), 0, math.cos(ya)]])
    rx = np.array([[1, 0, 0], [0, math.cos(pi), -math.sin(pi)], [0, math.sin(pi), math.cos(pi)]])
    rotation = rx @ ry
    vertices = [p['positions'] @ rotation.T for p in primitives]
    bounds = np.concatenate(vertices)
    lower, upper = bounds[:, :2].min(axis=0), bounds[:, :2].max(axis=0)
    scale = size * .76 / max(float((upper-lower).max()), .05)
    center = (upper+lower) * .5
    background = (255, 255, 255, 255) if mode in ('gray', 'silhouette') else (24, 24, 28, 255)
    pixels = np.empty((size, size, 4), dtype=np.uint8)
    pixels[:] = background
    depth = np.full((size, size), -np.inf)
    light = np.array([-.4, .8, 1.])
    light /= np.linalg.norm(light)
    for p, transformed in zip(primitives, vertices):
        screen = transformed.copy()
        screen[:, 0] = (transformed[:, 0]-center[0])*scale+size*.5
        screen[:, 1] = size*.5-(transformed[:, 1]-center[1])*scale
        for ids in p['indices']:
            tri = screen[ids]
            normal = np.cross(transformed[ids[1]]-transformed[ids[0]], transformed[ids[2]]-transformed[ids[0]])
            length = np.linalg.norm(normal)
            if length < 1e-12 or (normal[2] <= 0 and not p['double_sided']):
                continue
            normal /= length
            lo = np.maximum(0, np.floor(tri[:, :2].min(axis=0)).astype(int))
            hi = np.minimum(size-1, np.ceil(tri[:, :2].max(axis=0)).astype(int))
            if (hi < lo).any():
                continue
            xs, ys = np.meshgrid(np.arange(lo[0], hi[0]+1)+.5, np.arange(lo[1], hi[1]+1)+.5)
            a, b, c = tri
            denom = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(denom) < 1e-12:
                continue
            w0 = ((b[1]-c[1])*(xs-c[0])+(c[0]-b[0])*(ys-c[1]))/denom
            w1 = ((c[1]-a[1])*(xs-c[0])+(a[0]-c[0])*(ys-c[1]))/denom
            w2 = 1-w0-w1
            zz = w0*a[2]+w1*b[2]+w2*c[2]
            region = np.s_[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
            mask = (w0 >= -1e-7) & (w1 >= -1e-7) & (w2 >= -1e-7) & (zz > depth[region])
            rgba = np.broadcast_to(p['factor'], (*xs.shape, 4)).copy()
            if p['texture'] is not None:
                uv = w0[..., None]*p['uv'][ids[0]]+w1[..., None]*p['uv'][ids[1]]+w2[..., None]*p['uv'][ids[2]]
                tex = p['texture']
                tx = np.clip((uv[..., 0]*tex.shape[1]).astype(int), 0, tex.shape[1]-1)
                ty = np.clip((uv[..., 1]*tex.shape[0]).astype(int), 0, tex.shape[0]-1)
                rgba *= tex[ty, tx]
            mask &= rgba[..., 3] >= p['alpha_cutoff']
            shade = .65+.35*max(0., float(np.dot(normal, light)))
            rgb = np.clip(rgba[..., :3]*shade*255, 0, 254)
            if mode == 'silhouette':
                rgb[:] = 0
            elif mode == 'gray':
                gray = rgb @ np.array([.2126, .7152, .0722])
                rgb = np.repeat(gray[..., None], 3, axis=-1)
            patch = pixels[region]
            patch[mask, :3] = rgb[mask].astype(np.uint8)
            patch[mask, 3] = 255
            depth[region][mask] = zz[mask]
    return Image.fromarray(pixels)
