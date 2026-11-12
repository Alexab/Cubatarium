"""Verify four isolated proposal world flights; compose review contact sheet.

This records process/resource/spawn evidence, never automatic art/contact passes.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--production', action='store_true', help='Require ordinary bin/models instead of a proposal override')
    args = parser.parse_args()
    out = args.out.resolve()
    report = {'schema': 'creature-proposal-world-flight-audit.v1',
              'evidence_consistent': True, 'art_acceptance': 'pending',
              'scope': 'Four8-second idle/no-travel native Release flights and116 preview PNG; not stride/contact/continuous-action/performance acceptance',
              'production_models': args.production, 'resources': []}
    sheet = Image.new('RGB', (1280, 772), '#171b20')
    draw = ImageDraw.Draw(sheet)
    for index, sid in enumerate(('badger', 'treeman', 'lobster', 'seahorse')):
        label = 'world_' + sid
        context_path = out / ('context_' + label + '.json')
        context = read(context_path)
        started = datetime.fromisoformat(context['captured_at_utc']).timestamp()
        assert context['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
        assert context['world_species'] == context['preview_species'] == [sid]
        assert context['qa_water_placement'] == (sid in ('lobster', 'seahorse'))
        assert context['qa_models_override'] == (not args.production) and context['performance_gate'] == 'disabled'
        if args.production:
            assert Path(context['models_root']).resolve() == (ROOT / 'bin/models').resolve()
        for rel, digest in context['files_sha256'].items():
            assert sha(Path(context['models_root']) / rel) == digest
        for rel, digest in context['shaders_sha256'].items():
            assert sha(ROOT / rel) == digest
        flight = read(out / ('report_' + label + '.json'))
        assert flight['process_success'] and not flight['hang_killed']
        assert flight['world_startup']['runtime_world_matches_requested']
        assert flight['route_observation']['loading_seen']
        assert flight['route_observation']['ingame_frames'] > 30
        relocation = out / ('relocation_' + label + '.txt')
        assert any(line.split()[0] == sid and 'src=3' in line
                   for line in relocation.read_text(encoding='utf8').splitlines() if line.split())
        previews = list((out / ('native_' + sid)).glob(sid + '_*.png'))
        assert len(previews) == 29
        frames = sorted((out / ('runtime_' + label)).glob('frame_*.png'))
        assert len(frames) == 3
        for path in previews + frames:
            assert path.stat().st_mtime >= started
            with Image.open(path) as im:
                im.verify()
        chosen = frames[-1]
        x, y = index % 2 * 640, index // 2 * 386
        draw.text((x + 10, y + 6), sid + ' | actual world; acceptance pending', fill='white')
        with Image.open(chosen) as im:
            sheet.paste(im.convert('RGB').resize((640, 360)), (x, y + 26))
        report['resources'].append({'id': sid, 'context_sha256': sha(context_path),
            'exe_sha256': context['exe_sha256'], 'preview_count': len(previews),
            'world_frame_count': len(frames), 'ingame_frames': flight['route_observation']['ingame_frames'],
            'direct_review_frame': str(chosen.relative_to(out)), 'direct_review_frame_sha256': sha(chosen)})
    sheet.save(out / 'world_contact_sheet.png')
    report['sheet_sha256'] = sha(out / 'world_contact_sheet.png')
    (out / 'audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print('Four world flights,116 preview PNG,12 world PNG: evidence consistent; art pending')


if __name__ == '__main__':
    main()
