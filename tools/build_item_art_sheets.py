"""Arrange native Item Lab comparisons for human review; never judge art by counts."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
VIEWS = ('orbit', 'front', 'back', 'side', 'top', 'silhouette', 'gray', 'albedo', 'icons-32-48-64')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, default=ROOT / 'bin/item_lab_art/qa.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/item_art_review')
    args = parser.parse_args()
    report_path = args.report.resolve()
    output = args.output.resolve()
    frame_root = Path(str(report_path) + '.frames/art')
    if output == frame_root or frame_root.is_relative_to(output) or output.is_relative_to(frame_root):
        parser.error('Output must be separate from native evidence')
    report = json.loads(report_path.read_text(encoding='utf-8'))
    if report.get('passed') is not True:
        parser.error('Native report must pass before arranging evidence')
    gallery = report.get('art_gallery', {})
    expected = sorted(p.stem for p in (ROOT / 'content/items').glob('*.json'))
    if sorted(gallery) != expected:
        parser.error('Native gallery must cover the entire current item catalog')
    output.mkdir(parents=True, exist_ok=True)
    frames = {}

    def load(id, view, source):
        name = f'{id}-{view}-{source}.png'
        if name not in gallery[id]['frames'] or Path(name).name != name:
            raise ValueError('Unreported or unsafe frame: ' + name)
        path = frame_root / name
        data = path.read_bytes()
        frames[name] = hashlib.sha256(data).hexdigest()
        with Image.open(path) as image:
            if image.size != (512, 512):
                raise ValueError('Native target must be 512x512: ' + name)
            return image.convert('RGB')

    sheets = {}
    def save(image, name):
        image.save(output / name)
        sheets[name] = hashlib.sha256((output / name).read_bytes()).hexdigest()

    for id in expected:
        sheet = Image.new('RGB', (1000, 1080), (28, 30, 36))
        draw = ImageDraw.Draw(sheet)
        draw.text((10, 8), id + ' | real native reference / candidate | studio light', fill='white')
        for row, source in enumerate(('reference', 'candidate')):
            y = 32 + row * 224
            for column, view in enumerate(VIEWS[:5]):
                image = load(id, view, source).resize((200, 200), Image.Resampling.LANCZOS)
                sheet.paste(image, (column * 200, y))
                draw.text((column * 200 + 5, y + 203), source + ' / ' + view, fill='white')
            y = 484 + row * 224
            for column, view in enumerate(VIEWS[5:8]):
                image = load(id, view, source).resize((200, 200), Image.Resampling.LANCZOS)
                sheet.paste(image, (column * 200, y))
                draw.text((column * 200 + 5, y + 203), source + ' / ' + view, fill='white')
            # Copy the occupied icon strip without resizing: the three rendered
            # targets remain exactly 32/48/64 pixels, not enlarged thumbnails.
            icons = load(id, VIEWS[8], source).crop((16, 216, 496, 296))
            sheet.paste(icons, (row * 500, 966))
            draw.text((row * 500 + 5, 1049), source + ' / native 32, 48, 64 px', fill='white')
        save(sheet, id + '.png')

    for view in ('orbit', 'silhouette', 'gray', 'albedo'):
        for page in range((len(expected) + 17) // 18):
            ids = expected[page * 18:(page + 1) * 18]
            sheet = Image.new('RGB', (1008, 1168), (28, 30, 36))
            draw = ImageDraw.Draw(sheet)
            draw.text((8, 6), view + ' | each pair: reference, candidate', fill='white')
            for index, id in enumerate(ids):
                x, y = (index % 3) * 336, 28 + (index // 3) * 190
                for source_index, source in enumerate(('reference', 'candidate')):
                    image = load(id, view, source).resize((160, 160), Image.Resampling.LANCZOS)
                    sheet.paste(image, (x + source_index * 164, y))
                draw.text((x + 4, y + 164), id, fill='white')
            save(sheet, f'catalog-{view}-{page + 1}.png')
    manifest = dict(scope='native image arrangement; artistic acceptance not automated',
                    report=str(report_path), report_sha256=hashlib.sha256(report_path.read_bytes()).hexdigest(),
                    ids=expected, frames=frames, sheets=sheets, native_icon_pixels_preserved=True)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Arranged {len(frames)} native frames into {len(sheets)} comparison sheets')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
