"""Player geometry, UV, deterministic generation and independence regressions."""
import unittest,tempfile,json,hashlib
from PIL import Image
from pathlib import Path
from generate_player_appearance import generate,model_for,hair_cubes,BODIES,HAIR,COLORS,ROOT
from creaturegen import pipeline
from creaturegen.anatomy import build_model
class PlayerAppearanceTests(unittest.TestCase):
 def test_rig_and_physics_contract(self):
  base=build_model(pipeline.load_recipe('human'))
  for body in BODIES:
   for hair in HAIR:
    model=model_for(body,hair)
    self.assertEqual([(b.name,b.parent,b.pivot) for b in base.bones],[(b.name,b.parent,b.pivot) for b in model.bones[:-1]])
    self.assertEqual(model.bones[-1].parent,'head')
    if hair=='none':self.assertEqual(len(model.bones[-1].cubes),0)
    else:self.assertGreater(len(model.bones[-1].cubes),0)
    for c in model.cubes():
     u,v=c.uv;w,h,d=c.size
     self.assertLessEqual(u+2*(w+d),256);self.assertLessEqual(v+h+d,256)
  male=next(b for b in model_for('male','short').bones if b.name=='rightArm').cubes[0]
  female=next(b for b in model_for('female','short').bones if b.name=='rightArm').cubes[0]
  self.assertEqual(male.size[0],4);self.assertEqual(female.size[0],3)
 def test_hair_uv_islands(self):
  boxes=list(hair_cubes().values())
  for i,a in enumerate(boxes):
   u,v=a.uv;w,h,d=a.size;self.assertGreaterEqual(v,128)
   for b in boxes[i+1:]:
    x,y=b.uv;bw,bh,bd=b.size
    self.assertFalse(u<x+2*(bw+bd) and x<u+2*(w+d) and v<y+bh+bd and y<v+h+d)
 def test_deterministic_export_and_published_payload(self):
  with tempfile.TemporaryDirectory() as a,tempfile.TemporaryDirectory() as b:
   x=generate(Path(a));y=generate(Path(b));self.assertEqual(x,y);self.assertEqual(len(x['files']),42)
   for p,sha in x['files'].items():self.assertEqual(hashlib.sha256((ROOT/'models'/p).read_bytes()).hexdigest(),sha,p)
 def test_all_180_geometry_atlas_pairs_have_painted_faces(self):
  pairs=0
  for body in BODIES:
   for hair in HAIR:
    model=model_for(body,hair)
    for outfit in ('human','human_adventurer','human_guard'):
     folder=ROOT/'models'/('creatures' if outfit=='human' else 'skins')/outfit
     for color in COLORS:
      with Image.open(folder/'textures'/f'appearance_{body}_{color}.png') as image:
       self.assertEqual(image.size,model.texture_size)
       for cube in model.cubes():
        u,v=cube.uv;w,h,d=cube.size
        centres=[(u+d//2,v+d+h//2),(u+d+w//2,v+d+h//2),
                 (u+d+w+d//2,v+d+h//2),(u+2*d+w+w//2,v+d+h//2),
                 (u+d+w//2,v+d//2),(u+d+w+w//2,v+d//2)]
        for point in centres:self.assertEqual(image.getpixel(point)[3],255,(body,hair,outfit,color,cube.name,point))
      pairs+=1
  self.assertEqual(pairs,180)
if __name__=='__main__':unittest.main()
