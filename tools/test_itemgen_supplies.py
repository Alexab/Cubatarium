"""Authored supplies: physical bounds, real constructions, state and provenance."""
from pathlib import Path
import hashlib
import json
import tempfile
import unittest
import numpy as np
import yaml

from itemgen.design import ROOT, build_design
from itemgen.raster import load_primitives
from itemgen.supplies_designs import make_supplies, seed_missing
from itemgen.surface_paint import paint_surface
from itemgen.surface_export import write_asset
from itemgen.mesh import bevel_box


class SupplyTests(unittest.TestCase):
    def test_material_fields_are_distinct_and_emission_is_validated(self):
        styles=('plain','feather','painted','char','flame','cast','ore','coal','wool','grain','meat','roast')
        hashes={hashlib.sha256(paint_surface(32,48,dict(color=[130,100,70],pattern=style),'part.front').tobytes()).hexdigest() for style in styles}
        self.assertEqual(len(hashes),len(styles))
        with tempfile.TemporaryDirectory() as tmp:
            for bad in ([1.1,0,0],[-1,0,0],[0,0],[False,0,0]):
                dest=Path(tmp)/'invalid'
                with self.assertRaisesRegex(ValueError,'Emissive'):
                    write_asset(dest,'invalid',bevel_box((.1,.1,.1),.01),{'body':dict(color=[1,2,3],emissive=bad)},size=128,density=50)
                self.assertFalse(dest.exists())

    def test_regeneration_is_read_only_and_preserves_gameplay(self):
        ids=make_supplies()
        self.assertEqual(len(ids),23)
        definitions={id:(ROOT/'content/items'/f'{id}.json').read_bytes() for id in ids}
        sources={id:(ROOT/'tools/item_designs'/id/'design.yaml').read_bytes() for id in ids}
        geometry={}
        with tempfile.TemporaryDirectory() as tmp:
            for id in ids:
                with self.subTest(id=id):
                    output=build_design(id,output=Path(tmp)/id)
                    recipe=yaml.safe_load(sources[id])
                    build=json.loads((output/'design_build.json').read_text())
                    self.assertLessEqual(build['triangles'],recipe['triangle_budget'])
                    points=np.concatenate([p['positions'] for p in load_primitives(output)])
                    physical=json.loads(definitions[id])['physical']['size']
                    self.assertTrue(np.all(np.ptp(points,axis=0)<=np.array(physical)+1e-6))
                    self.assertTrue(np.isfinite(points).all())
                    signature=hashlib.sha256(np.unique(np.round(points,7),axis=0).tobytes()).hexdigest()
                    family='cast_ingot' if id.endswith('_ingot') else id
                    self.assertTrue(signature not in geometry or geometry[signature]==family,id)
                    geometry[signature]=family
                    self.assertEqual(sources[id],(ROOT/'tools/item_designs'/id/'design.yaml').read_bytes())
                    self.assertEqual(definitions[id],(ROOT/'content/items'/f'{id}.json').read_bytes())

    def test_seeding_retains_custom_sources(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            self.assertEqual(len(seed_missing(root)),23)
            source=root/'tools/item_designs/waterskin/design.yaml'
            source.write_text('authored sentinel',encoding='utf-8')
            self.assertEqual(seed_missing(root),[])
            self.assertEqual(source.read_text(encoding='utf-8'),'authored sentinel')

    def test_construction_and_state_contracts(self):
        sources=make_supplies()
        arrow=sources['arrow']
        self.assertEqual(sum(p['name'].startswith('feather') for p in arrow['parts']),3)
        self.assertGreater(max(y for x,y in arrow['parts'][1]['contour']),.3)
        for id in ('wood_shield','iron_shield'):
            self.assertTrue(any(p['name']=='back_grip' and p['shape']=='ring' for p in sources[id]['parts']))
            self.assertTrue(any(p['name']=='reverse_arm_strap' for p in sources[id]['parts']))
        self.assertNotEqual(sources['wood_shield']['parts'],sources['iron_shield']['parts'])
        self.assertEqual(sources['waterskin']['visual_bindings'][0]['range'],[0,4])
        self.assertEqual(sources['flint_and_steel']['visual_bindings'][0]['curve'],'pulse')
        self.assertTrue(any(m.get('emissive',[0,0,0])[0]>0 for m in sources['wood_torch']['materials'].values()))
        for source in sources.values():
            self.assertFalse(source['authorship']['external_meshes'])
            self.assertFalse(source['authorship']['external_pixels'])


if __name__=='__main__': unittest.main()
