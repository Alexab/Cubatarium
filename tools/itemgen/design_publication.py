"""Validated v3 publication with exclusive writer lock and crash-recoverable journal.

The manifest generation is the commit marker. Before it changes, recovery restores
all prior item directories and the prior manifest. After it changes, recovery
retains the committed generation and removes transaction scratch. Art acceptance
is separate; provenance records never automatically certify originality.
"""
from contextlib import contextmanager
from pathlib import Path
import json
import os
import re
import shutil
import uuid
import yaml
from itemgen.artifact_contract import digest,relative_file,validate_model
from itemgen.visual_bindings import prepare_bindings
from itemgen.wear_sections import prepare_sections
from itemgen.filesystem import replace_path

ROOT=Path(__file__).resolve().parents[2]


def checked_id(value):
    if not isinstance(value,str) or not re.fullmatch('[a-z0-9_]+',value):raise ValueError('Invalid item ID')
    return value


def _hashes(base):
    return {p.relative_to(base).as_posix():digest(p) for p in sorted(base.rglob('*')) if p.is_file()}


def validate_design(item_id, *, root=ROOT, staging=None):
    item_id=checked_id(item_id);root=Path(root).resolve()
    folder=Path(staging or root/'assetgen_output/items/redesign'/item_id).resolve()
    record=json.loads((folder/'design_build.json').read_text(encoding='utf-8'))
    if record.get('version')!=3 or record.get('id')!=item_id or record.get('generator',{}).get('version')!=3:
        raise ValueError('Rebuild with current versioned design builder before publication')
    for group in ('sources','definition'):
        if not record.get(group):raise ValueError(f'Missing {group} fingerprint')
        for name,value in record[group].items():
            if digest(relative_file(root,name))!=value:raise ValueError(f'Stale {group}: {name}')
    definition_path=f'content/items/{item_id}.json'
    if set(record['definition'])!={definition_path}:raise ValueError('Definition identity mismatch')
    source=root/'tools/item_designs'/item_id
    expected={p.relative_to(root).as_posix() for p in source.rglob('*') if p.is_file()}|{'tools/item_designs/provenance.json','LICENSE'}
    if set(record['sources'])!=expected:raise ValueError('Authored source set changed; rebuild design')
    dependencies={p.relative_to(ROOT).as_posix() for p in [*(ROOT/'tools/itemgen').glob('*.py'),ROOT/'shaders/item_surface.vert',ROOT/'shaders/item_surface.frag']}
    if set(record['generator'].get('sha256',{}))!=dependencies:raise ValueError('Generator dependency set changed; rebuild design')
    for name,value in record['generator']['sha256'].items():
        if digest(relative_file(root,name))!=value:raise ValueError(f'Stale generator: {name}')
    actual=_hashes(folder);actual.pop('design_build.json',None)
    if actual!=record.get('outputs'):raise ValueError('Staged output differs from validated build')
    provenance=json.loads((root/'tools/item_designs/provenance.json').read_text(encoding='utf-8'))['items'][item_id]
    if json.loads((folder/'ATTRIBUTION.json').read_text(encoding='utf-8'))!=provenance:raise ValueError('Attribution differs from explicit provenance source')
    for key in ('geometry','texture','design_card','license','license_source','origin_review','visual_independence_review'):
        if not provenance.get(key):raise ValueError(f'Missing explicit provenance field: {key}')
    if provenance['license_source']!='LICENSE' or (folder/'LICENSE.txt').read_bytes()!=(root/'LICENSE').read_bytes():raise ValueError('License source differs')
    recipe=yaml.safe_load((source/'design.yaml').read_text(encoding='utf-8'))
    if recipe.get('id')!=item_id or recipe.get('version')!=3:raise ValueError('Recipe identity mismatch')
    if 'geometry_source' in recipe:
        expected_source=f"tools/item_designs/{item_id}/{recipe['geometry_source']['path']}"
        if provenance['geometry'].get('authoring_format')!='manual-gltf-2.0' or provenance['geometry'].get('source')!=expected_source:
            raise ValueError('Update explicit provenance for the manual glTF source before publication')
    elif provenance['geometry'].get('source')!=f'tools/item_designs/{item_id}/design.yaml':
        raise ValueError('Generated geometry provenance must identify the active design recipe')
    budget=recipe['triangle_budget']
    if isinstance(budget,bool) or not isinstance(budget,int) or not 1<=budget<=2000:raise ValueError('Invalid declared item budget')
    inspected=validate_model(folder,budget=budget)
    if inspected['triangles']!=record.get('triangles') or inspected['resources']!=record.get('validated_resources'):raise ValueError('Build statistics do not match resources')
    g=json.loads((folder/'model.gltf').read_text(encoding='utf-8'))
    extras=g.get('extras',{})
    if extras.get('design_version')!=3:raise ValueError('Missing v3 item marker')
    parents={child:node.get('name','') for node in g['nodes'] for child in node.get('children',[])}
    nodes={node['name']:{'parent':parents.get(i,'') if parents.get(i,'')!=item_id else ''} for i,node in enumerate(g['nodes']) if node['name']!=item_id}
    prepare_bindings(extras.get('visual_bindings',[]),nodes,1,item_id)
    definition=json.loads((root/definition_path).read_text(encoding='utf-8'))
    prepare_sections(extras.get('wear_sections',[]),nodes,1,[0,0,0],definition.get('armor',{}).get('slots',[]))
    return dict(id=item_id,validated=True,folder=folder,record=record,provenance=provenance,**inspected)


@contextmanager
def _writer(root):
    folder=root/'assetgen_output/items';folder.mkdir(parents=True,exist_ok=True)
    with (folder/'.publication.lock').open('a+b') as lock:
        if os.fstat(lock.fileno()).st_size==0:lock.write(b'0');lock.flush()
        lock.seek(0)
        try:
            if os.name=='nt':
                import msvcrt
                msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(lock.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError as error:raise RuntimeError('Another item publication is active') from error
        try:yield
        finally:
            lock.seek(0)
            if os.name=='nt':msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
            else:fcntl.flock(lock.fileno(),fcntl.LOCK_UN)


def _atomic_bytes(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_suffix(path.suffix+'.tmp')
    with temporary.open('wb') as out:
        out.write(data);out.flush();os.fsync(out.fileno())
    os.replace(temporary,path)


def _atomic_json(path,data):
    _atomic_bytes(path,(json.dumps(data,indent=2)+'\n').encode('utf-8'))


def _remove_tree(path,scope):
    path=path.resolve();scope=scope.resolve()
    if path==scope or not path.is_relative_to(scope):raise ValueError('Unsafe publication cleanup path')
    if path.exists():shutil.rmtree(path)


def _recover_locked(root):
    journal=root/'models/items/PUBLICATION_TRANSACTION.json'
    if not journal.exists():return 'clean'
    state=json.loads(journal.read_text(encoding='utf-8'))
    token=state['generation']
    if state.get('version')!=1 or not isinstance(token,str) or not re.fullmatch('[0-9a-f]{32}',token):raise ValueError('Invalid publication journal')
    ids=state['ids']
    if not isinstance(ids,list) or not ids or len(ids)!=len(set(ids)):raise ValueError('Invalid publication journal IDs')
    for id in ids:checked_id(id)
    workspace=root/'assetgen_output/items/publications'/token
    if not workspace.is_dir():raise ValueError('Publication recovery backup is missing; retain journal for inspection')
    for id in ids:
        previous=workspace/'previous'/id;expected=state['previous'][id]
        if (expected is None and previous.exists()) or (expected is not None and (not previous.is_dir() or _hashes(previous)!=expected)):
            raise ValueError('Publication recovery snapshot is missing or changed')
    old=workspace/'manifest.previous.json'
    if state['had_manifest'] and (not old.is_file() or digest(old)!=state['previous_manifest_sha256']):raise ValueError('Previous manifest snapshot changed')
    manifest=root/'models/items/MANIFEST.json'
    committed=manifest.is_file() and digest(manifest)==state['manifest_sha256']
    if committed:
        current=json.loads(manifest.read_text(encoding='utf-8'))
        committed=all((root/name).is_file() and digest(root/name)==value for id in ids for name,value in current['items'][id]['sha256'].items() if name.startswith(f'models/items/{id}/'))
    if not committed:
        # Every destination has a complete snapshot before the journal is written.
        for id in ids:
            dest=root/'models/items'/id
            _remove_tree(dest,root/'models/items')
            previous=workspace/'previous'/id
            if previous.is_dir():shutil.copytree(previous,dest)
        old=workspace/'manifest.previous.json'
        if state['had_manifest']:
            _atomic_bytes(manifest,old.read_bytes())
        else:manifest.unlink(missing_ok=True)
    journal.unlink()
    _remove_tree(workspace,root/'assetgen_output/items/publications')
    return 'committed' if committed else 'rolled_back'


def recover_publication(*,root=ROOT):
    root=Path(root).resolve()
    with _writer(root):return _recover_locked(root)


def _install_directory(source,destination,displaced):
    if destination.exists():
        displaced.parent.mkdir(parents=True,exist_ok=True);replace_path(destination,displaced)
    replace_path(source,destination)


def publish_designs(item_ids, *, root=ROOT, staging=None):
    root=Path(root).resolve();ids=sorted(set(checked_id(id) for id in item_ids))
    if not ids:raise ValueError('No item designs selected')
    with _writer(root):
        _recover_locked(root)
        builds={id:validate_design(id,root=root,staging=Path(staging)/id if staging else None) for id in ids}
        manifest=root/'models/items/MANIFEST.json'
        before=json.loads(manifest.read_text(encoding='utf-8')) if manifest.exists() else {'items':{}}
        token=uuid.uuid4().hex;workspace=root/'assetgen_output/items/publications'/token
        workspace.mkdir(parents=True)
        try:
            next_manifest=json.loads(json.dumps(before));next_manifest.update(version=4,generator='itemgen-versioned',generation=token)
            next_manifest.pop('provenance',None) # No blanket originality claim for mixed generations.
            next_manifest.setdefault('items',{})
            for id,build in builds.items():
                destination=root/'models/items'/id;next_dir=workspace/'next'/id
                if destination.exists():
                    shutil.copytree(destination,workspace/'previous'/id)
                    shutil.copytree(destination,next_dir)
                else:next_dir.mkdir(parents=True)
                reference=next_dir/'_itemgen_reference'
                if destination.exists() and before.get('items',{}).get(id,{}).get('design_version')!=3:
                    if reference.exists():raise ValueError('Reserved baseline directory already exists; inspect before publication')
                    shutil.copytree(destination,reference)
                    _atomic_json(reference/'REFERENCE.json',dict(source='previous published runtime asset',sha256=_hashes(destination)))
                owned=set(before.get('items',{}).get(id,{}).get('owned_files',[]))
                for name in owned-set(build['record']['outputs'])-{'design_build.json'}:
                    old=relative_file(next_dir,name);old.unlink()
                for name in [*build['record']['outputs'],'design_build.json']:
                    source=relative_file(build['folder'],name);target=next_dir/name
                    target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
                record=build['record'];dependencies={**record['sources'],**record['definition'],**record['generator']['sha256']}
                hashes={**dependencies,**{f'models/items/{id}/{name}':value for name,value in _hashes(next_dir).items()}}
                next_manifest['items'][id]=dict(design_version=3,dependencies=sorted(dependencies),sha256=hashes,
                    owned_files=sorted([*record['outputs'],'design_build.json']),provenance=build['provenance'],
                    reference_model=f'models/items/{id}/_itemgen_reference/model.gltf' if (reference/'model.gltf').is_file() else None)
            if manifest.exists():shutil.copyfile(manifest,workspace/'manifest.previous.json')
            _atomic_json(workspace/'manifest.next.json',next_manifest)
            # Recheck frozen inputs after preparation, before any published file changes.
            for id in ids:validate_design(id,root=root,staging=builds[id]['folder'])
            journal=root/'models/items/PUBLICATION_TRANSACTION.json'
            _atomic_json(journal,dict(version=1,generation=token,ids=ids,had_manifest=manifest.exists(),manifest_sha256=digest(workspace/'manifest.next.json'),
                previous={id:_hashes(workspace/'previous'/id) if (workspace/'previous'/id).exists() else None for id in ids},
                previous_manifest_sha256=digest(manifest) if manifest.exists() else None))
            try:
                for id in ids:_install_directory(workspace/'next'/id,root/'models/items'/id,workspace/'displaced'/id)
                _atomic_json(manifest,next_manifest) # Commit marker, written last.
            except BaseException:
                _recover_locked(root)
                raise
            _recover_locked(root)
        except BaseException:
            # A real crash leaves the journal and its complete backup for recovery.
            if not (root/'models/items/PUBLICATION_TRANSACTION.json').exists():
                _remove_tree(workspace,root/'assetgen_output/items/publications')
            raise
    return dict(published=ids,generation=token,manifest=manifest)
