"""Join real automatic AIR demand jobs to computed/installed certificates."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def join_jobs(lines):
    jobs = {}
    for line in lines:
        event = re.search(r'\[RelightAudit\] (job_submit|worker_done|calculation_certificate) job=(\d+)\b', line)
        if not event:
            continue
        fields = {k:int(v) for k,v in re.findall(r'\b(\w+)=(-?\d+)\b', line)}
        source = re.search(r'\bsource=(\w+)', line)
        if source:
            fields['source'] = source.group(1)
        jobs.setdefault(int(event[2]), {})[event[1]] = fields
    joined = []
    for job, stages in jobs.items():
        submit = stages.get('job_submit', {})
        worker = stages.get('worker_done', {})
        cert = stages.get('calculation_certificate', {})
        requested = submit.get('air_requested', 0)
        if (submit.get('source') == 'light_sample_consumer' and requested > 0 and
                submit.get('samples_requested', 0) > 0 and
                submit.get('sky') == 1 and submit.get('block') == 1 and
                worker.get('inputs_complete') == 1 and worker.get('unknown_inputs') == 0 and
                worker.get('frontier_unfinished') == 0 and worker.get('read_set', 0) > 0 and
                cert.get('inputs_complete') == 1 and cert.get('include_sky') == 1 and
                cert.get('include_block') == 1 and cert.get('air_outputs', 0) >= requested and
                cert.get('air_complete', 0) == cert.get('air_outputs') and
                cert.get('sections', 0) >= cert.get('air_complete', 0) and
                worker.get('chunks', 0) >= cert.get('air_outputs', 0)):
            joined.append(dict(job=job, submit=submit, worker=worker, certificate=cert))
    return dict(schema='automatic-air-field-evidence.v1',
                status='PASS' if len(joined) >= 2 else 'UNTESTED',
                reason='joined_automatic_air_sample_creation' if len(joined) >= 2 else 'insufficient_complete_air_job_cohorts',
                scope='At least two actual automatic absent AIR sample jobs reached complete installed calculation receipts; no all-job terminal, retention duration, GPU or rendered claim',
                complete_cohorts=joined)


def evaluate(artifacts):
    paths = sorted((artifacts/'logs').glob('*.INFO.*'))
    result = join_jobs(line for p in paths for line in p.read_text(encoding='utf-8', errors='replace').splitlines())
    result['log_receipts'] = [dict(path=str(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in paths]
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = evaluate(args.artifacts)
    args.out.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('complete_cohorts','log_receipts')}))
    raise SystemExit(0 if result['status']=='PASS' else 1)
