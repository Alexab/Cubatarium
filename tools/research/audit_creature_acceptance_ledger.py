"""Read-only R6 inventory/evidence audit. Never grants artistic acceptance."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
from collections import Counter

import yaml

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from creaturegen.quality import bundle_hash, published_payload_matches, review_errors
from creaturegen.pipeline import SKIN_PLANS
from creaturegen.provenance import source_context


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=ROOT/'docs/research/creature-r6-v2/ledger_before.json')
    args=parser.parse_args()
    catalog=yaml.safe_load((ROOT/'tools/creature_catalog_original.yaml').read_text(encoding='utf8'))['species']
    contracts=yaml.safe_load((ROOT/'tools/creature_quality_contracts.yaml').read_text(encoding='utf8'))['species']
    reviews={}
    for path in sorted((ROOT/'docs/research').rglob('*review*.json')):
        try:doc=json.loads(path.read_text(encoding='utf8'))
        except (ValueError,UnicodeError):continue
        if doc.get('schema')!='creature-quality-review.v1':continue
        for sid,entry in doc.get('species',{}).items():
            reviews.setdefault((sid,entry.get('bundle_sha256')),[]).append(path)
    manifest=json.loads((ROOT/'models/creatures/asset_manifest.json').read_text(encoding='utf8'))
    entries={x['id']:x for x in manifest['species']+manifest.get('skins',[])}
    rows=[]
    for kind,ids in [('species',catalog),('skins',SKIN_PLANS)]:
        for sid in sorted(ids):
            stage=ROOT/'assetgen_output/creatures/staging'/('skins' if kind=='skins' else '')/sid
            published=ROOT/'models'/('skins' if kind=='skins' else 'creatures')/sid
            row={'id':sid,'kind':kind,'base_species':SKIN_PLANS[sid][0] if kind=='skins' else sid}
            path=stage/'manifest.json'
            if not path.is_file():
                row['error']='missing staged provenance';rows.append(row);continue
            data=json.loads(path.read_text(encoding='utf8'));digest=bundle_hash(stage)
            exact=[];invalid={}
            for review in reviews.get((sid,digest),[]):
                errors=review_errors(sid,stage,review)
                if errors:invalid[str(review.relative_to(ROOT))]=errors
                else:exact.append(str(review.relative_to(ROOT)))
            stale=[]
            for source,wanted in data.get('source_inputs',{}).items():
                src=ROOT/source
                if not src.is_file() or hashlib.sha256(src.read_bytes()).hexdigest()!=wanted:stale.append(source)
            context=source_context(ROOT,row['base_species'])
            for key in ('toolchain','functional_contract_sha256'):
                if data.get(key)!=context[key]:stale.append(key)
            if data.get('source_inputs')!=context['source_inputs']:stale.append('complete_source_inputs')
            records_valid=all((stage/f['path']).is_file() and
                hashlib.sha256((stage/f['path']).read_bytes()).hexdigest()==f['sha256']
                for f in data.get('files',[])) and bool(data.get('files'))
            row.update(backend=data['backend'],bundle_sha256=digest,
                published_payload_matches_staged=published_payload_matches(stage,published),
                bin_payload_matches_staged=published_payload_matches(stage,ROOT/'bin/models'/('skins' if kind=='skins' else 'creatures')/sid),
                exact_valid_r5_reviews=exact,invalid_exact_reviews=invalid,
                current_generator_source_inputs=not stale,stale_source_inputs=stale,
                manifest_state=entries.get(sid,{}).get('state'),
                file_records_valid=records_valid,
                contract_state=contracts.get(sid,{}).get('review_state') if kind=='species' else None,
                r6_final_acceptance='unproven')
            rows.append(row)
    summary={'expected_species':len(catalog),'expected_skins':len(SKIN_PLANS),
        'exact_valid_r5_reviews':sum(bool(r.get('exact_valid_r5_reviews')) for r in rows),
        'published_payload_matches_staged':sum(r.get('published_payload_matches_staged',False) for r in rows),
        'bin_payload_matches_staged':sum(r.get('bin_payload_matches_staged',False) for r in rows),
        'current_generator_source_inputs':sum(r.get('current_generator_source_inputs',False) for r in rows),
        'manifest_species_count':len(manifest['species']),
        'manifest_skin_count':len(manifest.get('skins',[])),
        'valid_file_records':sum(r.get('file_records_valid',False) for r in rows),
        'runtime_global_manifest_matches':(ROOT/'models/creatures/asset_manifest.json').read_bytes()==(ROOT/'bin/models/creatures/asset_manifest.json').read_bytes(),
        'contracts_states':dict(Counter(contracts[sid].get('review_state') for sid in catalog)),
        'unexpected_live_species':sorted(p.parent.name for p in (ROOT/'models/creatures').glob('*/creature.json') if p.parent.name not in catalog)}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps({'schema':'creature-r6-ledger.v1','scope':'evidence integrity and source freshness, not final artistic/runtime acceptance','summary':summary,'resources':rows},indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    print(json.dumps(summary,indent=2,ensure_ascii=False))


if __name__=='__main__':main()
