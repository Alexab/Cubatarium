"""Verify a geometry-only reviewed revision reached production and regression captures."""
import argparse
import json
from pathlib import Path
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from creaturegen.quality import bundle_hash
from audit_creature_anatomy_pilot import sha, read


def rgba(a, b):
    with Image.open(a) as x, Image.open(b) as y:
        assert x.size == y.size and x.convert('RGBA').tobytes() == y.convert('RGBA').tobytes(), (a, b)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--species', required=True)
    parser.add_argument('--qa-name', required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--previous-out', type=Path, required=True, help='Previous full production evidence directory')
    args = parser.parse_args(); out = args.out.resolve(); sid = args.species
    for value in (sid, args.qa_name): assert Path(value).name == value and value not in ('.', '..')
    qa = ROOT / 'assetgen_output/creatures' / args.qa_name
    snapshot = args.snapshot.resolve()
    for name, digest in read(snapshot / 'snapshot_manifest.json')['files_sha256'].items():
        assert sha(snapshot / name) == digest
    assert (ROOT / 'tools/creature_recipes' / (sid + '.yaml')).read_bytes() == (qa / 'recipes' / (sid + '.yaml')).read_bytes()
    proposal = qa / 'models/creatures' / sid
    for base in ('models/creatures', 'bin/models/creatures', 'assetgen_output/creatures/staging'):
        folder = ROOT / base / sid
        assert read(folder / 'model.gltf') == read(proposal / 'model.gltf')
        assert (folder / 'model.bin').read_bytes() == (proposal / 'model.bin').read_bytes()
        for p in (proposal / 'textures').glob('*.png'): assert p.read_bytes() == (folder / 'textures' / p.name).read_bytes()
    assert read(ROOT / 'models/creatures' / sid / 'creature.json') == read(proposal / 'creature.json') == read(snapshot / 'models/creatures' / sid / 'creature.json')
    review = read(out / 'revision_review.json')['species'][sid]
    assert review['bundle_sha256'] == bundle_hash(ROOT / 'assetgen_output/creatures/staging' / sid)
    rig = read(out / 'rig_uv_native_audit.json')
    assert rig['passed'] and rig['nodes_skin_ibm_definition_joint_indices_weights_exact']
    for name, digest in rig['payload_sha256'].items(): assert sha(proposal / name) == digest
    production = out / 'production'; context = read(production / 'context_production_final.json')
    assert context['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe') and not context['qa_models_override']
    body = read(production / 'body_parity.json')
    assert body['expected_changed_resources'] == [sid]
    assert body['legacy_png_count'] + body['matrix_png_count'] == 5290
    assert body['exact_rgba_png_count'] + len(body['expected_changed_frames']) == 5290
    count = 0
    for i in range(29):
        rgba(out / 'native_current' / f'{sid}_{i}.png', production / 'native_final' / f'{sid}_{i}.png'); count += 1
    matrix = next(r for r in read(out / 'native_current/matrix/manifest.json')['resources'] if r['id'] == sid)
    assert len(matrix['frames']) == 30
    for frame in matrix['frames']:
        for key in ('color', 'silhouette'):
            rgba(out / 'native_current/matrix' / frame[key], production / 'native_final/matrix' / frame[key]); count += 1
    assert count == 89
    previous = args.previous_out.resolve() / 'native_final/fp'
    current = production / 'native_final/fp'; fp_count = 0
    old_resources, resources = read(previous / 'manifest.json')['resources'], read(current / 'manifest.json')['resources']
    assert len(old_resources) == len(resources) == 9
    for a, b in zip(old_resources, resources):
        assert a['id'] == b['id'] and len(a['frames']) == len(b['frames'])
        for x, y in zip(a['frames'], b['frames']):
            assert x['png'] == y['png']; rgba(previous / x['png'], current / y['png']); fp_count += 1
    assert fp_count == 936
    assert read(production / 'fp_audit.json')['passed'] and read(production / 'audit_production_final.json')['passed']
    ledger = read(out / 'ledger_after_publish.json')['summary']
    for key in ('exact_valid_r5_reviews', 'published_payload_matches_staged', 'bin_payload_matches_staged', 'current_generator_source_inputs', 'valid_file_records'): assert ledger[key] == 61
    assert ledger['runtime_global_manifest_matches']
    result = {'passed': True, 'species': sid, 'scope': 'One normal-pipeline geometry-only R5 revision, full61 sampled GPU regression and isolated FP/entity persistence; not complete R6',
        'exe_sha256': context['exe_sha256'], 'candidate_to_production_rgba_exact': count,
        'body_rgba_exact': body['exact_rgba_png_count'], 'changed_resource_frames': len(body['expected_changed_frames']),
        'fp_rgba_exact': fp_count, 'ledger_fresh': 61, 'animation_sampler_arrays_exact': rig['animation_sampler_arrays_exact'],
        'review_sha256': sha(out / 'revision_review.json'), 'snapshot_sha256': sha(snapshot / 'snapshot_manifest.json'),
        'input_reports_sha256': {p: sha(production / p) for p in ('body_parity.json', 'fp_audit.json', 'audit_production_final.json')}}
    (out / 'publication_audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__': main()
