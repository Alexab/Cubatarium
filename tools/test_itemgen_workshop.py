"""Tool wave contract: 25 candidates, construction differences and safe seeding."""
from pathlib import Path
import hashlib
import json
import tempfile
import unittest
import numpy as np

from itemgen.design import ROOT, build_design
from itemgen.raster import load_primitives
from itemgen.workshop_designs import TIERS, FAMILIES, make_workshop_tools, seed_missing


class WorkshopTests(unittest.TestCase):
    def test_seeding_preserves_existing_author_sources(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            self.assertEqual(len(seed_missing(root)),25)
            source=root/'tools/item_designs/iron_axe/design.yaml'
            source.write_text('author sentinel',encoding='utf-8')
            self.assertEqual(seed_missing(root),[])
            self.assertEqual(source.read_text(encoding='utf-8'),'author sentinel')

    def test_all_tools_build_with_unique_meshes_and_unchanged_definitions(self):
        definitions={p:p.read_bytes() for p in (ROOT/'content/items').glob('*.json')}
        geometry=set()
        with tempfile.TemporaryDirectory() as tmp:
            for family in FAMILIES:
                for tier in TIERS:
                    id=f'{tier}_{family}'
                    path=build_design(id,output=Path(tmp)/id)
                    report=json.loads((path/'design_build.json').read_text())
                    self.assertLessEqual(report['triangles'],1200,id)
                    parts=load_primitives(path)
                    positions=np.concatenate([p['positions'] for p in parts])
                    physical=json.loads(definitions[ROOT/'content/items'/f'{id}.json'])['physical']['size']
                    self.assertTrue(np.all(np.ptp(positions,axis=0)<=np.array(physical)+1e-6),id)
                    # Compare actual coordinates, independent of UV/material
                    # bytes and primitive ordering; recolors cannot pass.
                    unique=np.unique(np.round(positions,7),axis=0)
                    mesh=hashlib.sha256(unique.tobytes()).hexdigest()
                    self.assertNotIn(mesh,geometry,id);geometry.add(mesh)
                    self.assertTrue(parts,id)
        self.assertEqual(len(geometry),25)
        self.assertTrue(all(p.read_bytes()==data for p,data in definitions.items()))

    def test_tiers_change_construction_not_only_colors(self):
        designs=make_workshop_tools()
        for family in FAMILIES:
            geometry={json.dumps(designs[f'{tier}_{family}']['parts'],sort_keys=True) for tier in TIERS}
            self.assertEqual(len(geometry),5,family)
        for tier in TIERS:
            self.assertTrue(any('cheek' in p['name'] for p in designs[f'{tier}_shovel']['parts']))
            self.assertTrue(any(p['rotation'][0]!=0 for p in designs[f'{tier}_hoe']['parts'] if p['name']=='hoe_working_blade'))


if __name__=='__main__': unittest.main()
