"""Shared authored components preserve resources, anchors and dependency lineage."""
import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest

import numpy as np
import yaml

from itemgen.components import world_matrices
from itemgen.design import ROOT, build_design
from itemgen.design_publication import _hashes, validate_design, publish_designs
from itemgen.manifest import validate_manifest
from itemgen.raster import load_primitives


class ComponentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('arrow', 'wood_bow'):
            shutil.copytree(ROOT/'tools/item_designs'/name, self.root/'tools/item_designs'/name)
            target = self.root/'content/items'/f'{name}.json'
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT/'content/items'/f'{name}.json', target)
        shutil.copytree(ROOT/'tools/itemgen', self.root/'tools/itemgen', ignore=shutil.ignore_patterns('__pycache__'))
        for name in ('tools/item_designs/provenance.json', 'LICENSE', 'shaders/item_surface.vert', 'shaders/item_surface.frag'):
            target = self.root/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT/name, target)

    def change(self, id, edit):
        path = self.root/'tools/item_designs'/id/'design.yaml'
        recipe = yaml.safe_load(path.read_text(encoding='utf-8'))
        edit(recipe)
        path.write_text(yaml.safe_dump(recipe, sort_keys=False), encoding='utf-8')

    def test_exact_source_buffers_pixels_uvs_and_nock_alignment(self):
        before = _hashes(self.root/'tools/item_designs')
        bow = build_design('wood_bow', root=self.root)
        arrow = build_design('arrow', root=self.root)
        embedded = bow/'components/loaded_arrow'
        self.assertEqual(_hashes(arrow), _hashes(embedded))
        self.assertEqual(before, _hashes(self.root/'tools/item_designs'))
        report = validate_design('wood_bow', root=self.root)
        self.assertIn('tools/item_designs/arrow/design.yaml', report['record']['sources'])
        self.assertIn('content/items/arrow.json', report['record']['sources'])
        g = json.loads((bow/'model.gltf').read_text())
        imported = [p for p in load_primitives(bow) if p['node_name'].startswith('component_loaded_arrow__')]
        original = load_primitives(arrow)
        self.assertEqual(len(imported), len(original))
        for actual, source in zip(imported, original):
            np.testing.assert_array_equal(actual['indices'], source['indices'])
            np.testing.assert_array_equal(actual['uv'], source['uv'])
            np.testing.assert_array_equal(actual['texture'], source['texture'])
        # Pulling the bow moves the source nock exactly with the string's pull
        # anchor. The source physical length is preserved, not refitted to bow.
        length = np.ptp(np.concatenate([p['positions'] for p in original]), axis=0)[1]
        for charge in (0, .5, 1):
            pose = copy.deepcopy(g)
            pull = next(n for n in pose['nodes'] if n['name'] == 'pull')
            pull['translation'][2] += .2 * report['record']['physical_fit']['scale'] * charge
            matrices = world_matrices(pose)
            np.testing.assert_allclose(matrices['component_loaded_arrow__nock'][:3, 3], matrices['pull'][:3, 3], atol=1e-8)
        actual_length = np.ptp(np.concatenate([p['positions'] for p in imported]), axis=0)[2]
        self.assertAlmostEqual(actual_length, length, places=7)
        first = _hashes(bow)
        build_design('wood_bow', root=self.root)
        self.assertEqual(first, _hashes(bow))

    def test_dependency_edit_rejects_stale_host_and_rebuild_updates_component(self):
        bow = build_design('wood_bow', root=self.root)
        old = _hashes(bow)
        self.change('arrow', lambda r: r['materials']['feather']['color'].__setitem__(0, 80))
        with self.assertRaisesRegex(ValueError, 'Stale sources'):
            validate_design('wood_bow', root=self.root)
        self.assertEqual(_hashes(bow), old)
        build_design('wood_bow', root=self.root)
        self.assertNotEqual(old, _hashes(bow))
        validate_design('wood_bow', root=self.root)

    def test_missing_anchor_and_cycles_preserve_last_valid_staging(self):
        bow = build_design('wood_bow', root=self.root)
        previous = _hashes(bow)
        self.change('wood_bow', lambda r: r['components'][0].__setitem__('source_anchor', 'absent'))
        with self.assertRaisesRegex(ValueError, 'source anchor'):
            build_design('wood_bow', root=self.root)
        self.assertEqual(previous, _hashes(bow))
        self.change('wood_bow', lambda r: r['components'][0].__setitem__('source_anchor', 'nock'))
        self.change('arrow', lambda r: r.update(components=[dict(name='cycle', item='wood_bow', parent='nock', source_anchor='wood_bow')]))
        with self.assertRaisesRegex(ValueError, 'Cyclic item component'):
            build_design('wood_bow', root=self.root)
        self.assertEqual(previous, _hashes(bow))

    def test_unknown_fields_and_unsafe_ids_fail_before_replacing_staging(self):
        bow = build_design('wood_bow', root=self.root)
        previous = _hashes(bow)
        self.change('wood_bow', lambda r: r['components'][0].__setitem__('unhandled_animation', True))
        with self.assertRaisesRegex(ValueError, 'Unknown component field'):
            build_design('wood_bow', root=self.root)
        self.assertEqual(previous, _hashes(bow))
        self.change('wood_bow', lambda r: r['components'][0].pop('unhandled_animation'))
        self.change('wood_bow', lambda r: r['components'][0].__setitem__('item', '../arrow'))
        with self.assertRaisesRegex(ValueError, 'item-style ID'):
            build_design('wood_bow', root=self.root)
        self.assertEqual(previous, _hashes(bow))


    def test_manual_component_source_remains_authoritative_and_read_only(self):
        arrow = build_design('arrow', root=self.root)
        authored = self.root/'tools/item_designs/arrow/authored'
        shutil.copytree(arrow, authored)
        self.change('arrow', lambda r: r.update(geometry_source=dict(path='authored/model.gltf')))
        provenance = self.root/'tools/item_designs/provenance.json'
        data = json.loads(provenance.read_text())
        data['items']['arrow']['geometry'].update(authoring_format='manual-gltf-2.0', source='tools/item_designs/arrow/authored/model.gltf')
        provenance.write_text(json.dumps(data), encoding='utf-8')
        before = _hashes(authored)
        bow = build_design('wood_bow', root=self.root)
        independent = build_design('arrow', root=self.root)
        self.assertEqual(_hashes(independent), _hashes(bow/'components/loaded_arrow'))
        self.assertEqual(before, _hashes(authored))
        validate_design('wood_bow', root=self.root)

    def test_publication_tracks_dependencies_and_rejects_partial_update(self):
        for id in ('arrow', 'wood_bow'):
            build_design(id, root=self.root)
        publish_designs(['arrow', 'wood_bow'], root=self.root)
        self.assertEqual(validate_manifest(self.root), [])
        original = _hashes(self.root/'models/items')
        self.change('arrow', lambda r: r['materials']['feather']['color'].__setitem__(0, 70))
        build_design('arrow', root=self.root)
        with self.assertRaisesRegex(ValueError, 'Republish dependent item wood_bow'):
            publish_designs(['arrow'], root=self.root)
        self.assertEqual(original, _hashes(self.root/'models/items'))
        build_design('wood_bow', root=self.root)
        publish_designs(['arrow', 'wood_bow'], root=self.root)
        self.assertEqual(validate_manifest(self.root), [])
        manifest = self.root/'models/items/MANIFEST.json'
        data = json.loads(manifest.read_text())
        data['items']['wood_bow']['dependencies'].remove('content/items/arrow.json')
        manifest.write_text(json.dumps(data), encoding='utf-8')
        self.assertTrue(any('incomplete versioned dependency' in error for error in validate_manifest(self.root)))

    def test_unhandled_animation_and_invalid_mounts_keep_last_valid_bundle(self):
        bow = build_design('wood_bow', root=self.root)
        previous = _hashes(bow)
        paths = {id: self.root/'tools/item_designs'/id/'design.yaml' for id in ('wood_bow', 'arrow')}
        originals = {id: path.read_bytes() for id, path in paths.items()}
        cases = [
            ('arrow', lambda r: r.update(visual_bindings=[dict(node='nock', channel='use_phase')]), 'must be static'),
            ('wood_bow', lambda r: r['components'][0].__setitem__('parent', 'absent'), 'parent is missing'),
            ('wood_bow', lambda r: r['nodes'].__setitem__('component_loaded_arrow__mount', dict(pivot=[0, 0, 0])), 'namespace collision'),
            ('wood_bow', lambda r: r['components'][0].__setitem__('scale', [1, 0, 1]), 'scale must be positive')]
        for id, change, error in cases:
            with self.subTest(error=error):
                for key, path in paths.items():
                    path.write_bytes(originals[key])
                self.change(id, change)
                with self.assertRaisesRegex(ValueError, error):
                    build_design('wood_bow', root=self.root)
                self.assertEqual(previous, _hashes(bow))


if __name__ == '__main__':
    unittest.main()
