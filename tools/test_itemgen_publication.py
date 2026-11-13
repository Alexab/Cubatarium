"""Publication must preserve artist work and publish a verifiable manifest."""
from pathlib import Path
import json
import shutil
import tempfile
import unittest
from unittest.mock import patch
from itemgen import pipeline
from itemgen.manifest import DEPENDENCIES,validate_manifest

ROOT=Path(__file__).resolve().parents[1]
class PublicationTests(unittest.TestCase):
    def test_publication_preserves_authored_pose_and_sidecars(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);id="iron_head"
            for relative in [f"content/items/{id}.json",f"tools/item_recipes/{id}.yaml",*DEPENDENCIES]:
                target=root/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/relative,target)
            dest=root/"models/items"/id;shutil.copytree(ROOT/"models/items"/id,dest)
            (dest/"wield_grip.json").write_text('{"artist":true}',encoding="utf-8")
            (dest/"wear.json").write_text('{"artist_wear":true}',encoding="utf-8")
            definition=root/"content/items"/(id+".json");data=json.loads(definition.read_text());data["visual"]["wield_offset"]=[.11,.22,.33];definition.write_text(json.dumps(data))
            with patch.multiple(pipeline,ROOT=root,CONTENT=root/"content/items",RECIPES=root/"tools/item_recipes",MODELS=root/"models/items",MATERIALS=root/"tools/item_materials.yaml",STAGING=root/"staging"):
                pipeline.build_item(id,publish=False)
                pipeline.publish_item(id)
            self.assertEqual(json.loads(definition.read_text())["visual"]["wield_offset"],[.11,.22,.33])
            self.assertEqual((dest/"wield_grip.json").read_text(),'{"artist":true}')
            self.assertEqual((dest/"wear.json").read_text(),'{"artist_wear":true}')
            self.assertEqual(validate_manifest(root),[])
            (dest/"model.bin").write_bytes(b"changed")
            self.assertTrue(any("model.bin" in e for e in validate_manifest(root)))
    def test_explicit_recipe_pose_remains_authoritative(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/"iron_head.json").write_text('{"visual":{"wield_offset":[1,2,3]}}')
            with patch.object(pipeline,"CONTENT",root):pipeline._retarget_content("iron_head",{"family":"armor","wield_offset":[4,5,6]})
            self.assertEqual(json.loads((root/"iron_head.json").read_text())["visual"]["wield_offset"],[4,5,6])
if __name__=="__main__":unittest.main()
