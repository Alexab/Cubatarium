"""Compare post-culling native pixels and retained poses against earlier evidence."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pixels(a, b):
    with Image.open(a) as x, Image.open(b) as y:
        assert x.size == y.size and x.convert('RGBA').tobytes() == y.convert('RGBA').tobytes(), (a, b)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    historical = out.parent
    native_audit = read(out / 'native_audit.json')
    assert native_audit['visual_envelope_required']
    assert native_audit['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
    assert native_audit['recovered_false_culls'] == {'baseline': 13, 'current': 16}
    main_count = 0
    for folder in ('native_original', 'native_current'):
        before = read(historical / folder / 'envelopes/manifest.json')
        after = read(out / folder / 'envelopes/manifest.json')
        assert [r['id'] for r in before['resources']] == [r['id'] for r in after['resources']]
        for a, b in zip(before['resources'], after['resources']):
            assert len(a['frames']) == len(b['frames'])
            for x, y in zip(a['frames'], b['frames']):
                for key in ('low', 'high', 'view_projection', 'draws'):
                    assert x['observed'][key] == y['observed'][key]
                pixels(historical / folder / 'envelopes' / x['native']['png'],
                       out / folder / 'envelopes' / y['native']['png'])
                main_count += 1
    frame_count = preview_count = 0
    transition_rows = []
    for label, old_folder, new_folder, old_context in (
            ('published', 'native_transitions_published_after', 'native_published', 'context_transitions_published_after.json'),
            ('proposal', 'native_current', 'native_proposal', 'context_envelope_current.json')):
        old_root = historical / 'motion_v4'
        before = read(old_root / old_folder / 'transition_probe.json')
        after = read(out / 'transitions' / new_folder / 'transition_probe.json')
        old_ctx = read(old_root / old_context)
        new_ctx = read(out / 'transitions' / ('context_transitions_' + label + '.json'))
        assert old_ctx['files_sha256'] == new_ctx['files_sha256']
        assert old_ctx['shaders_sha256'] == new_ctx['shaders_sha256']
        assert new_ctx['exe_sha256'] == native_audit['exe_sha256']
        assert [r['id'] for r in before['resources']] == [r['id'] for r in after['resources']]
        for a, b in zip(before['resources'], after['resources']):
            assert len(a['cases']) == len(b['cases'])
            for x, y in zip(a['cases'], b['cases']):
                assert {k: v for k, v in x.items() if k != 'frames'} == {k: v for k, v in y.items() if k != 'frames'}
                assert len(x['frames']) == len(y['frames'])
                for f, g in zip(x['frames'], y['frames']):
                    assert f == {k: g[k] for k in f}, (label, a['id'])
                    assert g['visual_envelope_covers_geometry']
                    assert all(g['render_low'][i] - .0001 <= g['low'][i] <= g['high'][i] <= g['render_high'][i] + .0001 for i in range(3))
                    frame_count += 1
            for i in range(29):
                name = a['id'] + '_' + str(i) + '.png'
                pixels(old_root / old_folder / name, out / 'transitions' / new_folder / name)
                preview_count += 1
        transition_rows.append({'catalog': label, 'old_context_sha256': sha(old_root / old_context),
            'new_context_sha256': sha(out / 'transitions' / ('context_transitions_' + label + '.json')),
            'native_sha256': sha(out / 'transitions' / new_folder / 'transition_probe.json')})
    inputs = {}
    for folder in (ROOT / 'models/creatures', ROOT / 'assetgen_output/creatures/review_2026_11_10/baseline/models/creatures'):
        for path in folder.glob('*/model.*'):
            if path.suffix in ('.gltf', '.bin'):
                inputs[str(path.relative_to(ROOT))] = sha(path)
    source = ('src/Creatures/Visual/Gltf/GltfConservativeRenderRadius.h',
              'src/Creatures/Visual/Gltf/CreatureGltfLoader.cpp', 'src/Creatures/Visual/Gltf/CreatureGltfTypes.h',
              'src/Creatures/Visual/CreatureVisualGltf.cpp', 'src/Creatures/Visual/CreatureVisualGltf.h',
              'src/Creatures/Visual/CreatureVisual.h', 'src/Creatures/Visual/CreatureVisibility.h',
              'src/Render/Engine/CreatureDrawPass.cpp', 'src/App/Platform/CreatureQaEnvelopeCapture.h',
              'src/App/Platform/CreatureQaTransitionProbe.h', 'tools/research/creature_gltf_render_envelope_audit.cpp')
    report = {'schema': 'creature-culling-regression-audit.v1', 'passed': True,
              'scope': 'Native preview GPU edge policy/CPU geometry and retained cold-entry transitions; not whole-world frustum routes, B/R/sprite envelopes or performance acceptance',
              'exe_sha256': native_audit['exe_sha256'], 'main_rgba_exact_count': main_count,
              'transition_frame_and_palette_exact_count': frame_count, 'transition_preview_rgba_exact_count': preview_count,
              'transition_contexts': transition_rows, 'model_inputs_sha256': inputs,
              'source_sha256': {p: sha(ROOT / p) for p in source},
              'research_exe_sha256': sha(ROOT / 'build/desktop-msvc/Release/creature_gltf_render_envelope_audit.exe')}
    assert main_count == 2472 and frame_count == 10912 and preview_count == 232
    (out / 'regression_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print('Exact:2472 GPU previews,10912 transition frames/palettes,232 transition previews; covered all sampled geometry')


if __name__ == '__main__':
    main()
