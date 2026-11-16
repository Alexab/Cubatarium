"""Compose labelled audit sheets from unchanged native captures; never edit inputs."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
CASES = [
    ('FP right', 'day-1280-100-hand-final', (520, 80, 1280, 720)),
    ('FP left', 'day-1280-100-left-hand-final', (0, 80, 760, 720)),
    ('TP back right', 'day-1280-100-back-hand-final', (440, 180, 840, 580)),
    ('TP back left', 'day-1280-100-back-left-hand-final', (440, 180, 840, 580)),
    ('TP female right', 'day-1280-100-front-female-hand-final', (440, 180, 840, 580)),
    ('TP female left', 'day-1280-100-front-female-left-hand-final', (440, 180, 840, 580)),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'bin/item_hand_acceptance/gallery')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    ids = sorted(p.stem for p in (ROOT/'content/items').glob('*.json'))
    sources = {}
    sheets = {}
    frames = ROOT/'bin/items_acceptance/native.json.presentations.json.frames'

    def panel(path, crop, label):
        sources[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        with Image.open(path) as source:
            image = source.convert('RGB').crop(crop)
        image.thumbnail((400, 300), Image.Resampling.LANCZOS)
        result = Image.new('RGB', (400, 326), (25, 28, 34))
        result.paste(image, ((400-image.width)//2, 26+(300-image.height)//2))
        ImageDraw.Draw(result).text((8, 7), label, fill='white')
        return result

    for item in ids:
        sheet = Image.new('RGB', (1200, 1004), (25, 28, 34))
        ImageDraw.Draw(sheet).text((10, 7), item, fill='white')
        panels = [panel(ROOT/'bin/item_world_visual'/case/(item+'.png'), crop, label)
                  for label, case, crop in CASES]
        for body, step in [('male', 0), ('male', 1), ('female', 1)]:
            path = frames/f'{item}-swing-{body}-{step}.png'
            panels.append(panel(path, (440, 40, 1280, 720), f'FP rendered swing {body}, phase {step}'))
        for index, image in enumerate(panels):
            sheet.paste(image, ((index % 3)*400, 26+(index//3)*326))
        path = args.output/(item+'.png')
        sheet.save(path)
        sheets[item] = hashlib.sha256(path.read_bytes()).hexdigest()
    # Smaller pages support visual inspection of all 70 items without selecting
    # only attractive examples. Full per-item sheets remain independently saved.
    for start in range(0, len(ids), 7):
        page = Image.new('RGB', (1720, 7*220), (25, 28, 34))
        draw = ImageDraw.Draw(page)
        for row, item in enumerate(ids[start:start+7]):
            draw.text((5, row*220+10), item, fill='white')
            for col, index in enumerate((0, 1, 4, 5)):
                label, case, crop = CASES[index]
                image = panel(ROOT/'bin/item_world_visual'/case/(item+'.png'), crop, label)
                image.thumbnail((400, 220), Image.Resampling.LANCZOS)
                page.paste(image, (120+col*400, row*220))
        page.save(args.output/f'overview-{start//7+1:02}.png')
    result = dict(items=70, native_source_frames=len(sources), source_sha256=sources,
                  sheet_sha256=sheets,
                  presentation='Labelled crops, aspect-preserving Lanczos downsampling; unmodified source PNG hashes recorded. This is not an original-size image archive.')
    (args.output/'manifest.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f'Composed {len(sheets)} item sheets and ten overview pages')


if __name__ == '__main__':
    main()
