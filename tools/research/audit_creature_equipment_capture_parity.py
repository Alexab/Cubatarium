"""Exact native equipment parity across explicit renderer changes; no art decision."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for prefix in ('reference','candidate'):
        parser.add_argument('--'+prefix+'-out',type=Path,required=True)
        parser.add_argument('--'+prefix+'-label',required=True)
        parser.add_argument('--'+prefix+'-dir',required=True)
    parser.add_argument('--allow-executable-change',action='store_true')
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();contexts=[];folders=[]
    for prefix in ('reference','candidate'):
        out=getattr(args,prefix+'_out').resolve()
        contexts.append(json.loads((out/f"context_{getattr(args,prefix+'_label')}.json").read_text()))
        folders.append(out/getattr(args,prefix+'_dir')/'equipment')
    before,after=contexts
    assert before['exe_sha256']==after['exe_sha256'] or args.allow_executable_change
    assert before['shaders_sha256']==after['shaders_sha256']
    assert before['files_sha256']==after['files_sha256']
    assert before.get('equipment_material','copper')==after.get('equipment_material','copper')
    for name,digest in before['equipment_files_sha256'].items():
        assert after['equipment_files_sha256'][name]==digest,name
    runtime=Path(after['models_root']).parent
    for name,digest in after['equipment_files_sha256'].items():
        assert hashlib.sha256((runtime/name).read_bytes()).hexdigest()==digest,name
    manifests=[json.loads((folder/'manifest.json').read_text()) for folder in folders]
    assert manifests[0]==manifests[1]
    rows=[];core=0;views=0
    for resource in manifests[1]['resources']:
        for kind in ('frames','views'):
            for frame in resource.get(kind,[]):
                name=frame['png'];paths=[folder/name for folder in folders]
                for path,context in zip(paths,contexts):
                    assert path.stat().st_mtime>=datetime.fromisoformat(context['captured_at_utc']).timestamp()
                assert paths[0].read_bytes()==paths[1].read_bytes(),name
                rows.append({'path':name,'sha256':hashlib.sha256(paths[1].read_bytes()).hexdigest()})
                if kind=='frames':core+=1
                else:views+=1
    report={'schema':'creature-equipment-capture-parity.v1',
            'scope':'Exact sampled PNG/pose metadata and referenced inputs only; preserves prior direct observations, not new art/runtime acceptance',
            'exe_sha256':[c['exe_sha256'] for c in contexts],
            'reference_context':str(args.reference_out/f'context_{args.reference_label}.json'),
            'candidate_context':str(args.candidate_out/f'context_{args.candidate_label}.json'),
            'current_candidate_inputs_verified':True,'core_png_count':core,
            'supplemental_png_count':views,'exact_png_count':len(rows),'frames':rows}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='frames'},indent=2))


if __name__=='__main__':main()
