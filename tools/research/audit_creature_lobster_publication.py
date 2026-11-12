"""Audit the reviewed own lobster refit reached staging/models/bin exactly."""
import argparse
import json
from pathlib import Path
from PIL import Image
from audit_creature_motion_v5 import ROOT, sha
from creaturegen.quality import bundle_hash


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rgba(a, b):
    with Image.open(a) as x, Image.open(b) as y:
        assert x.size == y.size and x.convert('RGBA').tobytes() == y.convert('RGBA').tobytes(), (a, b)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(); out = args.out.resolve()
    qa = ROOT / 'assetgen_output/creatures/lobster_anatomy_v3'
    snapshot = ROOT / 'assetgen_output/creatures/lobster_anatomy_pre_publish'
    for name, digest in read(snapshot / 'snapshot_manifest.json')['files_sha256'].items(): assert sha(snapshot / name) == digest
    assert (ROOT / 'tools/creature_recipes/lobster.yaml').read_bytes() == (qa / 'recipes/lobster.yaml').read_bytes()
    proposal = qa / 'models/creatures/lobster'
    for base in ('models/creatures', 'bin/models/creatures', 'assetgen_output/creatures/staging'):
        folder = ROOT / base / 'lobster'
        assert read(folder / 'model.gltf') == read(proposal / 'model.gltf')
        assert (folder / 'model.bin').read_bytes() == (proposal / 'model.bin').read_bytes()
        for p in (proposal / 'textures').glob('*.png'): assert p.read_bytes() == (folder / 'textures' / p.name).read_bytes()
    assert read(ROOT / 'models/creatures/lobster/creature.json') == read(proposal / 'creature.json') == read(snapshot / 'models/creatures/lobster/creature.json')
    review = read(out / 'revision_review.json')['species']['lobster']
    assert review['bundle_sha256'] == bundle_hash(ROOT / 'assetgen_output/creatures/staging/lobster')
    rig = read(out / 'rig_contact_audit.json')
    assert rig['passed'] and rig['joints'] == 51 and len(rig['bind_nodes_refit']) == 18
    assert rig['angular_channels_byte_exact'] == 90 and rig['fault_old_translation_regression_blocks'] > .05
    for name, digest in rig['payload_sha256'].items(): assert sha(proposal / name) == digest
    production = out / 'production'; context = read(production / 'context_production_final.json')
    assert context['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe') and not context['qa_models_override']
    body = read(production / 'body_parity.json')
    assert body['expected_changed_resources'] == ['lobster']
    assert body['legacy_png_count'] + body['matrix_png_count'] == 5290
    assert body['exact_rgba_png_count'] + len(body['expected_changed_frames']) == 5290
    count = 0
    for i in range(29):
        rgba(out / 'native_current' / f'lobster_{i}.png', production / 'native_final' / f'lobster_{i}.png'); count += 1
    matrix = read(out / 'native_current/matrix/manifest.json')['resources'][0]
    assert matrix['id'] == 'lobster' and len(matrix['frames']) == 30
    for frame in matrix['frames']:
        for key in ('color', 'silhouette'):
            rgba(out / 'native_current/matrix' / frame[key], production / 'native_final/matrix' / frame[key]); count += 1
    assert count == 89
    previous = ROOT / 'docs/research/creature-r6-v2/envelopes/penguin_belly_v2/production/native_final/fp'
    current = production / 'native_final/fp'; fp_count = 0
    for a, b in zip(read(previous / 'manifest.json')['resources'], read(current / 'manifest.json')['resources']):
        assert a['id'] == b['id'] and len(a['frames']) == len(b['frames'])
        for x, y in zip(a['frames'], b['frames']):
            assert x['png'] == y['png']; rgba(previous / x['png'], current / y['png']); fp_count += 1
    assert fp_count == 936
    assert read(production / 'fp_audit.json')['passed'] and read(production / 'audit_production_final.json')['passed']
    ledger = read(out / 'ledger_after_publish.json')['summary']
    for key in ('exact_valid_r5_reviews', 'published_payload_matches_staged', 'bin_payload_matches_staged', 'current_generator_source_inputs', 'valid_file_records'): assert ledger[key] == 61
    assert ledger['runtime_global_manifest_matches']
    report = {'passed': True, 'scope': 'One normal-pipeline R5 lobster anatomy/bind revision, full61 sampled GPU regression and isolated FP/entity persistence; not final R6 or complete compatibility/terrain/performance',
              'exe_sha256': context['exe_sha256'], 'candidate_to_production_rgba_exact': count,
              'body_rgba_exact': body['exact_rgba_png_count'], 'expected_changed_lobster_png': len(body['expected_changed_frames']),
              'fp_rgba_exact': fp_count, 'ledger_fresh': 61, 'joints_retained': 51, 'refit_bind_nodes': 18,
              'angular_channels_exact': 90, 'foot_y_max_error_blocks': rig['foot_joint_y_max_error_blocks'],
              'review_sha256': sha(out / 'revision_review.json'), 'snapshot_sha256': sha(snapshot / 'snapshot_manifest.json'),
              'input_reports_sha256': {p: sha(production / p) for p in ('body_parity.json', 'fp_audit.json', 'audit_production_final.json')}}
    (out / 'publication_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
