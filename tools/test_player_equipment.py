"""Regression for regenerated bone-volume footwear: no second sole offset."""
import json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from itemgen import pipeline
class PlayerEquipmentTests(unittest.TestCase):
 def test_regenerated_boot_fitting_matches_published_sidecars(self):
  with tempfile.TemporaryDirectory() as temp,patch.object(pipeline,'STAGING',Path(temp)):
   for material in ('copper','gold','iron','leather'):
    sid=material+'_feet';folder=pipeline.build_item(sid,publish=False)
    generated=json.loads((folder/'wear.json').read_text())
    published=json.loads((pipeline.MODELS/sid/'wear.json').read_text())
    self.assertEqual(generated,published,sid)
    self.assertEqual(generated['fit_mode'],'bone_volume')
    self.assertEqual(generated['offset'],[0,0,0],'fitting already anchors the sole')
    self.assertEqual(set(generated['bones']),{'leftLeg','rightLeg'})
if __name__=='__main__':unittest.main()
