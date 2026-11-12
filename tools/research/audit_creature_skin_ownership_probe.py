"""Audit bundled skin ownership/effective-selection evidence, not full appearance."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', default='skin_catalog')
    parser.add_argument('--native-dir', default='native_skin')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    context_path = args.out / f'context_{args.label}.json'
    context = read(context_path)
    root = Path(context['models_root'])
    assert context['native_resource_cache_probe'] and not context['qa_models_override']
    assert digest(repo / 'bin/Cubatarium.exe') == context['exe_sha256']
    for file, sha in context['files_sha256'].items():
        assert digest(root / file) == sha, file
    for file, sha in context['shaders_sha256'].items():
        assert digest(repo / file) == sha, file
    source_skins = {p.parent.name: read(p) for p in (root / 'skins').glob('*/skin.json')}
    assert len(source_skins) == 5
    definitions = {p.parent.name: read(p) for p in (root / 'creatures').glob('*/creature.json')}
    assert len(definitions) == 56 and set(context['preview_species']) == definitions.keys()
    path = args.out / args.native_dir / 'appearance_skin_probe.json'
    report = read(path)
    assert path.stat().st_mtime >= datetime.fromisoformat(context['captured_at_utc']).timestamp()
    rows = {(r['species'], r['skin'], r['raw_selection']): r for r in report['cases']}
    expected = {(sid, skin, raw) for sid in definitions for skin in source_skins for raw in (True, False)}
    assert len(rows) == len(report['cases']) == 560 and rows.keys() == expected
    compatible_count = 0
    skin_sources = {}
    for (sid, skin_id, raw), row in rows.items():
        skin = source_skins[skin_id]
        compatible = skin.get('creature_id', '') in ('', sid)
        assert row['compatible'] == compatible and row['passed']
        assert row['draws'] == len(row['textures']) == len(row['base_textures']) > 0
        assert all(t > 0 for t in row['textures'])
        if compatible:
            compatible_count += 1
            assert definitions[sid]['visual']['backend'] == 'bone_skeleton'
            assert definitions[sid]['visual'].get('texture', 'diffuse') == 'diffuse'
            key = f'skin/{skin_id}/diffuse'
            source = root / 'skins' / skin_id / 'textures/diffuse.png'
            skin_sources[key] = digest(source)
            assert row['expected_skin_texture'] > 0
            assert row['textures'] == [row['expected_skin_texture']] * row['draws']
            assert row['textures'] != row['base_textures']
        else:
            assert row['expected_skin_texture'] == 0
            assert row['textures'] == row['base_textures']
            assert row['actual_hash'] == row['base_hash']
        sibling = rows[(sid, skin_id, not raw)]
        assert row['textures'] == sibling['textures'] and row['actual_hash'] == sibling['actual_hash']
    assert compatible_count == 10 and report['passed']
    summary = {
        'schema': 'creature-skin-ownership-evidence-audit.v1', 'passed': True,
        'scope': 'Bundled 56x5x2 actual draw texture IDs and foreign-skin pose/mesh hashes; not full texture-map/FP/save/pack acceptance',
        'context': str(context_path), 'native_probe': str(path),
        'context_sha256': digest(context_path), 'probe_sha256': digest(path),
        'exe_sha256': context['exe_sha256'], 'current_runtime_inputs_verified': True,
        'cases': 560, 'compatible_cases': 10, 'foreign_cases': 550,
        'raw_and_resolved_only_selection_identical': True,
        'skin_atlas_sources_sha256': skin_sources,
    }
    (args.out / 'skin_ownership_audit.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
