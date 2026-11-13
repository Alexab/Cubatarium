"""Own block character variants. Does not modify the shared creature generator or other species.
Run: python tools/generate_player_appearance.py [--output directory]
"""
from pathlib import Path
import argparse,copy,hashlib,json
from PIL import Image
from creaturegen import pipeline
from creaturegen.anatomy import build_model,Bone,Cube
from creaturegen.export_bone import export_geometry
from creaturegen.surface_materials import bake_surface_atlas,surface_point
from creaturegen.provenance import source_context
ROOT=Path(__file__).resolve().parents[1]
BODIES=('male','female')
HAIR=('none','short','bob','long','ponytail','curly')
COLORS={'black':(35,31,33),'chestnut':(78,46,31),'brown':(113,82,53),'blond':(205,174,102),'ginger':(159,73,34)}
# Each hair box occupies its own UV island, shared by every body/outfit/color.
BOXES={
 'cap':([-4.5,32.02,-4.5],[9,1,9]),
 'back':([-4.5,27,3.25],[9,5,1]),
 'fringe':([-4.5,29,-4.25],[5,3,1]),
 'side_l':([-4.3,26,-3],[1,6,7]),
 'side_r':([3.3,26,-3],[1,6,7]),
 'long_back':([-4.5,20,3.25],[9,7,1]),
 'tail':([-1.5,22,4.25],[3,8,3]),
 'curl_l':([-5.25,28,-3.5],[2,4,7]),
 'curl_r':([3.25,28,-3.5],[2,4,7]),
 'curl_top':([-2.5,33.03,-2.5],[5,1,5]),
 'short_l':([-4.25,29,-3.25],[1,3,7]),
 'short_r':([3.25,29,-3.25],[1,3,7]),
}
SETS={'none':[],'short':['cap','back','fringe','short_l','short_r'],
 'bob':['cap','back','fringe','side_l','side_r'],
 'long':['cap','back','fringe','side_l','side_r','long_back'],
 'ponytail':['cap','back','fringe','short_l','short_r','tail'],
 'curly':['cap','back','fringe','curl_l','curl_r','curl_top']}
def hair_cubes():
 result={};u=1;v=130;row=0
 for name,(origin,size) in BOXES.items():
  w,h,d=size;width=2*(w+d);height=h+d
  if u+width>256:u=1;v+=row+2;row=0
  result[name]=Cube(origin,size,'accent',(u,v),'hair_'+name)
  u+=width+2;row=max(row,height)
 return result

def model_for(body,style):
 m=build_model(pipeline.load_recipe('human'));m.texture_size=(256,256)
 if body=='female':
  for b in m.bones:
   for c in b.cubes:
    if b.name in ('rightArm','leftArm'):
     c.size[0]=3
     c.origin[0]=-6.5 if b.name=='rightArm' else 3.5
    if b.name=='body':c.origin[0]+=.5;c.size[0]=7
 cubes=hair_cubes()
 m.bones.append(Bone('hair','head',[0,24,0],[cubes[n] for n in SETS[style]]))
 return m

def paint(body,outfit,color):
 # Paint the union once; UVs of every hairstyle are a subset of this atlas.
 m=model_for(body,'none');m.bones[-1].cubes=list(hair_cubes().values())
 recipe=pipeline.load_recipe('human');palette=pipeline.palette_for_recipe(recipe,pipeline._load_palettes())
 if outfit=='human_adventurer':palette.update(body=[120,86,50],belly=[210,190,145],limb=[63,73,62])
 if outfit=='human_guard':palette.update(body=[75,86,108],belly=[171,166,145],limb=[51,57,70],mark=[119,105,78])
 image=bake_surface_atlas(m,palette,'traveler');pixels=image.load()
 for c in m.cubes():
  u,v=c.uv;w,h,d=c.size
  regions={'west':(u,v+d,d,h),'north':(u+d,v+d,w,h),'east':(u+d+w,v+d,d,h),'south':(u+2*d+w,v+d,w,h),'up':(u+d,v,w,d),'down':(u+d+w,v,w,d)}
  if c.name!='head' and not c.name.startswith('hair_'):continue
  for face,(ox,oy,fw,fh) in regions.items():
   for j in range(fh):
    for i in range(fw):
     x,y,z=surface_point(c,face,(i+.5)/fw,(j+.5)/fh)
     if c.name.startswith('hair_'):
      shade=5 if int(x+z)%3==0 else -3 if face=='down' else 0
      rgb=tuple(max(0,min(255,k+shade)) for k in COLORS[color])
     else:
      rgb=tuple(palette['face'])
      if face=='north':
       # One logical texel per feature: calm two-texel eyes, small nose and mouth.
       if 27<=y<28 and 1<=abs(x)<3:rgb=(232,222,198) if abs(x)>=2 else (61,84,72)
       elif 28<=y<29 and 2<=abs(x)<3:rgb=(174,125,92) if body=='male' else (167,116,86)
       elif 26<=y<27 and 0<x<1:rgb=(176,124,90)
       elif 25<=y<26 and abs(x)<1:rgb=(165,105,88) if body=='female' else (171,118,89)
      elif face=='down':rgb=(169,118,89)
     pixels[ox+i,oy+j]=tuple(rgb)+(255,)
 return image

def generate(output):
 human=output/'creatures/human';(human/'appearance').mkdir(parents=True,exist_ok=True)
 definition=json.loads((ROOT/'models/creatures/human/creature.json').read_text())
 identifier=definition['visual'].get('geometry','geometry.human')
 for body in BODIES:
  for style in HAIR:
   (human/'appearance'/f'{body}_{style}.geo.json').write_text(json.dumps(export_geometry(model_for(body,style),identifier),indent=2)+'\n')
  for outfit in ('human','human_adventurer','human_guard'):
   folder=human if outfit=='human' else output/'skins'/outfit
   (folder/'textures').mkdir(parents=True,exist_ok=True)
   for color in COLORS:paint(body,outfit,color).save(folder/'textures'/f'appearance_{body}_{color}.png')
 files=[*human.glob('appearance/*'),*human.glob('textures/appearance_*.png')]
 for outfit in ('human_adventurer','human_guard'):files.extend((output/'skins'/outfit/'textures').glob('appearance_*.png'))
 manifest={'schema':'player-appearance-assets.v1','source_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
 **source_context(ROOT,'human'),
 'files':{p.relative_to(output).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}}
 (human/'appearance_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 return manifest
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'models');a=p.parse_args()
 result=generate(a.output);print('Generated',len(result['files']),'player appearance assets')
