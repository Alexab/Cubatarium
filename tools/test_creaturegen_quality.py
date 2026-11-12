"""Regression tests for the evidence boundary and glTF hierarchy contract."""
from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

from creaturegen.anatomy import Bone, Cube, Model
from creaturegen.export_gltf import export_gltf
from creaturegen.quality import ARTIFACTS, CRITERIA, bundle_hash, review_errors
from creaturegen.pipeline import palette_for_recipe
from creaturegen import pipeline


class QualityTests(unittest.TestCase):
    def test_authored_bone_recipe_cannot_silently_fall_back_to_an_archetype_gait(self):
        recipe = dict(pipeline.load_recipe('cow'))
        recipe.pop('animation_profile', None)
        with patch.object(pipeline, 'load_recipe', return_value=recipe):
            with self.assertRaisesRegex(ValueError, 'requires explicit animation_profile'):
                pipeline.runtime_definition('cow', self.root)

    def test_manifest_covers_skins_and_rejects_unreviewed_or_changed_payload(self):
        stage=self.root/'stage';models=self.root/'models/creatures';skins=self.root/'models/skins'
        runtime=self.root/'bin/models/creatures';runtime.mkdir(parents=True)
        for folder,published,sid,accepted in [(stage/'sample',models/'sample','sample',True),
                (stage/'skins/coat',skins/'coat','coat',False)]:
            folder.mkdir(parents=True);published.mkdir(parents=True)
            (folder/'payload.bin').write_bytes(b'own');(published/'payload.bin').write_bytes(b'own')
            metadata={'id':sid,'backend':'bone_skeleton','state':'exported'}
            (folder/'manifest.json').write_text(json.dumps(metadata))
            if accepted:
                metadata.update(state='accepted',review_bundle_sha256=bundle_hash(folder))
                (folder/'manifest.json').write_text(json.dumps(metadata))
        with patch.multiple(pipeline,ROOT=self.root,STAGING=stage,MODELS=models,SKINS=skins,
                SKIN_PLANS={'coat':('sample','forest','')}),patch.object(pipeline,'creature_ids',return_value=['sample','missing']):
            pipeline.write_asset_manifest()
            doc=json.loads((models/'asset_manifest.json').read_text())
            self.assertEqual([e['state'] for e in doc['species']],['accepted','missing_provenance'])
            self.assertEqual(doc['skins'][0]['state'],'needs_visual_review')
            self.assertEqual((models/'asset_manifest.json').read_bytes(),(runtime/'asset_manifest.json').read_bytes())
            (models/'sample/payload.bin').write_bytes(b'changed')
            pipeline.write_asset_manifest()
            self.assertEqual(json.loads((models/'asset_manifest.json').read_text())['species'][0]['state'],'needs_visual_review')

    def test_toolchain_and_functional_contract_are_bound_to_review(self):
        manifest=self.bundle/'manifest.json'
        manifest.write_text(json.dumps({'id':'sample','toolchain':{'Pillow':'old'},'functional_contract_sha256':'one'}))
        self.entry['bundle_sha256']=bundle_hash(self.bundle);self.save_review()
        manifest.write_text(json.dumps({'id':'sample','toolchain':{'Pillow':'new'},'functional_contract_sha256':'one'}))
        self.assertIn('stale review: candidate bundle changed',review_errors('sample',self.bundle,self.review))
        manifest.write_text(json.dumps({'id':'sample','toolchain':{'Pillow':'old'},'functional_contract_sha256':'two'}))
        self.assertIn('stale review: candidate bundle changed',review_errors('sample',self.bundle,self.review))

    def test_portable_image_binding_and_zero_weight_joint_slots(self):
        cube=Cube([0,0,0],[4,4,4],'body',blend={
            'bone':'tip','axis':'y','range':[0,4],'weights':[0,1]})
        model=Model([Bone('root',None,[0,0,0],[]),
            Bone('body','root',[0,0,0],[cube]),Bone('tip','body',[0,4,0],[])])
        gltf,blob=export_gltf(model,[])
        attributes=gltf['meshes'][0]['primitives'][0]['attributes']
        def values(name,fmt):
            a=gltf['accessors'][attributes[name]];view=gltf['bufferViews'][a['bufferView']]
            offset=view.get('byteOffset',0)+a.get('byteOffset',0);stride=struct.calcsize(fmt)
            return [struct.unpack_from(fmt,blob,offset+i*stride) for i in range(a['count'])]
        for joints,weights in zip(values('JOINTS_0','<4B'),values('WEIGHTS_0','<4f')):
            self.assertAlmostEqual(sum(weights),1)
            for joint,weight in zip(joints,weights):
                if weight==0:self.assertEqual(joint,0,'unused influence must not carry a joint')
        material=gltf['materials'][0]
        texture=gltf['textures'][material['pbrMetallicRoughness']['baseColorTexture']['index']]
        self.assertEqual(gltf['images'][texture['source']]['uri'],'textures/'+material['name']+'.png')
        self.assertEqual(material['alphaMode'],'MASK')
        self.assertEqual(material['alphaCutoff'],.05)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bundle = self.root / "candidate"
        self.bundle.mkdir()
        (self.bundle / "model.bin").write_bytes(b"candidate payload")
        self.evidence = self.root / "comparison.png"
        Image.new("RGB", (64, 64), (50, 100, 150)).save(self.evidence)
        digest = hashlib.sha256(self.evidence.read_bytes()).hexdigest()
        self.entry = {"decision": "accepted", "reviewed_by": "test fixture",
                      "baseline_revision": "baseline", "bundle_sha256": bundle_hash(self.bundle),
                      "criteria": dict.fromkeys(CRITERIA, True),
                      "artifacts": {k: {"path": self.evidence.name, "sha256": digest} for k in ARTIFACTS}}
        self.review = self.root / "review.json"

    def save_review(self):
        self.review.write_text(json.dumps({"schema": "creature-quality-review.v1", "species": {"sample": self.entry}}), encoding="utf-8")

    def test_missing_review_blocks_structurally_valid_bundle(self):
        self.assertTrue(review_errors("sample", self.bundle, None))

    def test_complete_review_integrity_can_pass(self):
        self.save_review()
        self.assertEqual([], review_errors("sample", self.bundle, self.review))

    def test_candidate_edit_invalidates_review(self):
        self.save_review()
        (self.bundle / "model.bin").write_bytes(b"changed")
        self.assertIn("stale review: candidate bundle changed", review_errors("sample", self.bundle, self.review))

    def test_reviewed_image_edit_invalidates_evidence(self):
        self.save_review()
        self.evidence.write_bytes(b"changed")
        self.assertTrue(any("changed comparison" in e for e in review_errors("sample", self.bundle, self.review)))

    def test_marker_is_not_a_render_even_with_matching_hash(self):
        self.evidence.write_bytes(b"BMP")
        for a in self.entry["artifacts"].values():
            a["sha256"] = hashlib.sha256(b"BMP").hexdigest()
        self.save_review()
        self.assertTrue(any("invalid comparison image" in e for e in review_errors("sample", self.bundle, self.review)))

    def test_partial_review_cannot_be_accepted(self):
        del self.entry["criteria"]["rig_and_motion"]
        self.save_review()
        self.assertIn("unreviewed criterion: rig_and_motion", review_errors("sample", self.bundle, self.review))

    def test_scale_change_invalidates_an_otherwise_identical_bundle(self):
        manifest = self.bundle/"manifest.json"
        manifest.write_text(json.dumps({"id":"sample","model_scale":1}),encoding="utf-8")
        self.entry["bundle_sha256"] = bundle_hash(self.bundle)
        self.save_review()
        manifest.write_text(json.dumps({"id":"sample","model_scale":2}),encoding="utf-8")
        self.assertIn("stale review: candidate bundle changed", review_errors("sample",self.bundle,self.review))

    def test_scene_lists_roots_not_joint_descendants(self):
        model = Model([Bone("body", None, [0, 0, 0], [Cube([0, 0, 0], [2, 2, 2], "body")]),
                       Bone("head", "body", [0, 2, 0], [Cube([0, 2, 0], [2, 2, 2], "face")])])
        g, _ = export_gltf(model, ["idle", "walk"])
        roots = set(g["scenes"][0]["nodes"])
        children = {i for n in g["nodes"] for i in n.get("children", [])}
        self.assertFalse(roots & children)

    def test_species_palette_is_independent_of_family_defaults(self):
        palettes = {"forest": {"body": [80, 70, 60], "mark": [60, 50, 40]}}
        result = palette_for_recipe({"palette_id": "forest", "palette_overrides": {"body": [220, 220, 210]}}, palettes)
        self.assertEqual(result["body"], [220, 220, 210])
        self.assertEqual(palettes["forest"]["body"], [80, 70, 60])
        with self.assertRaises(ValueError):
            palette_for_recipe({"palette_id": "forest", "palette_overrides": {"body": [500, 0, 0]}}, palettes)


if __name__ == "__main__":
    unittest.main()
