"""Derive worn-only armor without changing inventory/FP source models.

The copper/iron inventory mesh contains a cuirass AND two pauldrons. Putting
that entire suit on every arm shrinks the actual protection and duplicates
shoulders. Diagnose complete connected components. --surface-cage builds
an authored, thin hollow beveled shell outside a declared anatomical box cage,
with open limb ends, thickness rims, and a new authored pixel atlas.
Without the flag, export the selected source component unchanged (diagnostic
mode; bounds alone did not pass visual review). Leather is one source piece.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def accessor(doc, folder, index):
    acc = doc['accessors'][index]
    view = doc['bufferViews'][acc['bufferView']]
    fmt = {5121: 'B', 5123: 'H', 5125: 'I', 5126: 'f'}[acc['componentType']]
    count = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[acc['type']]
    size = struct.calcsize('<' + fmt * count)
    data = (folder / doc['buffers'][view['buffer']]['uri']).read_bytes()
    offset = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    return [struct.unpack_from('<' + fmt * count, data,
                               offset + i * view.get('byteStride', size))
            for i in range(acc['count'])]


def components(positions, indices):
    parent = list(range(len(positions)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    welded = {}
    for i, pos in enumerate(positions):
        if pos in welded:
            parent[root(i)] = root(welded[pos])
        else:
            welded[pos] = i
    for k in range(0, len(indices), 3):
        for i in indices[k + 1:k + 3]:
            parent[root(i)] = root(indices[k])
    groups = {}
    for k in range(0, len(indices), 3):
        groups.setdefault(root(indices[k]), []).extend(indices[k:k + 3])
    return list(groups.values())


def surface_cage():
    """Authored thin, beveled, hollow armor shell outside the declared cage.

    Source projection proved body exclusion but retained crossing inner/outer
    legacy layers. A clean wearable has one outer and one inner wall, plus
    open limb ends with thickness rims. Inventory source geometry stays intact.
    """
    outer = [(-1.06,-1.02),(-1.02,-1.06),(1.02,-1.06),(1.06,-1.02),
             (1.06,1.02),(1.02,1.06),(-1.02,1.06),(-1.06,1.02)]
    inner = [(-1.01,-1.005),(-1.005,-1.01),(1.005,-1.01),(1.01,-1.005),
             (1.01,1.005),(1.005,1.01),(-1.005,1.01),(-1.01,1.005)]
    output, out_indices, uvs = [], [], []

    def point(ring, i, y):
        return (ring[i][0], y, ring[i][1])

    def quad(vertices, normal, bevel=False):
        a,b,c = vertices[:3]
        ab = [b[i]-a[i] for i in range(3)]
        ac = [c[i]-a[i] for i in range(3)]
        cross = [ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
        if sum(a*b for a,b in zip(cross,normal)) < 0:
            vertices = list(reversed(vertices))
        axis = max(range(3), key=lambda a: abs(normal[a]))
        face = axis*2 + (normal[axis] > 0)
        axes = [a for a in range(3) if a != axis]
        for order in ((0,1,2),(0,2,3)):
            tri = [vertices[i] for i in order]
            assert any(all(sign*p[a]>=1.-1e-8 for p in tri)
                       for a in range(3) for sign in (-1,1))
            out_indices.extend(range(len(output),len(output)+3))
            output.extend(tri)
            for p in tri:
                u = p[axes[0]]/1.06*.5+.5
                v = p[axes[1]]/1.06*.5+.5
                if axis in (0,2):
                    u=p[2 if axis==0 else 0]/1.06*.5+.5
                    v=1.-(p[1]*.5+.5)
                if bevel:u=1./31.
                uvs.append(((face%3*32+.5+u*31.)/128.,
                            (face//3*32+.5+v*31.)/64.))

    for i in range(8):
        j=(i+1)%8
        direction=(outer[i][0]+outer[j][0],0.,outer[i][1]+outer[j][1])
        bevel=outer[i][0]!=outer[j][0] and outer[i][1]!=outer[j][1]
        quad([point(outer,i,-1),point(outer,j,-1),point(outer,j,1),point(outer,i,1)],direction,bevel)
        quad([point(inner,i,-1),point(inner,j,-1),point(inner,j,1),point(inner,i,1)],tuple(-v for v in direction),bevel)
        for y in (-1,1):
            quad([point(outer,i,y),point(outer,j,y),point(inner,j,y),point(inner,i,y)],(0.,y,0.),True)
    return output,out_indices,uvs


def paint_wear_texture(folder, slot):
    material = folder.name.split('_')[0]
    palette = {
        'copper': ((162, 105, 62), (210, 151, 91), (99, 64, 48), (68, 52, 44)),
        'iron': ((123, 145, 151), (184, 205, 204), (74, 94, 107), (47, 65, 78)),
        'leather': ((105, 75, 53), (151, 112, 71), (65, 48, 39), (44, 35, 29)),
    }
    base, light, shade, dark = palette[material]
    image = Image.new('RGB', (128, 64), base)
    for face in range(6):
        tile = Image.new('RGB', (32, 32), base)
        draw = ImageDraw.Draw(tile)
        draw.rectangle((0, 0, 31, 31), outline=dark, width=1)
        draw.line((1, 1, 30, 1), fill=light, width=2)
        draw.line((1, 2, 1, 29), fill=light)
        draw.line((2, 30, 30, 30), fill=shade, width=2)
        draw.line((30, 2, 30, 29), fill=shade)
        draw.rectangle((4, 4, 27, 27), outline=shade)
        # Restrained, authored seams/rivets and a small angular maker's motif.
        # No random noise or copied creature/legacy texture pixels.
        for x, y in ((4, 4), (27, 4), (4, 27), (27, 27)):
            draw.rectangle((x, y, x + 1, y + 1), fill=light)
        if slot == 'chest':
            draw.polygon(((16, 9), (21, 15), (16, 22), (11, 15)), fill=shade)
            draw.line((16, 11, 18, 15, 16, 19, 14, 15, 16, 11), fill=light)
            draw.line((7, 24, 24, 24), fill=dark)
        elif slot == 'arms':
            draw.line((5, 9, 26, 9), fill=light)
            draw.line((5, 22, 26, 22), fill=dark, width=2)
            draw.rectangle((13, 13, 18, 17), outline=shade)
        else:
            draw.line((9, 6, 9, 25), fill=light)
            draw.line((23, 6, 23, 25), fill=shade)
            draw.polygon(((16, 7), (19, 12), (16, 17), (13, 12)), outline=dark)
        if slot in ('arms','legs'):
            # Narrow skeleton limbs occupy only a few screen pixels. A 32-wide
            # ornament aliases into moving stripes; author broad 8x16 panels
            # and nearest-upscale their atlas tile instead of adding noise.
            small=Image.new('RGB',(8,16),base);d=ImageDraw.Draw(small)
            d.rectangle((0,0,7,15),outline=dark)
            d.line((1,1,6,1),fill=light)
            d.line((1,2,1,13),fill=light)
            d.line((6,2,6,14),fill=shade)
            d.line((2,14,5,14),fill=shade)
            if slot=='arms':
                d.rectangle((3,5,4,7),fill=shade)
                d.line((2,11,5,11),fill=dark)
            else:
                d.line((3,4,3,11),fill=shade)
                d.point((4,5),fill=light)
            tile=small.resize((32,32),Image.Resampling.NEAREST)
        image.paste(tile, (face % 3 * 32, face // 3 * 32))
    image.save(folder / 'wear_texture.png', optimize=False)


def derive(folder, slot, use_cage=False):
    source = folder / 'model.gltf'
    doc = json.loads(source.read_text(encoding='utf8'))
    derived = copy.deepcopy(doc)
    if use_cage:
        derived['accessors'] = []
        derived['bufferViews'] = []
        derived['buffers'] = []
    output_buffer = 0 if use_cage else len(doc['buffers'])
    blob = bytearray()
    rows = []
    for mesh_index, mesh in enumerate(doc['meshes']):
        for prim_index, prim in enumerate(mesh['primitives']):
            assert prim.get('mode', 4) == 4
            pos = accessor(doc, folder, prim['attributes']['POSITION'])
            indices = [x[0] for x in accessor(doc, folder, prim['indices'])]
            groups = components(pos, indices)
            # Authored policy for the three bundled armor sets only. Largest
            # component is their cuirass; metal has separate side pauldrons.
            selected = max(groups, key=len)
            policy = 'cuirass'
            if slot == 'arms' and len(groups) > 1:
                shoulders = [g for g in groups if
                             min(pos[i][0] for i in g) > .25 and
                             min(pos[i][1] for i in g) > -.05]
                assert shoulders, folder
                selected = max(shoulders, key=len)
                policy = 'positive_x_pauldron'
            if use_cage:
                assert set(prim['attributes']) == {'POSITION'}, 'Cage export must not drop authored channels'
                projected, selected, uvs = surface_cage()
                view_index = len(derived['bufferViews'])
                packed_positions = b''.join(struct.pack('<fff', *v) for v in projected)
                derived['bufferViews'].append({'buffer': output_buffer,
                                               'byteOffset': len(blob),
                                               'byteLength': len(packed_positions), 'target': 34962})
                blob.extend(packed_positions)
                acc_index = len(derived['accessors'])
                derived['accessors'].append({'bufferView': view_index, 'componentType': 5126,
                                             'count': len(projected), 'type': 'VEC3',
                                             'min': [min(v[a] for v in projected) for a in range(3)],
                                             'max': [max(v[a] for v in projected) for a in range(3)]})
                derived['meshes'][mesh_index]['primitives'][prim_index]['attributes']['POSITION'] = acc_index
                packed_uvs = b''.join(struct.pack('<ff', *v) for v in uvs)
                uv_view = len(derived['bufferViews'])
                derived['bufferViews'].append({'buffer': output_buffer, 'byteOffset': len(blob),
                                               'byteLength': len(packed_uvs), 'target': 34962})
                blob.extend(packed_uvs)
                uv_accessor = len(derived['accessors'])
                derived['accessors'].append({'bufferView': uv_view, 'componentType': 5126,
                                             'count': len(uvs), 'type': 'VEC2'})
                derived['meshes'][mesh_index]['primitives'][prim_index]['attributes']['TEXCOORD_0'] = uv_accessor
                derived['meshes'][mesh_index]['primitives'][prim_index]['material'] = 0
            packed = struct.pack('<' + 'I' * len(selected), *selected)
            view_index = len(derived['bufferViews'])
            derived['bufferViews'].append({'buffer': output_buffer,
                                           'byteOffset': len(blob),
                                           'byteLength': len(packed), 'target': 34963})
            blob.extend(packed)
            acc_index = len(derived['accessors'])
            derived['accessors'].append({'bufferView': view_index,
                                         'componentType': 5125, 'count': len(selected),
                                         'type': 'SCALAR', 'min': [min(selected)],
                                         'max': [max(selected)]})
            derived['meshes'][mesh_index]['primitives'][prim_index]['indices'] = acc_index
            rows.append({'policy': policy, 'component_count': len(groups),
                         'source_triangles': len(indices) // 3,
                         'worn_triangles': len(selected) // 3,
                         'surface_cage': use_cage,
                         'wear_geometry': 'authored_hollow_beveled_shell' if use_cage else 'selected_source_component',
                         'triangle_plane_exclusion_verified': use_cage})
    derived['buffers'].append({'uri': 'wear_indices.bin', 'byteLength': len(blob)})
    if use_cage:
        assert not doc.get('images'), 'Do not replace an authored source texture'
        paint_wear_texture(folder, slot)
        derived['images'] = [{'uri': 'wear_texture.png'}]
        derived['samplers'] = [{'magFilter': 9728, 'minFilter': 9728, 'wrapS': 33071, 'wrapT': 33071}]
        derived['textures'] = [{'sampler': 0, 'source': 0}]
        derived['materials'] = [{'name': 'Cubatarium fitted ' + folder.name,
                                 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0},
                                                         'metallicFactor': 0., 'roughnessFactor': .8}}]
    derived['asset']['generator'] = 'Cubatarium build_creature_wear_variants.py'
    derived['asset']['extras'] = {'source_model_sha256': digest(source),
                                 'purpose': ('authored worn-only hollow armor shell; original inventory/FP resources retained'
                                             if use_cage else 'worn-only anatomical attachment; original vertices/UV/material retained')}
    (folder / 'wear_indices.bin').write_bytes(blob)
    (folder / 'wear.gltf').write_text(json.dumps(derived, indent=2) + '\n', encoding='utf8')
    wear_path = folder / 'wear.json'
    wear = json.loads(wear_path.read_text(encoding='utf8'))
    wear['model'] = 'wear.gltf'
    if use_cage:
        wear['fit_bounds'] = {'low': [-1, -1, -1], 'high': [1, 1, 1]}
    else:
        wear.pop('fit_bounds', None)
    wear_path.write_text(json.dumps(wear, indent=2) + '\n', encoding='utf8')
    return {'item': folder.name, 'source_model_sha256': digest(source),
            'wear_gltf_sha256': digest(folder / 'wear.gltf'),
            'wear_indices_sha256': digest(folder / 'wear_indices.bin'), 'primitives': rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--surface-cage', action='store_true')
    args = parser.parse_args()
    rows = [derive(ROOT / 'models/items' / f'{material}_{slot}', slot, args.surface_cage)
            for material in ('copper', 'iron', 'leather')
            for slot in ('chest', 'arms', 'legs')]
    out = ROOT / 'docs/research/creature-r6-v2/equipment-fit/wear_derivation.json'
    out.write_text(json.dumps({'schema': 'creature-wear-submesh-derivation.v1',
                               'items': rows}, indent=2) + '\n', encoding='utf8')
    print(f'Derived {len(rows)} worn-only models; source inventory/FP geometry unchanged')


if __name__ == '__main__':
    main()
