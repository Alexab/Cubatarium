"""Two isolated 56+5 builds with Python read tracing and exact reproducibility.

Only metadata/provenance may differ from the accepted runtime payload here.
This is evidence, never an automatic artistic acceptance decision.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from creaturegen import pipeline
from creaturegen.hashutil import write_json
from creaturegen.quality import bundle_hash


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=ROOT/'assetgen_output/creatures/r6_repro_v2')
    parser.add_argument('--report',type=Path,default=ROOT/'docs/research/creature-r6-v2/repro/summary.json')
    parser.add_argument('--allow-legacy-geometry-version-fix',action='store_true',help='allow only 1.12 -> 1.8 header correction with all other geometry JSON unchanged')
    args=parser.parse_args();output=args.out.resolve()
    if not output.is_relative_to((ROOT/'assetgen_output/creatures').resolve()):raise ValueError('Build output outside scratch')
    reads=set();violations=[];phase={'building':True}
    dependencies=[Path(sys.base_prefix).resolve(),Path(sys.prefix).resolve()]
    allowed_files={ROOT/'LICENSE',ROOT/'tools/creature_palettes.yaml',ROOT/'tools/creature_quality_contracts.yaml',
        ROOT/'tools/requirements-creaturegen.txt',ROOT/'tools/creature_catalog_original.yaml'}
    def audit(event,arguments):
        if event=='socket.connect':raise RuntimeError('Production asset build must not use network')
        if not phase['building']:return
        if event!='open' or not isinstance(arguments[0],(str,bytes,os.PathLike)):return
        mode=arguments[1];flags=arguments[2]
        readable=('r' in mode or '+' in mode) if isinstance(mode,str) else (flags&os.O_ACCMODE)!=os.O_WRONLY
        if not readable:return
        path=Path(os.fsdecode(arguments[0])).resolve()
        if path.is_relative_to(output):reads.add('OUTPUT/'+path.relative_to(output).as_posix());return
        if path.is_relative_to(ROOT):
            relative=path.relative_to(ROOT).as_posix()
            own_tools=path.is_relative_to(ROOT/'tools/creaturegen') or path.is_relative_to(ROOT/'tools/creature_recipes')
            definition=(path.name=='creature.json' and path.parent.parent==ROOT/'models/creatures') or (
                path.name=='skin.json' and path.parent.parent==ROOT/'models/skins')
            if path in allowed_files or own_tools or definition:
                reads.add(relative);return
        elif any(path.is_relative_to(prefix) for prefix in dependencies):
            return
        violations.append(str(path));raise RuntimeError('Unexpected production build input: '+str(path))
    sys.addaudithook(audit)
    results=[]
    for name in ('run_a','run_b'):
        phase['building']=True
        stage=output/name/'staging';models=output/name/'models'
        resources=[]
        for sid in pipeline.creature_ids():
            print(name+' species '+sid,flush=True)
            folder=pipeline.build_species(sid,stage/'creatures'/sid)
            errors=pipeline.validate_staging(sid,folder)
            if errors:raise RuntimeError(sid+': '+ '; '.join(errors))
            target=models/'creatures'/sid
            shutil.copytree(folder,target,dirs_exist_ok=True)
            write_json(target/'creature.json',pipeline.runtime_definition(sid,folder))
            resources.append(('creatures',sid,folder))
        for sid in pipeline.SKIN_PLANS:
            print(name+' skin '+sid,flush=True)
            folder=pipeline.build_skin(sid,stage/'skins'/sid)
            errors=pipeline.validate_staging(sid,folder)
            if errors:raise RuntimeError(sid+': '+ '; '.join(errors))
            shutil.copytree(folder,models/'skins'/sid,dirs_exist_ok=True)
            resources.append(('skins',sid,folder))
        phase['building']=False
        rows=[]
        for kind,sid,folder in resources:
            files={p.relative_to(folder).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(folder.rglob('*')) if p.is_file()}
            current=ROOT/'models'/kind/sid
            differences=[];header_fixes=[]
            for relative,digest in files.items():
                if relative in ('manifest.json','LICENSE.txt'):continue
                previous=current/relative
                if not previous.is_file() or hashlib.sha256(previous.read_bytes()).hexdigest()!=digest:
                    fixed=False
                    if args.allow_legacy_geometry_version_fix and relative=='geometry.geo.json' and previous.is_file():
                        before=json.loads(previous.read_text(encoding='utf8'));after=json.loads((folder/relative).read_text(encoding='utf8'))
                        old_version=before.pop('format_version',None);new_version=after.pop('format_version',None)
                        fixed=old_version=='1.12.0' and new_version=='1.8.0' and before==after
                        if fixed:header_fixes.append({'path':relative,'before':old_version,'after':new_version})
                    if not fixed:differences.append(relative)
            if differences:raise RuntimeError('Unexpected runtime payload change '+sid+': '+str(differences))
            rows.append({'id':sid,'kind':kind,'files':files,'bundle_sha256':bundle_hash(folder),
                'runtime_payload_unchanged_except_authorship':not header_fixes,
                'allowed_geometry_header_changes':header_fixes})
        results.append(rows)
    assert results[0]==results[1],'Two isolated builds differ (including manifests/file bytes)'
    assert len(results[0])==61
    report={'schema':'creature-catalog-reproducibility.v1','resource_count':61,'species_count':56,'skin_count':5,
        'two_builds_byte_identical':True,'metadata_only_relative_to_accepted_payload':True,
        'read_guard':'Python audit open events: own source/config and own output only; Python runtime dependencies allowed; network denied',
        'unexpected_reads':violations,'observed_project_reads':sorted(reads),
        'resources':results[0]}
    # Report is outside the guarded production build; remove no guard, write-only
    # evidence is permitted and does not become an authoring input.
    write_json(args.report,report)
    print('REPRODUCIBLE 61/61; no unexpected production reads',flush=True)


if __name__=='__main__':main()
