"""Equal-elapsed diagnostic GIFs from verified native phase captures, not art acceptance."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from audit_creature_anatomy_pilot import verified_context
from audit_compose_creature_envelopes import read, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--species', required=True)
    args = parser.parse_args(); out = args.out.resolve()
    variants = [('original','native_original','context_envelope_original.json'),
                ('own before','native_before','context_own_before.json'),
                ('candidate','native_current','context_envelope_current.json')]
    resources = {}
    for label,folder,context in variants:
        verified_context(out,context)
        manifest = read(out/folder/'envelopes/manifest.json')
        resource = next(r for r in manifest['resources'] if r['id']==args.species)
        resources[label] = resource
        for key in ['framing_low','framing_high']:
            assert resource[key] == resources['original'][key]
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf',15)
    small = ImageFont.truetype('C:/Windows/Fonts/consola.ttf',12)
    outputs = []
    for state in ['Walk','Run']:
        frames = []; samples = []; errors = []; periods = {}
        for label,_,_ in variants:
            witness = next(f for f in resources[label]['frames'] if f['state']==state and f['phase']==.5)
            periods[label] = witness['anim_time_sec']/witness['effective_phase']
        for step in range(125):
            elapsed = step*.04
            sheet = Image.new('RGB',(816,340),(27,32,39)); draw=ImageDraw.Draw(sheet)
            draw.text((8,5),f'{args.species} {state} | elapsed {elapsed:.2f}s | unit travelScale',font=font,fill='white')
            draw.text((8,25),'5s preview excerpt repeats; GIF boundary is not an asset loop seam',font=small,fill='#d2dce8')
            for col,(label,folder,_) in enumerate(variants):
                desired = (elapsed/periods[label])%1
                available = [f for f in resources[label]['frames'] if f['state']==state]
                frame = min(available,key=lambda f:min(abs(f['effective_phase']-desired),1-abs(f['effective_phase']-desired)))
                error = min(abs(frame['effective_phase']-desired),1-abs(frame['effective_phase']-desired))*periods[label]
                errors.append(error)
                path = out/folder/'envelopes'/frame['native']['png']
                with Image.open(path) as im:
                    assert im.size == (256,256) and im.getchannel('A').getbbox()
                    tile=Image.new('RGBA',(256,256),(226,230,234,255));tile.alpha_composite(im.convert('RGBA'))
                x=8+col*272
                draw.text((x,49),f'{label}: {periods[label]:.3f}s',font=small,fill='white')
                sheet.paste(tile.convert('RGB'),(x,72))
                samples.append({'step':step,'variant':label,'elapsed_seconds':elapsed,'desired_phase':desired,
                                'captured_phase':frame['effective_phase'],'phase_time_error_seconds':error,
                                'source':str(path.relative_to(out)),'sha256':sha(path)})
            frames.append(sheet)
        path=out/f'temporal_{state.lower()}.gif'
        frames[0].save(path,save_all=True,append_images=frames[1:],duration=40,loop=0,disposal=2,optimize=False)
        for step in [0,25,50,75,100]:
            frames[step].save(out/f'temporal_{state.lower()}_{step:03}.png')
        strip=Image.new('RGB',(1320,916),(27,32,39)); sd=ImageDraw.Draw(strip)
        sd.text((8,8),f'{args.species} {state} | SAME ELAPSED preview samples; own candidate unapproved',font=font,fill='white')
        for row,(label,_,_) in enumerate(variants):
            for col,step in enumerate([0,12,25,37,50]):
                x=8+col*264;y=42+row*290
                sd.text((x,y),f'{label} t={step*.04:.2f}s',font=small,fill='white')
                strip.paste(frames[step].crop((8+row*272,72,264+row*272,328)),(x,y+22))
        strip.save(out/f'temporal_{state.lower()}_strip.png')
        outputs.append({'path':path.name,'sha256':sha(path),'state':state,'periods_seconds_at_unit_travel':periods,
                        'maximum_nearest_phase_time_error_seconds':max(errors),'samples':samples})
    report={'scope':'Verified native phase samples replayed on a shared elapsed preview clock; 25fps5s excerpt, nearest phase sampling, GIF color quantization; not terrain/sliding/world/performance or art acceptance',
            'contexts_sha256':{c:sha(out/c) for _,_,c in variants},'outputs':outputs}
    (out/'temporal_comparison.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print('Temporal comparisons',len(outputs))


if __name__ == '__main__':
    main()
