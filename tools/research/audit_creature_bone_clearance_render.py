"""Bind new native captures to the bone-only runtime change; no art auto-approval."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    reference_out = out.parent/'formats'
    reference = reference_out/'native_production'
    current = out/'native_current'
    old_context = json.loads((reference_out/'context_production_catalog.json').read_text())
    new_context = json.loads((out/'context_current_catalog.json').read_text())
    assert old_context['files_sha256'] == new_context['files_sha256'], 'Assets changed'
    assert old_context['shaders_sha256'] == new_context['shaders_sha256']
    assert old_context['preview_species'] == new_context['preview_species']
    for name, digest in new_context['files_sha256'].items():
        assert hashlib.sha256((Path(new_context['models_root'])/name).read_bytes()).hexdigest() == digest
    audit = json.loads((out/'clearance_audit.json').read_text())
    allowed = {r['id'] for r in audit['resources'] if r['corrected_ground_scenarios']}
    manifests = [json.loads((folder/'matrix/manifest.json').read_text()) for folder in (reference, current)]
    assert manifests[0] == manifests[1], 'Pose/camera/state metadata changed'
    rows = []
    for r in manifests[1]['resources']:
        names = ['matrix/'+f[key] for f in r['frames'] for key in ('color', 'silhouette')]
        if not r['skin']:
            names += [f"{r['id']}_{i}.png" for i in range(35 if r['id']=='human' else 29)]
        changed = []
        for name in names:
            old, new = reference/name, current/name
            assert old.stat().st_mtime >= datetime.fromisoformat(old_context['captured_at_utc']).timestamp()
            assert new.stat().st_mtime >= datetime.fromisoformat(new_context['captured_at_utc']).timestamp()
            if old.read_bytes() != new.read_bytes():
                assert r['species'] in allowed, (r['id'], name, 'unrelated backend/profile changed')
                changed.append(name)
        rows.append({'id': r['id'], 'species': r['species'], 'png_count': len(names),
                     'changed_pngs': changed, 'all_pngs_byte_identical': not changed,
                     'native_bone_root_clearance_can_apply': r['species'] in allowed})
    report = {'schema': 'creature-bone-clearance-render-change.v1',
              'scope': 'Change confinement and resource freshness, not artistic acceptance',
              'model_files_and_shaders_unchanged': True,
              'exe_before': old_context['exe_sha256'], 'exe_after': new_context['exe_sha256'],
              'resource_count': len(rows), 'png_count': sum(r['png_count'] for r in rows),
              'byte_identical_resources': sum(r['all_pngs_byte_identical'] for r in rows),
              'changed_resource_ids': [r['id'] for r in rows if not r['all_pngs_byte_identical']],
              'resources': rows}
    (out/'render_change_audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'resources'}, indent=2))


if __name__ == '__main__':
    main()
