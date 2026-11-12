"""Compose all three native pose turntables with verified capture inputs."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from audit_creature_anatomy_pilot import verified_context
from audit_compose_creature_envelopes import sha, read


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--species', required=True)
    args = parser.parse_args(); out = args.out.resolve()
    context = verified_context(out, 'context_envelope_current.json')
    native = out / 'native_current/matrix'
    manifest = read(native / 'manifest.json')
    resource = next(r for r in manifest['resources'] if r['id'] == args.species)
    assert len(resource['frames']) == 30
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 14)
    sheets = []
    for group in range(3):
        selected = resource['frames'][group * 10:(group + 1) * 10]
        assert len({(f['state'], f['phase']) for f in selected}) == 1
        sheet = Image.new('RGB', (800, 400), (27, 31, 36)); draw = ImageDraw.Draw(sheet)
        draw.text((8, 5), args.species + ' | native full turn | proposal unapproved', font=font, fill='white')
        for i, frame in enumerate(selected):
            path = native / frame['color']
            with Image.open(path) as image:
                assert image.getchannel('A').getbbox()
                tile = image.convert('RGBA').resize((152, 152), Image.Resampling.NEAREST)
            background = Image.new('RGBA', tile.size, (224, 229, 232, 255)); background.alpha_composite(tile)
            x, y = i % 5 * 160 + 4, i // 5 * 185 + 28
            sheet.paste(background.convert('RGB'), (x, y + 30))
            draw.text((x, y), f"{frame['state']} {frame['phase']}", font=font, fill='white')
            draw.text((x, y + 14), f"yaw{frame['yaw_degrees']} p{frame['pitch_degrees']}", font=font, fill='white')
        path = out / f'fullturn_{group}.png'; sheet.save(path)
        sheets.append({'path': path.name, 'sha256': sha(path), 'frames': selected})
    (out / 'fullturn_sheets.json').write_text(json.dumps({'scope': 'Verified native views; not artistic acceptance',
        'context_exe_sha256': context['exe_sha256'], 'manifest_sha256': sha(native / 'manifest.json'),
        'sheets': sheets}, indent=2) + '\n', encoding='utf-8')
    print('Composed three native full-turn sheets')


if __name__ == '__main__': main()
