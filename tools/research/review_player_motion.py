"""Review sheets from native renderer captures; never synthesizes a model."""
from pathlib import Path
import argparse,json,hashlib,re,datetime
from PIL import Image,ImageDraw

def compose(root):
    before=root/'native_before/player_appearance'
    after=root/'native_after/player_appearance'
    def tile(path,size):
        im=Image.open(path).convert('RGBA').resize((size,size),Image.Resampling.NEAREST)
        bg=Image.new('RGBA',im.size,(35,42,52,255));bg.alpha_composite(im)
        return bg.convert('RGB')
    def sheet(name,rows,size=180):
        cols=max(len(paths) for _,paths in rows)
        im=Image.new('RGB',(cols*size,len(rows)*(size+24)),(35,42,52));d=ImageDraw.Draw(im)
        for row,(label,paths) in enumerate(rows):
            d.text((5,row*(size+24)+4),label,fill='white')
            for col,path in enumerate(paths):im.paste(tile(path,size),(col*size,row*(size+24)+24))
        im.save(root/name)
    states=('Walk','Run','Crouch','Jump','Fall','Fly','Hover','Swim','Tread','Action')
    sheet('motion_sheet.png',[(body+' | '+' '.join(states),[after/f'{body}_motion_{state}2.png' for state in states])for body in ('male','female')],160)
    rows=[]
    for body in ('male','female'):
        for state in ('Walk','Run','Crouch','Jump','Swim','Action'):
            rows.append((f'{body} {state}: BEFORE / AFTER phases 1 2 3',
                         [folder/f'{body}_motion_{state}{phase}.png' for phase in (1,2,3) for folder in (before,after)]))
    sheet('before_after_motion.png',rows,160)
    sheet('body_hairstyles_sheet.png',[(body+' / '+hair,[after/f'{body}_{hair}_view{v}.png' for v in range(4)])for body in ('male','female')for hair in ('none','short','bob','long','ponytail','curly')])
    sheet('equipment_sheet.png',[(body,[after/f'{body}_{hair}_helmet.png'for hair in ('none','short','bob','long','ponytail','curly')])for body in ('male','female')])
    sheet('hair_motion_sheet.png',[(body+' / '+hair,[after/f'{body}_{hair}_key_{state}.png'for state in ('Run','Crouch','Swim','Fly','Fall')])for body in ('male','female')for hair in ('none','short','bob','long','ponytail','curly')],180)
    sheet('equipment_motion_sheet.png',[(body+' / '+hair,[after/f'{body}_{hair}_gear_{state}.png'for state in ('Run','Crouch','Swim')])for body in ('male','female')for hair in ('none','short','bob','long','ponytail','curly')],200)
    sheet('hair_colors_sheet.png',[(body,[after/f'{body}_color_{color}_base.png'for color in ('black','chestnut','brown','blond','ginger')])for body in ('male','female')],200)
    records=[]
    for row in json.loads((after/'manifest.json').read_text(encoding='utf-8')):
        path=after/(row['id']+'.png');im=Image.open(path).convert('RGBA');bbox=im.getchannel('A').getbbox()
        if not bbox or min(bbox)<=0 or bbox[2]>=256 or bbox[3]>=256:raise ValueError((path,bbox))
        records.append({'id':row['id'],'bbox':bbox,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    for body in ('male','female'):
        images=[Image.open(after/f'{body}_{hair}_helmet.png').tobytes()for hair in ('none','short','bob','long','ponytail','curly')]
        assert len(set(images))==1,'helmet must hide every hairstyle'
    (root/'native_image_audit.json').write_text(json.dumps({'frames':records,'helmet_hair_hidden':True},indent=2)+'\n',encoding='utf-8')
    print('Validated',len(records),'native frames')
    fp=root/'native_after/fp'
    variants=('human','human_female','human_adventurer','human_female_guard')
    out=Image.new('RGB',(1152,len(variants)*240),(35,42,52));draw=ImageDraw.Draw(out)
    for row,variant in enumerate(variants):
        draw.text((5,row*240+4),variant+' | bare / dual / attack',fill='white')
        for col,case in enumerate(('bare_idle_0','dual_idle_0','dual_attack_8')):
            im=Image.open(fp/f'{variant}_{case}.png').convert('RGBA').crop((0,180,768,432)).resize((384,216),Image.Resampling.NEAREST)
            bg=Image.new('RGBA',im.size,(35,42,52,255));bg.alpha_composite(im);out.paste(bg.convert('RGB'),(col*384,row*240+24))
    out.save(root/'first_person_sheet.png')
    route=json.loads((root/'route_audit.json').read_text(encoding='utf-8'))
    project=Path(__file__).resolve().parents[2];source=Path(route['source'])
    if not source.is_absolute():source=project/source
    frames=sorted((source.parent/'frames').glob('*.png'))
    rows=[json.loads(line)for line in source.read_text(encoding='utf-8').splitlines()]
    # PNG writing is asynchronous and can lag by seconds. Associate native
    # captures using their enqueue timestamps, not their file creation time.
    epoch=source.stat().st_mtime-rows[-1]['time']
    capture_times={};capture_log=None
    for log in (project/'bin/logs').glob('*INFO*'):
        for line in log.read_text(encoding='utf-8',errors='replace').splitlines():
            if source.parent.name not in line or '[FlightCapture] queued index=' not in line:continue
            stamp=re.match(r'I(\d{8}) (\d{2}:\d{2}:\d{2}\.\d+)',line)
            index=re.search(r'queued index=(\d+)',line)
            if stamp and index:
                capture_times[f'frame_{int(index[1]):03d}.png']=datetime.datetime.strptime(stamp[1]+' '+stamp[2],'%Y%m%d %H:%M:%S.%f').timestamp()
                capture_log=log
    if not all(path.name in capture_times for path in frames):raise ValueError('Native enqueue timestamps missing')
    groups={name:[r for r in rows if r['scenario']==name]for name in route['stages']}
    out=Image.new('RGB',(4*320,5*264),(35,42,52));draw=ImageDraw.Draw(out);selected=[]
    def crop(path):return Image.open(path).convert('RGB').crop((384,170,896,682))
    for i,(name,values) in enumerate(groups.items()):
        target=epoch+values[len(values)//2]['time']
        path=min(frames,key=lambda p:abs(capture_times[p.name]-target))
        x=(i%4)*320;y=(i//4)*264
        out.paste(crop(path).resize((240,240)),(x+40,y+24));draw.text((x+4,y+4),name+' / '+path.stem,fill='white')
        selected.append({'scenario':name,'frame':path.name,'capture_epoch':capture_times[path.name],'time_association':'native enqueue timestamp; route origin aligned to final flushed row','sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    out.save(root/'world_motion_sheet.png')
    clip=frames[::2];images=[];durations=[]
    for i,path in enumerate(clip):
        im=crop(path).resize((384,384));ImageDraw.Draw(im).text((5,5),path.stem,fill='white')
        images.append(im.quantize(colors=96))
        duration=(capture_times[clip[i+1].name]-capture_times[path.name])*1000 if i+1<len(clip) else 500
        durations.append(max(100,min(1500,int(duration))))
    images[0].save(root/'movement_native.gif',save_all=True,append_images=images[1:],duration=durations,loop=0,optimize=True)
    (root/'world_visual_context.json').write_text(json.dumps({'source':str(source),'capture_log':str(capture_log),'queued_captures':len(capture_times),'saved_captures':len(frames),'selected_frames':selected,'clip':'native captures, cropped and sampled every second saved frame; no synthetic pose frames'},indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True)
    compose(p.parse_args().root)
