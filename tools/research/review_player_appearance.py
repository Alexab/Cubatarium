"""Compose native player review sheets; images are engine captures, not rebuilt models."""
from pathlib import Path
import argparse,json,hashlib
from PIL import Image,ImageDraw

def tile(path,size):
 im=Image.open(path).convert('RGBA').resize((size,size),Image.Resampling.NEAREST)
 out=Image.new('RGBA',(size,size),(32,40,50,255));out.alpha_composite(im);return out.convert('RGB')
def main(root,native):
 n=root/native/'player_appearance';rows=json.loads((n/'manifest.json').read_text())
 def grid(name,groups,cols,size=180):
  out=Image.new('RGB',(cols*size,len(groups)*(size+24)),(32,40,50));d=ImageDraw.Draw(out)
  for row,(label,paths) in enumerate(groups):
   d.text((4,row*(size+24)+3),label,fill='white')
   for col,p in enumerate(paths):out.paste(tile(p,size),(col*size,row*(size+24)+24))
  out.save(root/name)
 grid('body_hairstyles_sheet.png',[(b+' / '+h,[n/f'{b}_{h}_view{v}.png' for v in range(4)]) for b in ('male','female') for h in ('none','short','bob','long','ponytail','curly')],4)
 grid('hair_colors_sheet.png',[(b,[n/f'{b}_color_{c}_base.png' for c in ('black','chestnut','brown','blond','ginger')]) for b in ('male','female')],5,200)
 grid('outfits_sheet.png',[(b,[n/f'{b}_color_chestnut_{o}.png' for o in ('base','human_adventurer','human_guard')]) for b in ('male','female')],3,220)
 grid('motion_sheet.png',[(b+' | Walk, Run, Crouch, Jump, Swim, Action',[n/(x['id']+'.png') for x in rows if x['id'].startswith(b+'_motion_') and x['id'].endswith('1')]) for b in ('male','female')],6,170)
 grid('helmets_sheet.png',[(b,[n/f'{b}_{h}_helmet.png' for h in ('none','short','bob','long','ponytail','curly')]) for b in ('male','female')],6,160)
 grid('before_after_sheet.png',[(s+' | BEFORE / AFTER front | BEFORE / AFTER 45deg',[root/'native_before/matrix'/s/'pose_0_view_0_color.png',root/native/'matrix'/s/'pose_0_view_0_color.png',root/'native_before/matrix'/s/'pose_0_view_1_color.png',root/native/'matrix'/s/'pose_0_view_1_color.png']) for s in ('human','human_adventurer','human_guard')],4,240)
 audit=[]
 for row in rows:
  p=n/(row['id']+'.png');im=Image.open(p).convert('RGBA');box=im.getchannel('A').getbbox()
  assert box and min(box)>0 and box[2]<256 and box[3]<256,(p,box)
  audit.append(dict(id=row['id'],sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bbox=box))
 # Retained state must not leak between hair choices: all helmet renders of each body match.
 for b in ('male','female'):
  frames=[Image.open(n/f'{b}_{h}_helmet.png').convert('RGBA').tobytes() for h in ('none','short','bob','long','ponytail','curly')]
  # Framing bounds deliberately include only submitted geometry, so every hidden-hair view is identical.
  assert len(set(frames))==1,b+': helmet did not hide all hair consistently'
 (root/'native_image_audit.json').write_text(json.dumps({'frames':audit,'helmet_hair_hidden':True},indent=2)+'\n')
 print('Reviewed capture integrity:',len(audit),'frames')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--native',default='native_final');a=p.parse_args();main(a.root,a.native)
