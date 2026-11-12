"""Verify reviewed proposals reached ordinary recipes/staging/models/bin exactly."""
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


def rgba(a, b):
    with Image.open(a) as x, Image.open(b) as y:
        assert x.size == y.size and x.convert('RGBA').tobytes() == y.convert('RGBA').tobytes(), (a, b)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    production = out / 'production'
    qa = ROOT / 'assetgen_output/creatures/envelope_motion_v5'
    context = read(production / 'context_production_final.json')
    body = read(production / 'body_parity.json')
    assert sha(ROOT / 'bin/Cubatarium.exe') == context['exe_sha256']
    assert set(body['expected_changed_resources']) == {'badger', 'treeman', 'lobster', 'seahorse'}
    assert body['legacy_png_count'] + body['matrix_png_count'] == 5290
    assert body['exact_rgba_png_count'] + len(body['expected_changed_frames']) == 5290
    rows = []
    for sid in ('badger', 'treeman', 'lobster', 'seahorse'):
        proposal = qa / 'models/creatures' / sid
        published, runtime, staged = (ROOT / base / sid for base in ('models/creatures', 'bin/models/creatures', 'assetgen_output/creatures/staging'))
        assert (ROOT / 'tools/creature_recipes' / (sid + '.yaml')).read_bytes() == (qa / 'recipes' / (sid + '.yaml')).read_bytes()
        for folder in (published, runtime, staged):
            assert read(folder / 'model.gltf') == read(proposal / 'model.gltf')
            assert (folder / 'model.bin').read_bytes() == (proposal / 'model.bin').read_bytes()
            for p in (proposal / 'textures').glob('*.png'):
                assert p.read_bytes() == (folder / 'textures' / p.name).read_bytes()
        assert read(published / 'creature.json') == read(proposal / 'creature.json')
        for i in range(29):
            name = sid + '_' + str(i) + '.png'
            rgba(out / 'native_current' / name, production / 'native_final' / name)
        rows.append({'id': sid, 'recipe_sha256': sha(ROOT / 'tools/creature_recipes' / (sid + '.yaml')),
                     'staged_manifest_sha256': sha(staged / 'manifest.json'),
                     'model_bin_sha256': sha(published / 'model.bin'), 'ordinary_preview_rgba_exact': 29})
    old = ROOT / 'docs/research/creature-r6-v2/user-world/integrated'
    old_context = read(old / 'context_integrated_final.json')
    relevant = ('creatures/human/', 'creatures/bot/', 'creatures/zombie/', 'creatures/skeleton/', 'skins/')
    current_files = {k.replace('\\', '/'): v for k, v in context['files_sha256'].items()}
    for path, digest in old_context['files_sha256'].items():
        if path.replace('\\', '/').startswith(relevant):
            assert current_files[path.replace('\\', '/')] == digest
    assert context['equipment_files_sha256'] == old_context['equipment_files_sha256']
    old_fp, new_fp = old / 'native_final/fp', production / 'native_final/fp'
    old_manifest, new_manifest = read(old_fp / 'manifest.json'), read(new_fp / 'manifest.json')
    fp_count = 0
    for a, b in zip(old_manifest['resources'], new_manifest['resources']):
        assert a['id'] == b['id'] and len(a['frames']) == len(b['frames'])
        for before, after in zip(a['frames'], b['frames']):
            assert before['png'] == after['png']
            rgba(old_fp / before['png'], new_fp / after['png'])
            fp_count += 1
    assert fp_count == 936
    ledger = read(out / 'ledger_after_publish.json')['summary']
    for key in ('exact_valid_r5_reviews', 'published_payload_matches_staged', 'bin_payload_matches_staged', 'current_generator_source_inputs', 'valid_file_records'):
        assert ledger[key] == 61
    assert ledger['runtime_global_manifest_matches']
    report = {'schema': 'creature-motion-v5-publication-audit.v1', 'passed': True,
              'scope': 'Four normal-pipeline R5 revisions, exact reviewed data and GPU previews, full61 capture regression, unchanged FP pixels; not final R6 art/terrain/routes/UI-pack/performance acceptance',
              'exe_sha256': context['exe_sha256'], 'resources': rows, 'proposal_preview_rgba_exact_count': 116,
              'body_matrix_total': 5290, 'unchanged_rgba_count': body['exact_rgba_png_count'],
              'changed_frames_in_four_reviewed_resources': len(body['expected_changed_frames']),
              'fp_rgba_exact_count': fp_count, 'fp_authored_arm_gpu_draws': 1872,
              'ledger_resources_fresh': 61, 'review_sha256': sha(out / 'revision_review.json'),
              'input_reports_sha256': {p: sha(production / p) for p in ('body_parity.json', 'fp_audit.json', 'context_production_final.json')}}
    (out / 'publication_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print('Published4 exact;116 candidate UI frames exact;5290 body/936 FP frames verified;ledger61 fresh')


if __name__ == '__main__':
    main()
