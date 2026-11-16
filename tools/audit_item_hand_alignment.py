"""Audit functional directions and palm contacts of actual submitted item models."""
import argparse
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def unit(value):
    length = math.sqrt(sum(x*x for x in value))
    if length < 1e-8 or not math.isfinite(length):
        raise ValueError('Invalid direction')
    return [x/length for x in value]


def transform(matrix, value, point=False):
    if len(matrix) != 16 or any(not math.isfinite(x) for x in matrix):
        raise ValueError('Invalid model matrix')
    return [sum(matrix[c*4+r]*value[c] for c in range(3)) +
            (matrix[12+r] if point else 0) for r in range(3)]


def distance(a, b):
    return math.sqrt(sum((x-y)**2 for x, y in zip(a, b)))


def inspect_directions(matrix, forward, contract):
    measurements = []
    for rule in contract['checks']:
        axis = unit(transform(matrix, rule['axis']))
        target = unit(forward) if rule['direction'] == 'forward' else [0, 1, 0]
        alignment = sum(a*b for a, b in zip(axis, target))
        if rule['absolute']:
            alignment = abs(alignment)
        measurements.append(dict(**rule, alignment=alignment,
                                 passed=alignment >= rule['minimum']))
    return measurements


def inspect_world(report, contracts):
    if report.get('passed') is not True or report['request'].get('interaction'):
        raise ValueError('Expected passing static actual-world capture')
    hand = report['request'].get('hand', 'right')
    first = report['request']['perspective'] == 'first'
    rows = []
    for frame in report['frames']:
        item = frame['id']
        if item not in contracts:
            raise ValueError('Unclassified item: '+item)
        if frame.get('active_item') != item or not frame.get('matching_body'):
            raise ValueError('Capture lost actual held or dropped item: '+item)
        if first:
            draws = [x for x in frame['fp_items'] if x['id'] == item and
                     x.get('left') == (hand == 'left')]
            if len(draws) != 1:
                raise ValueError('Require exactly one matching FP hand draw: '+item)
            draw = draws[0]
            gap = draw['grip_ndc_gap']
            contact = gap < .0001
            forward = draw['view_forward']
        else:
            if not frame.get('has_wield_palm'):
                raise ValueError('Missing actual skeleton palm: '+item)
            matches = []
            for draw in frame['world_items']:
                if Path(draw['path']).parent.name != item:
                    continue
                anchor = draw[hand+'_grip']
                gap = distance(transform(draw['model_world'], anchor, True),
                               frame['wield_palm'])
                if gap < .002:
                    matches.append((draw, gap))
            if len(matches) != 1:
                raise ValueError('Require one distinct palm-attached TP draw: '+item)
            draw, gap = matches[0]
            contact = True
            forward = frame['camera_front']
        measurements = inspect_directions(draw['model_world'], forward, contracts[item])
        rows.append(dict(id=item, hand=hand, body=report['request']['body'],
                         perspective=report['request']['perspective'],
                         grip_gap=gap, contact_passed=contact,
                         measurements=measurements,
                         passed=contact and draw['visible_primitives'] > 0 and
                         all(x['passed'] for x in measurements)))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--swing-frames', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--allow-subset', action='store_true')
    args = parser.parse_args()
    contracts = json.loads((ROOT/'tools/item_hand_alignment.json').read_text(encoding='utf-8'))['items']
    catalog = sorted(p.stem for p in (ROOT/'content/items').glob('*.json'))
    if sorted(contracts) != catalog:
        raise ValueError('Every catalog item requires an explicit hand contract')
    if bool(args.report) == bool(args.swing_frames):
        parser.error('Choose one world report or deterministic swing-frame directory')
    if args.report:
        data = json.loads(args.report.read_text(encoding='utf-8'))
        summary = json.loads(args.report.with_name('summary.json').read_text(encoding='utf-8'))
        if not summary.get('passed'):
            raise ValueError('Runner restoration/source checks did not pass')
        rows = inspect_world(data, contracts)
        if not args.allow_subset and [x['id'] for x in rows] != catalog:
            raise ValueError('Require complete ordered catalog coverage')
    else:
        rows = []
        seen = set()
        for file in sorted(args.swing_frames.glob('*-swing-*.png.json')):
            data = json.loads(file.read_text(encoding='utf-8'))
            item = data['id']
            body, step = file.name.split('-swing-', 1)[1].split('.png.json')[0].rsplit('-', 1)
            key = (item, body, int(step))
            if key in seen or item not in contracts:
                raise ValueError('Duplicate/unclassified swing sample')
            seen.add(key)
            # At the strike peak, a chopping tool may tilt down. Its working
            # direction must still face the action; upright-only constraints
            # apply to rest/return, not to the downstroke.
            contract = contracts[item]
            if step == '1':
                contract = dict(checks=[dict(rule, minimum=.35) for rule in contract['checks']
                                       if rule['direction'] == 'forward'])
            values = inspect_directions(data['model_world'], data['view_forward'], contract)
            contact = data['main_grip_ndc_gap'] < .0001
            rows.append(dict(id=item, body=body, step=int(step), measurements=values,
                             grip_gap=data['main_grip_ndc_gap'], contact_passed=contact,
                             passed=contact and data['all_in_front'] and
                             data['visible_bounds_fraction'] >= .6 and all(v['passed'] for v in values)))
        expected = {(item, body, step) for item in catalog for body in ('male', 'female') for step in range(3)}
        if seen != expected:
            raise ValueError(f'Incomplete all-item swing coverage: {len(seen)}/{len(expected)}')
    result = dict(passed=all(x['passed'] for x in rows), source=str(args.report or args.swing_frames),
                  scope='Actual held draw, selected by matching hand and palm; physical drops cannot satisfy the gate',
                  items=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    failed = [x['id'] for x in rows if not x['passed']]
    print(f'Hand alignment: {len(rows)} items; failed={failed}')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
