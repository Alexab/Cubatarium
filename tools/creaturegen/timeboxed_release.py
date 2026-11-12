"""Explicit limited-scope catalog publication; never grants R5/R6 art approval.

The strict quality-review path is unchanged. A bounded release needs its own
hashed61-resource decision and native image evidence, with a deferred backlog.
"""
import json
from pathlib import Path
from PIL import Image
from creaturegen.pipeline import (ROOT,MODELS,SKINS,STAGING,SKIN_PLANS,creature_ids,
    validate_staging,runtime_definition,_merge_tree,_drop_stale_textures,
    _drop_stale_model_files,_sync_bin,write_asset_manifest)
from creaturegen.provenance import source_context,canonical_hash
from creaturegen.quality import bundle_hash,published_payload_matches
from creaturegen.hashutil import file_hash,write_json


def publish_timeboxed_release(review_path: Path) -> dict:
    review_path=review_path.resolve();review=json.loads(review_path.read_text(encoding='utf8'))
    if review.get('schema')!='creature-timeboxed-release.v1' or review.get('r6_complete') is not False:
        raise ValueError('A bounded release must explicitly leave full R6 incomplete')
    if not review.get('reviewed_by') or not review.get('excluded_scope'):
        raise ValueError('Missing reviewer or explicit deferred scope')
    backlog=(review_path.parent/review['backlog']['path']).resolve()
    if not backlog.is_file()or file_hash(backlog)!=review['backlog']['sha256']:
        raise ValueError('Missing or changed deferred backlog')
    expected=set(creature_ids())|set(SKIN_PLANS)
    if set(review.get('resources',{}))!=expected:raise ValueError('Release must cover all56 species and5 skins')
    context_file=(review_path.parent/review['native_context']['path']).resolve()
    if file_hash(context_file)!=review['native_context']['sha256']:raise ValueError('Changed native capture context')
    context=json.loads(context_file.read_text());candidate=Path(context['models_root'])
    if context['exe_sha256']!=file_hash(ROOT/'bin/Cubatarium.exe'):raise ValueError('Native capture used another Release')
    result_file=(review_path.parent/review['runtime_result']['path']).resolve()
    if file_hash(result_file)!=review['runtime_result']['sha256']or not json.loads(result_file.read_text())['process_success']:
        raise ValueError('Missing successful world smoke')
    prepared=[]
    for sid in sorted(expected):
        skin=sid in SKIN_PLANS;base=SKIN_PLANS[sid][0]if skin else sid
        stage=STAGING/'skins'/sid if skin else STAGING/sid
        entry=review['resources'][sid];manifest=json.loads((stage/'manifest.json').read_text())
        if entry.get('decision')!='accept_bounded' or entry.get('bundle_sha256')!=bundle_hash(stage):
            raise ValueError('Missing/stale bounded decision: '+sid)
        errors=validate_staging(sid,stage)
        if errors:raise ValueError(sid+': '+str(errors))
        context=source_context(ROOT,base)
        if any(manifest.get(k)!=v for k,v in context.items()):raise ValueError('Stale source provenance: '+sid)
        prefix=('skins/' if skin else 'creatures/')+sid+'/'
        capture=json.loads(context_file.read_text())
        for record in manifest['files']:
            key=(prefix+record['path']).replace('/','\\')
            if capture['files_sha256'].get(key)!=file_hash(stage/record['path']):
                raise ValueError('Native capture does not match staged payload: '+sid+'/'+record['path'])
        image=(review_path.parent/entry['native_image']['path']).resolve()
        if file_hash(image)!=entry['native_image']['sha256']:raise ValueError('Changed native evidence: '+sid)
        with Image.open(image)as im:
            im.load()
            if min(im.size)<64:raise ValueError('Unreadable native evidence: '+sid)
        planned=None if skin else runtime_definition(sid,stage)
        if planned is not None and canonical_hash(planned)!=manifest['runtime_definition_sha256']:
            raise ValueError('Changed runtime definition: '+sid)
        if planned is not None and canonical_hash(json.loads((candidate/'creatures'/sid/'creature.json').read_text()))!=canonical_hash(planned):
            raise ValueError('Native runtime metadata differs: '+sid)
        prepared.append((sid,skin,stage,manifest,planned))
    # All checks precede the first write to the published catalog.
    for sid,skin,stage,manifest,planned in prepared:
        destination=(SKINS if skin else MODELS)/sid
        _merge_tree(stage,destination);_drop_stale_textures(stage,destination)
        if not skin:
            _drop_stale_model_files(stage,destination)
            write_json(destination/'creature.json',planned);_sync_bin(sid)
        else:_merge_tree(destination,ROOT/'bin/models/skins'/sid)
        if not published_payload_matches(stage,destination):raise RuntimeError('Publication payload mismatch: '+sid)
        manifest.update(state='published_bounded',bounded_release_bundle_sha256=bundle_hash(stage),
            bounded_release_review_path=review_path.relative_to(ROOT).as_posix(),r6_final_acceptance='deferred')
        write_json(stage/'manifest.json',manifest)
    write_asset_manifest()
    return {'published_species':56,'published_skins':5,'scope':'bounded release, not full R5/R6 acceptance'}
