"""Compose verified original/own-before/proposal native frames; no acceptance."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from audit_creature_anatomy_pilot import verified_context
from audit_compose_creature_envelopes import overlay, read, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--species', required=True)
    parser.add_argument('--states', default='Idle:0,Walk:.25,Action:.5')
    args = parser.parse_args(); out = args.out.resolve()
    variants = [('original', 'native_original', 'context_envelope_original.json'),
                ('own before', 'native_before', 'context_own_before.json'),
                ('candidate', 'native_current', 'context_envelope_current.json')]
    resources = {}
    for label, folder, context in variants:
        verified_context(out, context)
        manifest = read(out / folder / 'envelopes/manifest.json')
        resources[label] = next(r for r in manifest['resources'] if r['id'] == args.species)
        for key in ('framing_low', 'framing_high'):
            assert resources[label][key] == resources['original'][key]
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 17)
    sheets = []
    for condition in args.states.split(','):
        state, value = condition.split(':'); phase = float(value)
        sheet = Image.new('RGB', (960, 990), (24, 29, 33)); draw = ImageDraw.Draw(sheet)
        draw.text((8, 8), f'{args.species} {state} {phase}: original / own before / candidate', font=font, fill='white')
        draw.text((8, 30), 'Native Release, shared camera; green collider; proposal unapproved', font=font, fill='white')
        for row, (label, folder, _) in enumerate(variants):
            resource = resources[label]
            for col, view in enumerate(('front', 'side', 'oblique')):
                frame = next(f for f in resource['frames'] if f['state'] == state and f['phase'] == phase and f['view'] == view)
                draw.text((col * 320 + 8, 60 + row * 308), label + ' | ' + view, font=font, fill='white')
                tile = overlay(out / folder / 'envelopes', frame, resource['collision_max'], 288, True)
                sheet.paste(tile, (col * 320 + 16, 80 + row * 308))
        path = out / ('threeway_' + state.lower() + '.png'); sheet.save(path)
        sheets.append({'path': path.name, 'sha256': sha(path)})
    report = {'scope': 'Verified native composition only; not artistic acceptance', 'sheets': sheets,
              'contexts_sha256': {context: sha(out / context) for _, _, context in variants}}
    (out / 'threeway_sheets.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Three-way native sheets:', len(sheets))


if __name__ == '__main__': main()
