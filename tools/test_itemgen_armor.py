"""Armor source, anatomical-section and physical-envelope contracts."""
from pathlib import Path
import copy
import hashlib
import json
import tempfile
import unittest
import numpy as np
import yaml

from itemgen.armor_designs import make_armor,seed_missing
from itemgen.design import ROOT,build_design
from itemgen.raster import load_primitives
from itemgen.wear_sections import prepare_sections


class ArmorTests(unittest.TestCase):
    def test_all_sixteen_sources_fit_and_have_distinct_geometry(self):
        geometry=set()
        with tempfile.TemporaryDirectory() as tmp:
            for id in make_armor():
                with self.subTest(id=id):
                    source=ROOT/'tools/item_designs'/id/'design.yaml';before=source.read_bytes()
                    definition=ROOT/'content/items'/f'{id}.json';gameplay=definition.read_bytes()
                    path=build_design(id,output=Path(tmp)/id)
                    g=json.loads((path/'model.gltf').read_text());meta=json.loads((path/'design_build.json').read_text())
                    points=np.concatenate([p['positions'] for p in load_primitives(path)])
                    self.assertTrue(np.all(np.ptp(points,axis=0)<=np.array(json.loads(gameplay)['physical']['size'])+1e-6))
                    self.assertLessEqual(meta['triangles'],2000)
                    signature=hashlib.sha256(np.unique(np.round(points,7),axis=0).tobytes()).hexdigest()
                    self.assertNotIn(signature,geometry);geometry.add(signature)
                    sections=g['extras']['wear_sections']
                    self.assertEqual(len(sections),5 if id.endswith('legs') else 2 if id.endswith('feet') else 1)
                    assigned=[bone for section in sections for bone in section['bones']]
                    self.assertEqual(len(assigned),len(set(assigned)))
                    self.assertEqual(source.read_bytes(),before);self.assertEqual(definition.read_bytes(),gameplay)

    def test_safe_seeding(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.assertEqual(len(seed_missing(root)),16)
            file=root/'tools/item_designs/gold_legs/design.yaml';file.write_text('artist sentinel')
            self.assertEqual(seed_missing(root),[]);self.assertEqual(file.read_text(),'artist sentinel')

    def test_reference_bounds_transform_and_rejection(self):
        nodes={'part':{}}
        rows=[dict(node='part',bones=['body'],slot=1,region='full',low=[-1,-2,-3],high=[1,2,3])]
        result=prepare_sections(rows,nodes,.5,[0,1,0],['chest'])
        self.assertEqual(result[0]['low'],[-.5,-1.5,-1.5]);self.assertEqual(rows[0]['low'],[-1,-2,-3])
        for change in ('node','bounds','slot','duplicate','region','orientation'):
            bad=copy.deepcopy(rows)
            if change=='node':bad[0]['node']='missing'
            if change=='bounds':bad[0]['high']=[-1,-2,-3]
            if change=='slot':bad[0]['slot']=3
            if change=='duplicate':bad.append(copy.deepcopy(bad[0]))
            if change=='region':bad[0]['region']='unknown'
            if change=='orientation':bad[0]['euler_deg']=[0,float('nan'),0]
            with self.assertRaises(ValueError):prepare_sections(bad,nodes,1,[0,0,0],['chest'])


if __name__=='__main__':unittest.main()
