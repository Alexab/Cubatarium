"""Assemble static authored item resources without rebuilding their mesh or pixels.

glTF indices are remapped; buffer/image payloads remain byte-identical. A host
mount supplies animation/visibility. Dependencies are built in private staging,
never read from a potentially stale published catalog or external resource pack.
"""
import copy
import json
import math
import re

import numpy as np
import yaml

from itemgen.artifact_contract import digest


def components(recipe):
    rows = recipe.get('components', [])
    if not isinstance(rows, list):
        raise ValueError('Components must be an array')
    names = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) - {'name', 'item', 'parent', 'source_anchor', 'rotation_deg', 'scale', 'translation'}:
            raise ValueError('Unknown component field')
        for key in ('name', 'item'):
            if not isinstance(row.get(key), str) or not re.fullmatch('[a-z0-9_]+', row[key]):
                raise ValueError('Component name/item must be an item-style ID')
        if row['name'] in names:
            raise ValueError('Duplicate component name')
        names.add(row['name'])
        for key in ('parent', 'source_anchor'):
            if not isinstance(row.get(key), str) or not row[key]:
                raise ValueError('Component requires named parent and source anchor')
        for key, default in (('rotation_deg', [0, 0, 0]), ('scale', [1, 1, 1]), ('translation', [0, 0, 0])):
            value = row.get(key, default)
            if not isinstance(value, list) or len(value) != 3 or any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in value):
                raise ValueError('Invalid component transform vector: ' + key)
            if key == 'scale' and min(value) <= .0001:
                raise ValueError('Component scale must be positive')
    return rows


def source_files(root, item_id):
    """Exact transitive authored source set, including dependent physical fits."""
    result = set()
    visited = set()

    def visit(current, ancestry):
        if current in ancestry:
            raise ValueError('Cyclic item component dependency: ' + ' -> '.join((*ancestry, current)))
        if current in visited:
            return
        folder = root / 'tools/item_designs' / current
        try:
            recipe = yaml.safe_load((folder / 'design.yaml').read_text(encoding='utf-8'))
        except yaml.YAMLError as error:
            raise ValueError('Invalid component source YAML: ' + current) from error
        if not isinstance(recipe, dict) or recipe.get('id') != current or recipe.get('version') != 3:
            raise ValueError('Component requires an authored v3 item: ' + current)
        result.update(p.relative_to(root).as_posix() for p in folder.rglob('*') if p.is_file())
        if current != item_id:
            definition = root / 'content/items' / (current + '.json')
            if not definition.is_file():
                raise ValueError('Component has no gameplay definition: ' + current)
            result.add(definition.relative_to(root).as_posix())
        for row in components(recipe):
            visit(row['item'], (*ancestry, current))
        visited.add(current)

    visit(item_id, ())
    return result


def local_matrix(node):
    if 'matrix' in node:
        return np.asarray(node['matrix'], dtype=float).reshape(4, 4).T
    x, y, z, w = node.get('rotation', [0, 0, 0, 1])
    matrix = np.eye(4)
    matrix[:3, :3] = np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]) @ np.diag(node.get('scale', [1, 1, 1]))
    matrix[:3, 3] = node.get('translation', [0, 0, 0])
    return matrix


def world_matrices(gltf):
    result = {}

    def visit(index, parent):
        node = gltf['nodes'][index]
        matrix = parent @ local_matrix(node)
        result[node['name']] = matrix
        for child in node.get('children', []):
            visit(child, matrix)

    for index in gltf['scenes'][gltf.get('scene', 0)]['nodes']:
        visit(index, np.eye(4))
    return result


def assemble(destination, recipe, fit_scale, build):
    """Return lineage metadata; retain every source buffer and image exactly."""
    rows = components(recipe)
    if not rows:
        return []
    model = destination / 'model.gltf'
    g = json.loads(model.read_text(encoding='utf-8'))
    records = []
    for row in rows:
        name = row['name']
        folder = destination / 'components' / name
        build(row['item'], folder)
        source = json.loads((folder / 'model.gltf').read_text(encoding='utf-8'))
        if source.get('extras', {}).get('visual_bindings'):
            raise ValueError('Component source must be static; animate the host mount')
        parents = [i for i, node in enumerate(g['nodes']) if node['name'] == row['parent']]
        if len(parents) != 1:
            raise ValueError('Component parent is missing or ambiguous: ' + row['parent'])
        matrices = world_matrices(source)
        if row['source_anchor'] not in matrices:
            raise ValueError('Missing component source anchor: ' + row['source_anchor'])
        anchor = matrices[row['source_anchor']][:3, 3]
        x, y, z = np.radians(row.get('rotation_deg', [0, 0, 0]))
        cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
        rotation = np.array([[cy*cz, -cy*sz, sy], [cx*sz+sx*sy*cz, cx*cz-sx*sy*sz, -sx*cy], [sx*sz-cx*sy*cz, sx*cz+cx*sy*sz, cx*cy]])
        mount = np.eye(4)
        # Source meshes are already in fitted physical metres. Only the host's
        # authored translation uses its fit scale; do not refit the arrow.
        mount[:3, :3] = rotation @ np.diag(row.get('scale', [1, 1, 1]))
        mount[:3, 3] = np.asarray(row.get('translation', [0, 0, 0])) * fit_scale - mount[:3, :3] @ anchor
        prefix = 'component_' + name + '__'
        if any(node['name'].startswith(prefix) for node in g['nodes']):
            raise ValueError('Component node namespace collision')
        groups = ('buffers', 'bufferViews', 'accessors', 'images', 'samplers', 'textures', 'materials', 'meshes', 'nodes')
        offsets = {key: len(g.get(key, [])) for key in groups}
        imported = copy.deepcopy(source)
        for key in ('buffers', 'images'):
            for entry in imported[key]:
                entry['uri'] = (folder.relative_to(destination) / entry['uri']).as_posix()
        for entry in imported['bufferViews']:
            entry['buffer'] += offsets['buffers']
        for entry in imported['accessors']:
            entry['bufferView'] += offsets['bufferViews']
        for entry in imported['textures']:
            entry['source'] += offsets['images']
            entry['sampler'] += offsets['samplers']
        for entry in imported['materials']:
            entry['name'] = prefix + entry['name']
            entry['pbrMetallicRoughness']['baseColorTexture']['index'] += offsets['textures']
        for entry in imported['meshes']:
            for primitive in entry['primitives']:
                primitive['attributes'] = {k: v + offsets['accessors'] for k, v in primitive['attributes'].items()}
                primitive['indices'] += offsets['accessors']
                primitive['material'] += offsets['materials']
        for entry in imported['nodes']:
            entry['name'] = prefix + entry['name']
            if 'mesh' in entry:
                entry['mesh'] += offsets['meshes']
            if 'children' in entry:
                entry['children'] = [v + offsets['nodes'] for v in entry['children']]
        if prefix+'mount' in {entry['name'] for entry in imported['nodes']}:
            raise ValueError('Component mount namespace collision')
        for key in groups:
            g.setdefault(key, []).extend(imported.get(key, []))
        mount_index = len(g['nodes'])
        g['nodes'].append(dict(name=prefix+'mount', matrix=mount.T.reshape(-1).tolist(), children=[i+offsets['nodes'] for i in source['scenes'][source.get('scene', 0)]['nodes']]))
        g['nodes'][parents[0]].setdefault('children', []).append(mount_index)
        records.append(dict(name=name, item=row['item'], parent=row['parent'], source_anchor=row['source_anchor'], model_sha256=digest(folder/'model.gltf'), build_sha256=digest(folder/'design_build.json')))
    g['extras']['components'] = records
    model.write_text(json.dumps(g, indent=2)+'\n', encoding='utf-8')
    return records
