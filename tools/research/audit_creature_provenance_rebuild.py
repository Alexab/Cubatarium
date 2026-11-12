"""Verify the 61-resource reproducibility report and preserved live payload."""
from pathlib import Path
import argparse
import hashlib
import json
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from creaturegen.provenance import source_context
from creaturegen.quality import bundle_hash


def files(root):
    return {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in root.rglob('*') if p.is_file()}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--published',action='store_true')
    a=p.parse_args()
    out=ROOT/'docs/research/creature-r6-v2/repro'
    report=json.loads((out/'summary.json').read_text(encoding='utf8'))
    assert report['two_builds_byte_identical'] and not report['unexpected_reads']
    archive=ROOT/'assetgen_output/creatures/pre_r6_provenance/models'
    if not a.published:
        assert files(archive)==files(ROOT/'models'),'Isolated builds changed the live models tree'
    rows=[]
    for r in report['resources']:
        kind,sid=r['kind'],r['id']
        stage=ROOT/'assetgen_output/creatures/r6_repro_v2/run_a/staging'/kind/sid
        manifest=json.loads((stage/'manifest.json').read_text(encoding='utf8'))
        assert r['files']==files(stage),sid
        assert r['bundle_sha256']==bundle_hash(stage),sid
        base=manifest.get('base_species',sid)
        for key,value in source_context(ROOT,base).items():assert manifest[key]==value,(sid,key)
        for rec in manifest['files']:
            assert r['files'][rec['path']]==rec['sha256'],(sid,rec['path'])
            assert rec['seed_hex']==f"{rec['seed']:016x}"
        # Compare all runtime files, including generated creature definitions;
        # only provenance/license files may differ from the accepted archive.
        generated=ROOT/'assetgen_output/creatures/r6_repro_v2/run_a/models'/kind/sid
        before=archive/kind/sid
        changed=[]
        for relative,digest in files(generated).items():
            if relative in ('manifest.json','LICENSE.txt'):continue
            assert files(before).get(relative)==digest,(sid,relative)
            if a.published:
                for tree in ('models','bin/models'):
                    assert hashlib.sha256((ROOT/tree/kind/sid/relative).read_bytes()).hexdigest()==digest,(tree,sid,relative)
        if a.published:
            for tree in ('models','bin/models'):
                assert (ROOT/tree/kind/sid/'LICENSE.txt').read_bytes()==(stage/'LICENSE.txt').read_bytes()
        rows.append({'id':sid,'kind':kind,'current_provenance':True,
            'generated_runtime_payload_identical_to_accepted_archive':True,
            'own_license_recorded':True,'file_records_verified':len(manifest['files'])})
    result={'schema':'creature-provenance-rebuild-audit.v1','resource_count':len(rows),
        'isolated_builds_left_live_models_tree_unchanged':True if not a.published else 'see audit_candidate.json',
        'published_source_and_bin_verified':a.published,'resources':rows}
    name='audit_published.json' if a.published else 'audit_candidate.json'
    (out/name).write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print('VERIFIED',len(rows),'resources')


if __name__=='__main__':main()
