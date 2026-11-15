"""Require separate actual TP wielded and physical-drop draws for every item."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def validate(report, expected):
    request = report.get('request', {})
    if not report.get('passed') or request.get('perspective') not in ('front', 'back') or request.get('interaction'):
        raise ValueError('Expected a passing static third-person item report')
    frames = report.get('frames', [])
    if [row['id'] for row in frames] != expected:
        raise ValueError('Actual-world TP report must cover the complete ordered catalog')
    for row in frames:
        item = row['id']
        draws = [draw for draw in row.get('world_items', [])
                 if Path(draw['path']).parent.name == item and draw.get('visible_primitives', 0) > 0]
        if row.get('physical_bodies') != 1 or not row.get('matching_body') or row.get('active_item') != item or len(draws) != 2:
            raise ValueError(f'{item}: require one active item and one physical drop with two actual model draws; got {len(draws)}')
    return len(frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding='utf-8'))
    summary = json.loads(args.report.with_name('summary.json').read_text(encoding='utf-8'))
    if not summary.get('passed'):
        raise ValueError('The native runner did not pass its restoration/source checks')
    expected = sorted(path.stem for path in (ROOT/'content/items').glob('*.json'))
    count = validate(report, expected)
    print(f'Actual world TP resources: {count} items, each wielded plus one distinct physical drop draw')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
