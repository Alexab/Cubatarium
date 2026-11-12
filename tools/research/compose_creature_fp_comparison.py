"""Compose deterministic sheets from the actual, audited FP frame pairs."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw
from audit_creature_texture_mapping_probe import read, digest


BG = (34, 36, 42)


def bounds(paths):
    boxes = []
    for path in paths:
        with Image.open(path) as image:
            box = image.convert('RGBA').getchannel('A').getbbox()
            if box:
                boxes.append(box)
    assert boxes
    return (max(0, min(b[0] for b in boxes)-16), max(0, min(b[1] for b in boxes)-16),
            min(768, max(b[2] for b in boxes)+16), min(432, max(b[3] for b in boxes)+16))


def tile(canvas, path, xy, size, box):
    with Image.open(path) as image:
        image = image.convert('RGBA').crop(box)
        scale = min(size[0]/image.width, size[1]/image.height, 2.)
        image = image.resize((round(image.width*scale), round(image.height*scale)), Image.Resampling.NEAREST)
        x, y = xy
        canvas.paste(image, (x+(size[0]-image.width)//2, y+size[1]-image.height), image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--before-dir', default='native_before')
    parser.add_argument('--after-dir', default='native_final')
    args = parser.parse_args()
    audit = read(args.out / 'fp_audit.json')
    folders = [args.out / directory / 'fp' for directory in (args.before_dir, args.after_dir)]
    for folder, records in zip(folders, (audit['before_frame_sha256'], audit['frame_sha256'])):
        for row in records:
            assert digest(folder / row['path']) == row['sha256']
    target = args.out / 'sheets'
    target.mkdir(parents=True, exist_ok=True)
    records = []
    resources = ['human', 'human_adventurer', 'human_guard', 'bot', 'zombie', 'skeleton']
    for resource in resources:
        sheet = Image.new('RGB', (1792, 528), BG)
        draw = ImageDraw.Draw(sheet)
        draw.text((10, 8), f'{resource}: SAME crop/scale in each before/after pair; actual FP pixels', fill='white')
        for column, loadout in enumerate(('bare', 'main', 'off', 'dual')):
            name = f'{resource}_{loadout}_idle_0.png'
            paths = [folder / name for folder in folders]
            box = bounds(paths)
            for row, path in enumerate(paths):
                x, y = column*448, 32+row*248
                draw.text((x+8, y+4), f'{"before" if row==0 else "after"} / {loadout}', fill='white')
                tile(sheet, path, (x, y+24), (448, 216), box)
        path = target / f'{resource}_idle_comparison.png'
        sheet.save(path)
        records.append({'path': str(path.relative_to(args.out)).replace('\\', '/'), 'sha256': digest(path), 'resource': resource, 'scope': 'Four idle loadout pairs'})
        for action, count in [('walk', 8), ('attack', 17)]:
            names = [f'{resource}_dual_{action}_{phase}.png' for phase in range(count)]
            box = bounds([folder/name for folder in folders for name in names])
            columns = 5 if action == 'attack' else 4
            rows = (count+columns-1)//columns
            canvas = Image.new('RGB', (columns*288, rows*152*2+32), BG)
            draw = ImageDraw.Draw(canvas)
            draw.text((10, 8), f'{resource} / dual / {action}: before top, after bottom; complete sampled cycle', fill='white')
            for version, folder in enumerate(folders):
                for phase, name in enumerate(names):
                    x, y = (phase%columns)*288, 32+(version*rows+phase//columns)*152
                    draw.text((x+6, y+4), f'{"before" if version==0 else "after"} {phase}/{count-1}', fill='white')
                    tile(canvas, folder/name, (x, y+22), (288, 126), box)
            path = target / f'{resource}_{action}_comparison.png'
            canvas.save(path)
            records.append({'path': str(path.relative_to(args.out)).replace('\\', '/'), 'sha256': digest(path), 'resource': resource, 'scope': f'Dual loadout {count}-phase {action} pair'})
    (target / 'manifest.json').write_text(json.dumps({'schema': 'creature-fp-comparison-sheets.v1', 'scope': 'Actual audited native pixels, comparisons preserve pair crop/scale; no automatic art verdict', 'sheets': records}, indent=2)+'\n', encoding='utf8')
    print(f'Wrote {len(records)} paired FP sheets')


if __name__ == '__main__':
    main()
