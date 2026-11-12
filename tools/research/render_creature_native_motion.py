"""Compose real native rest/locomotion/run/action frames without model reconstruction."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-dir', type=Path, required=True)
    parser.add_argument('--species', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for sid in args.species.split(','):
        sheet = Image.new('RGB', (1024, 7*276), (32, 37, 43))
        draw = ImageDraw.Draw(sheet)
        row = 0
        for label, indices in [('rest', range(4)), ('primary locomotion', range(4, 12)),
                               ('Run at walk_speed', range(13, 21)), ('Action', range(21, 29))]:
            for i, index in enumerate(indices):
                with Image.open(args.native_dir/f'{sid}_{index}.png') as source:
                    source.load()
                    assert source.size == (256, 256)
                    sheet.paste(source.convert('RGB'), (i%4*256, (row+i//4)*276+20))
                draw.text((i%4*256+4, (row+i//4)*276+3), f'{sid}: {label} / {i}', fill='white')
            row += (len(indices)+3)//4
        sheet.save(args.out/f'{sid}.png')
        print('COMPOSED', sid)


if __name__ == '__main__':
    main()
