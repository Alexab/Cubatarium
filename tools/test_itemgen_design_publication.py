"""Versioned item publication must conserve author work through real interruption."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import yaml
from itemgen.design import ROOT,build_design
from itemgen import design_publication as pub,pipeline
from itemgen.manifest import update_manifest,validate_manifest


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.ids=['bucket','iron_head']
        shutil.copytree(ROOT/'tools/itemgen',self.root/'tools/itemgen',ignore=shutil.ignore_patterns('__pycache__'))
        for name in ['LICENSE','tools/item_materials.yaml','tools/item_designs/provenance.json','shaders/item_surface.vert','shaders/item_surface.frag']:
            dest=self.root/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,dest)
        for id in self.ids:
            shutil.copytree(ROOT/'tools/item_designs'/id,self.root/'tools/item_designs'/id)
            for name in [f'content/items/{id}.json',f'tools/item_recipes/{id}.yaml']:
                dest=self.root/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,dest)
            with patch.multiple(pipeline,ROOT=self.root,CONTENT=self.root/'content/items',RECIPES=self.root/'tools/item_recipes',MODELS=self.root/'models/items',MATERIALS=self.root/'tools/item_materials.yaml',STAGING=self.root/'legacy-staging'):
                old=pipeline.build_item(id,publish=False)
            shutil.copytree(old,self.root/'models/items'/id)
            build_design(id,root=self.root)
        update_manifest(self.root)

    def snapshot(self):return pub._hashes(self.root/'models/items')

    def child_crash(self,code):
        program="""
import os,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from itemgen import design_publication as p
root=Path(sys.argv[2]).resolve()
"""+code+"\np.publish_designs(['bucket','iron_head'],root=root)\n"
        return subprocess.run([sys.executable,'-c',program,str(ROOT/'tools'),str(self.root)],capture_output=True,text=True,timeout=30)

    def test_publish_preserves_sources_definition_grips_and_wear(self):
        destination=self.root/'models/items/iron_head'
        for name in ['wield_grip.json','wear.json']:(destination/name).write_text('artist-owned bytes')
        before_sources=pub._hashes(self.root/'tools/item_designs');before_defs=pub._hashes(self.root/'content/items')
        previous={id:pub._hashes(self.root/'models/items'/id) for id in self.ids}
        result=pub.publish_designs(self.ids,root=self.root)
        self.assertEqual(result['published'],self.ids)
        self.assertEqual(pub._hashes(self.root/'tools/item_designs'),before_sources)
        self.assertEqual(pub._hashes(self.root/'content/items'),before_defs)
        for name in ['wield_grip.json','wear.json']:self.assertEqual((destination/name).read_text(),'artist-owned bytes')
        self.assertEqual(validate_manifest(self.root),[])
        for id in self.ids:
            reference=pub._hashes(self.root/'models/items'/id/'_itemgen_reference');reference.pop('REFERENCE.json')
            self.assertEqual(reference,previous[id])
        pub.publish_designs(self.ids,root=self.root)
        for id in self.ids:
            reference=pub._hashes(self.root/'models/items'/id/'_itemgen_reference');reference.pop('REFERENCE.json')
            self.assertEqual(reference,previous[id])
        self.assertEqual(pub.recover_publication(root=self.root),'clean')
        for id in self.ids:
            pub.validate_design(id,root=self.root)
            with self.assertRaisesRegex(ValueError,'protected'):pipeline.guard_legacy_publication(id,models=self.root/'models/items')
        with patch.object(pipeline,'MODELS',self.root/'models/items'):
            with self.assertRaisesRegex(ValueError,'protected'):pipeline.build_item('bucket',publish=True)
            with self.assertRaisesRegex(ValueError,'protected'):pipeline.publish_item('bucket')
        manifest_before=(self.root/'models/items/MANIFEST.json').read_bytes()
        with self.assertRaisesRegex(ValueError,'publish-design'):update_manifest(self.root,'bucket')
        self.assertEqual((self.root/'models/items/MANIFEST.json').read_bytes(),manifest_before)

    def test_stale_source_definition_generator_and_output_fail_before_mutation(self):
        before=self.snapshot()
        names=['tools/item_designs/bucket/design.yaml','content/items/bucket.json','tools/itemgen/mesh.py','shaders/item_surface.frag','assetgen_output/items/redesign/iron_head/model.bin','tools/item_designs/provenance.json']
        for name in names:
            with self.subTest(name=name):
                path=self.root/name;data=path.read_bytes();path.write_bytes(data+b' ')
                try:
                    with self.assertRaises((ValueError,KeyError)):pub.publish_designs(self.ids,root=self.root)
                    self.assertEqual(self.snapshot(),before)
                finally:path.write_bytes(data)
        overlay=self.root/'tools/item_designs/bucket/new-artist-layer.txt';overlay.write_text('new source')
        with self.assertRaisesRegex(ValueError,'source set'):pub.publish_designs(self.ids,root=self.root)
        self.assertEqual(self.snapshot(),before)

    def test_failure_after_first_swap_rolls_everything_back_byte_for_byte(self):
        before=self.snapshot();original=pub._install_directory;calls=[]
        def fail(source,dest,previous):
            original(source,dest,previous);calls.append(dest)
            if len(calls)==1:raise OSError('injected publication failure')
        with patch.object(pub,'_install_directory',side_effect=fail):
            with self.assertRaisesRegex(OSError,'injected'):pub.publish_designs(self.ids,root=self.root)
        self.assertEqual(self.snapshot(),before)
        self.assertEqual(pub.recover_publication(root=self.root),'clean')

    def test_process_death_before_commit_recovers_previous_generation(self):
        before=self.snapshot()
        result=self.child_crash("""
original=p._install_directory
def crash(source,dest,previous):
    original(source,dest,previous)
    os._exit(93)
p._install_directory=crash
""")
        self.assertEqual(result.returncode,93,result.stderr)
        self.assertTrue((self.root/'models/items/PUBLICATION_TRANSACTION.json').is_file())
        self.assertEqual(pub.recover_publication(root=self.root),'rolled_back')
        self.assertEqual(self.snapshot(),before)
        self.assertEqual(pub.recover_publication(root=self.root),'clean')

    def test_process_death_after_commit_retains_completed_generation(self):
        result=self.child_crash("""
original=p._atomic_json
def crash(path,data):
    original(path,data)
    if path==root/'models/items/MANIFEST.json':os._exit(94)
p._atomic_json=crash
""")
        self.assertEqual(result.returncode,94,result.stderr)
        self.assertEqual(pub.recover_publication(root=self.root),'committed')
        self.assertEqual(validate_manifest(self.root),[])
        self.assertEqual(json.loads((self.root/'models/items/bucket/model.gltf').read_text())['extras']['design_version'],3)

    def test_corrupt_recovery_snapshot_fails_without_further_mutation(self):
        before=self.snapshot()
        result=self.child_crash("""
original=p._install_directory
def crash(source,dest,previous):
    original(source,dest,previous)
    os._exit(93)
p._install_directory=crash
""")
        self.assertEqual(result.returncode,93,result.stderr)
        state=json.loads((self.root/'models/items/PUBLICATION_TRANSACTION.json').read_text())
        snapshot=self.root/'assetgen_output/items/publications'/state['generation']/'previous/bucket/model.bin'
        data=snapshot.read_bytes();snapshot.write_bytes(b'corrupt')
        interrupted=self.snapshot()
        with self.assertRaisesRegex(ValueError,'snapshot'):pub.recover_publication(root=self.root)
        self.assertEqual(self.snapshot(),interrupted)
        snapshot.write_bytes(data)
        self.assertEqual(pub.recover_publication(root=self.root),'rolled_back')
        self.assertEqual(self.snapshot(),before)

    def test_single_writer_and_failed_build_keep_existing_output(self):
        before=self.snapshot()
        with pub._writer(self.root):
            with self.assertRaisesRegex(RuntimeError,'active'):pub.publish_designs(self.ids,root=self.root)
        self.assertEqual(self.snapshot(),before)
        stage=self.root/'assetgen_output/items/redesign/bucket';old=pub._hashes(stage)
        source=self.root/'tools/item_designs/bucket/design.yaml';recipe=yaml.safe_load(source.read_text());recipe['triangle_budget']=1
        source.write_text(yaml.safe_dump(recipe))
        with self.assertRaisesRegex(ValueError,'budget'):build_design('bucket',root=self.root)
        self.assertEqual(pub._hashes(stage),old)


if __name__=='__main__':unittest.main()
