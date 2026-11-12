"""Verify an isolated geometry-only proposal and compose three-way native evidence.

Can reuse unchanged references only with identical executable/profile/input
hashes. Does not grant artistic acceptance or publish a proposal.
"""
import argparse
import json
from pathlib import Path
import shutil
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from review_creature_redesign import ROOT, accessor
from audit_creature_motion_v5 import inspect, sha
from audit_compose_creature_envelopes import overlay


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def verified_context(out, filename):
    context = read(out / filename)
    assert context['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
    assert context['envelope_profile_sha256'] == sha(out / 'native_profile.json')
    for name, digest in context['files_sha256'].items():
        assert sha(Path(context['models_root']) / name) == digest, name
    for name, digest in context['shaders_sha256'].items():
        # Context shader keys include the bin directory.
        assert sha(ROOT / name) == digest, name
    return context


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name', required=True)
    parser.add_argument('--species', default='penguin')
    parser.add_argument('--states', default='Idle:0,Walk:.25,Fly:.25')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--reuse-reference-out', type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    assert out.is_relative_to((ROOT / 'docs/research').resolve())
    reused = []
    if args.reuse_reference_out:
        old = args.reuse_reference_out.resolve()
        assert sha(old / 'native_profile.json') == sha(out / 'native_profile.json')
        for native, context in [('native_original', 'context_envelope_original.json'), ('native_before', 'context_own_before.json')]:
            verified_context(old, context)
            assert not (out / native).exists() and not (out / context).exists()
            shutil.copytree(old / native, out / native)
            shutil.copy2(old / context, out / context)
            reused.append({'from': str(old), 'context': context, 'context_sha256': sha(old / context),
                           'native_manifest_sha256': sha(old / native / 'envelopes/manifest.json')})
        (out / 'reference_evidence_reuse.json').write_text(json.dumps(reused, indent=2) + '\n', encoding='utf-8')
    assert Path(args.species).name == args.species and args.species not in ('.', '..')
    before = ROOT / 'models/creatures' / args.species
    candidate = ROOT / 'assetgen_output/creatures' / args.qa_name / 'models/creatures' / args.species
    g, blob, stats = inspect(before)
    h, data, new = inspect(candidate)
    assert new['triangles'] == stats['triangles']
    assert new['constant_uv_triangles'] <= stats['constant_uv_triangles']
    assert g['nodes'] == h['nodes'] and g['skins'] == h['skins']
    assert (before / 'creature.json').read_bytes() == (candidate / 'creature.json').read_bytes()
    assert np.array_equal(accessor(g, blob, g['skins'][0]['inverseBindMatrices']), accessor(h, data, h['skins'][0]['inverseBindMatrices']))
    assert len(g['animations']) == len(h['animations'])
    tracks = 0
    for a, b in zip(g['animations'], h['animations']):
        assert a['name'] == b['name'] and a['channels'] == b['channels']
        assert len(a['samplers']) == len(b['samplers'])
        for s, t in zip(a['samplers'], b['samplers']):
            assert s.get('interpolation') == t.get('interpolation')
            for key in ('input', 'output'):
                assert np.array_equal(accessor(g, blob, s[key]), accessor(h, data, t[key]))
            tracks += 1
    assert len(g['meshes']) == len(h['meshes']) == 1
    a, b = g['meshes'][0]['primitives'], h['meshes'][0]['primitives']
    assert len(a) == len(b)
    for p, q in zip(a, b):
        for key in ('JOINTS_0', 'WEIGHTS_0'):
            assert np.array_equal(accessor(g, blob, p['attributes'][key]), accessor(h, data, q['attributes'][key]))
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 17)
    variants = [('original', 'native_original', 'context_envelope_original.json'),
                ('own before', 'native_before', 'context_own_before.json'),
                ('candidate', 'native_current', 'context_envelope_current.json')]
    resources = {}
    for label, native, context in variants:
        verified_context(out, context)
        resources[label] = read(out / native / 'envelopes/manifest.json')['resources'][0]
        assert resources[label]['id'] == args.species
    assert Path(read(out / 'context_envelope_current.json')['models_root']).resolve() == candidate.parent.parent.resolve()
    sheets = []
    for condition in args.states.split(','):
        state, value = condition.split(':'); phase = float(value)
        sheet = Image.new('RGB', (960, 990), (24, 29, 33))
        draw = ImageDraw.Draw(sheet)
        draw.text((8, 8), f'{args.species} {state} {phase}: original / own before / candidate', font=font, fill='white')
        draw.text((8, 30), 'Native Release, shared camera; green collider; candidate unapproved', font=font, fill='white')
        for row, (label, native, _) in enumerate(variants):
            resource = resources[label]
            assert resource['framing_low'] == resources['original']['framing_low']
            assert resource['framing_high'] == resources['original']['framing_high']
            for col, view in enumerate(('front', 'side', 'oblique')):
                frame = next(f for f in resource['frames'] if f['state'] == state and f['phase'] == phase and f['view'] == view)
                draw.text((col * 320 + 8, 60 + row * 308), label + ' | ' + view, font=font, fill='white')
                sheet.paste(overlay(out / native / 'envelopes', frame, resource['collision_max'], 288, True), (col * 320 + 16, 80 + row * 308))
        path = out / ('threeway_' + state.lower() + '.png')
        sheet.save(path)
        sheets.append({'path': path.name, 'sha256': sha(path)})
    report = {'passed': True, 'scope': 'Isolated candidate data invariants and native input consistency; not art/contact/performance acceptance',
              'animation_sampler_arrays_exact': tracks, 'nodes_skin_ibm_definition_joint_indices_weights_exact': True,
              'before_geometry': stats, 'candidate_geometry': new, 'sheets': sheets,
              'payload_sha256': {str(p.relative_to(candidate)): sha(p) for p in candidate.rglob('*') if p.is_file()}}
    transition_path = out / 'native_current/transition_probe.json'
    if transition_path.is_file():
        transitions = read(transition_path)
        resource = next(r for r in transitions['resources'] if r['id'] == args.species)
        frames = [f for c in resource['cases'] for f in c['frames']]
        supported = [f['low'][1] for c in resource['cases'] if c['supported'] for f in c['frames']]
        assert frames
        # Native CreatureQaTransitionProbe explicitly omits supported routes
        # for the free-swimming seahorse. Do not invent a floor verdict.
        if not supported:
            assert args.species == 'seahorse'
            assert all(not c['supported'] and c['in_fluid'] for c in resource['cases'])
        assert all(f['visual_envelope_covers_geometry'] for f in frames)
        if supported:
            assert min(supported) >= -.001
        report['sampled_native_cpu_transitions'] = {'cases': len(resource['cases']), 'frames': len(frames),
            'supported_frames': len(supported), 'supported_min_y': min(supported) if supported else None,
            'supported_route_scope': 'sampled' if supported else 'omitted by native free-swimmer fixture; no floor acceptance',
            'sha256': sha(transition_path), 'scope': 'Sampled retained UpdatePose; not terrain/stride or GPU fade acceptance'}
    (out / 'rig_uv_native_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Candidate invariant audit PASS; three unapproved native comparison sheets')


if __name__ == '__main__':
    main()
