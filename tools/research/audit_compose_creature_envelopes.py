"""Audit native original/current captures and compose fixed-scale collision overlays.

Green checks here mean evidence consistency, never collision or art acceptance.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from review_creature_redesign import ROOT


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def matrix(values):
    return np.array(values).reshape(4, 4).T


def overlay(folder, frame, collision, size, light=False):
    with Image.open(folder / frame['native']['png']) as im:
        image = im.convert('RGBA')
    background = Image.new('RGBA', image.size, (226, 230, 234, 255) if light else (29, 34, 41, 255))
    background.alpha_composite(image)
    draw = ImageDraw.Draw(background)
    vp = matrix(frame['observed']['view_projection'])
    x, y, z = collision
    corners = np.array([[a * x / 2, b * y, c * z / 2, 1.]
                        for a in (-1, 1) for b in (0, 1) for c in (-1, 1)])
    points = corners @ vp.T
    points = points[:, :2] / points[:, 3:]
    points = np.column_stack([(points[:, 0] + 1) * 128, (1 - points[:, 1]) * 128])
    for a in range(8):
        for delta in (1, 2, 4):
            b = a ^ delta
            if a < b:
                draw.line([tuple(points[a]), tuple(points[b])], fill=(12, 145, 65, 255) if light else (67, 242, 132, 255), width=1)
    return background.convert('RGB').resize((size, size), Image.Resampling.NEAREST)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--light-background', action='store_true', help='Neutral light backing for dark source skins')
    parser.add_argument('--require-visual-envelope', action='store_true', help='Require the new conservative native bounds and independent edge recovery')
    parser.add_argument('--oblique-only', action='store_true', help='Compose only the motion sheet for an explicit oblique-only temporal profile; all captured bounds still audited')
    args = parser.parse_args()
    out = args.out.resolve()
    profile_file = out / 'native_profile.json'
    profile = read(profile_file)
    offline = read(out / 'offline_metrics.json')
    contexts = {name: read(out / f'context_envelope_{label}.json')
                for name, label in [('baseline', 'original'), ('current', 'current')]}
    manifests = {name: read(out / f'native_{label}/envelopes/manifest.json')
                 for name, label in [('baseline', 'original'), ('current', 'current')]}
    assert contexts['baseline']['exe_sha256'] == contexts['current']['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
    assert contexts['baseline']['qa_models_override']
    assert Path(contexts['current']['models_root']).resolve() / 'creatures' == Path(offline.get('current_models_root', ROOT / 'models/creatures')).resolve() or (
        not contexts['current']['qa_models_override'] and Path(offline.get('current_models_root', ROOT / 'models/creatures')).resolve() == ROOT / 'models/creatures')
    assert contexts['baseline']['shaders_sha256'] == contexts['current']['shaders_sha256']
    assert set(profile['resources']) == set(contexts['baseline']['preview_species']) == set(contexts['current']['preview_species'])
    files, errors, witnesses, native_images = [], [], [], []
    resources = {name: {r['id']: r for r in manifest['resources']} for name, manifest in manifests.items()}
    for variant, context in contexts.items():
        assert context['native_envelope_variant'] == variant
        assert context['envelope_profile_sha256'] == sha(profile_file)
        assert context['performance_gate'] == 'disabled'
        for name, digest in context['files_sha256'].items():
            assert sha(Path(context['models_root']) / name) == digest, name
        assert set(resources[variant]) == set(profile['resources'])
        folder = out / ('native_original' if variant == 'baseline' else 'native_current') / 'envelopes'
        for sid, resource in resources[variant].items():
            if args.require_visual_envelope:
                render_low, render_high = np.array(resource['render_low']), np.array(resource['render_high'])
                assert np.isfinite(render_low).all() and np.isfinite(render_high).all()
                assert resource['model_render_radius'] > 0
            expected = profile['resources'][sid]
            assert len(resource['frames']) == len(expected['frames'])
            assert resource['framing_low'] == [float(np.float32(v)) for v in expected['framing_low']]
            assert resource['framing_high'] == [float(np.float32(v)) for v in expected['framing_high']]
            for frame, planned in zip(resource['frames'], expected['frames']):
                for key in planned:
                    assert frame[key] == planned[key], (sid, key)
                assert frame['clip'] == planned['expected_clips'][variant]
                values = offline['resources'][sid][variant]['clips'][frame['clip']]['samples']
                sampled = min(values, key=lambda s: abs(s['phase'] - frame['effective_phase']))
                error = max(float(np.max(np.abs(np.array(sampled[k]) - frame['observed'][k]))) for k in ('low', 'high'))
                assert error < .001, (sid, frame['state'], frame['phase'], error)
                errors.append(error)
                if args.require_visual_envelope:
                    assert frame['observed']['visual_envelope_covers_geometry']
                    assert (np.array(frame['observed']['low']) >= render_low - .0001).all()
                    assert (np.array(frame['observed']['high']) <= render_high + .0001).all()
                path = folder / frame['native']['png']
                assert path.stat().st_mtime >= datetime.fromisoformat(context['captured_at_utc']).timestamp()
                with Image.open(path) as im:
                    assert im.size == (256, 256)
                    alpha = np.asarray(im.convert('RGBA'))[:, :, 3]
                    assert np.count_nonzero(alpha) == frame['native']['alpha_pixels'] > 0
                    assert not (alpha[0].any() or alpha[-1].any() or alpha[:, 0].any() or alpha[:, -1].any()), path
                native_images.append({'file': path.relative_to(out).as_posix(), 'sha256': sha(path)})
            edge = resource['culling_edge']
            if edge:
                path = folder / edge['native']['png']
                with Image.open(path) as im:
                    coverage = int(np.count_nonzero(np.asarray(im.convert('RGBA'))[:, :, 3]))
                assert coverage == edge['actual_visible_pixels'] == edge['native']['alpha_pixels']
                assert edge['confirmed_false_cull'] == (edge['old_collision_culling_rejects'] and coverage > 0)
                # Independent six-plane test of the exact projection and collider.
                vp = matrix(edge['observed']['view_projection'])
                planes = [vp[3] + vp[a] * sign for a in range(3) for sign in (-1, 1)]
                x, y, z = resource['collision_max']
                corners = np.array([[a * x / 2, b * y, c * z / 2, 1.]
                                    for a in (-1, 1) for b in (0, 1) for c in (-1, 1)])
                rejects = any(np.max(corners @ plane) < 0 for plane in planes)
                assert rejects == edge['old_collision_culling_rejects']
                if args.require_visual_envelope:
                    render_corners = np.array([[x, y, z, 1.] for x in (render_low[0], render_high[0])
                        for y in (render_low[1], render_high[1]) for z in (render_low[2], render_high[2])])
                    new_rejects = any(np.max(render_corners @ plane) < 0 for plane in planes)
                    assert new_rejects == edge['new_visual_culling_rejects']
                    assert not (new_rejects and coverage > 0), (sid, variant, 'visible mesh culled')
                witnesses.append({'id': sid, 'variant': variant, 'confirmed_false_cull': edge['confirmed_false_cull'],
                                  'new_visual_culling_rejects': edge.get('new_visual_culling_rejects'),
                                  'alpha_pixels': coverage, 'file': path.relative_to(out).as_posix(), 'sha256': sha(path)})
    sheets = out / 'sheets'
    sheets.mkdir(exist_ok=True)
    font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 15)
    heading = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 19)
    for sid in profile['resources']:
        before, after = resources['baseline'][sid], resources['current'][sid]
        for a, b in zip(before['frames'], after['frames']):
            assert a['observed']['view_projection'] == b['observed']['view_projection'], 'Paired framing differs'
        states = list(dict.fromkeys(f['state'] for f in before['frames']))
        # Every state at four phases, native oblique view, original/own pair.
        tile, rowheight = 224, 258
        sheet = Image.new('RGB', (tile * 4, 55 + len(states) * rowheight * 2), (21, 25, 31))
        draw = ImageDraw.Draw(sheet)
        draw.text((10, 7), sid + ' | native original / own | equal world framing', font=heading, fill='white')
        draw.text((10, 31), 'Green = unchanged gameplay collider; overlay is diagnostic, not acceptance', font=font, fill=(67, 242, 132))
        for state_index, state in enumerate(states):
            for version_index, variant in enumerate(('baseline', 'current')):
                resource = resources[variant][sid]
                folder = out / ('native_original' if variant == 'baseline' else 'native_current') / 'envelopes'
                y = 55 + (state_index * 2 + version_index) * rowheight
                for col, phase in enumerate((0., .25, .5, .75)):
                    frame = next(f for f in resource['frames'] if f['state'] == state and f['phase'] == phase and f['view'] == 'oblique')
                    sheet.paste(overlay(folder, frame, resource['collision_max'], tile, args.light_background), (col * tile, y + 32))
                    draw.text((col * tile + 4, y + 3), f"{variant} {state}/{frame['clip']} {phase:.2f}", font=font, fill='white')
        path = sheets / (sid + '_motion.png')
        sheet.save(path)
        files.append({'id': sid, 'kind': 'motion', 'file': path.relative_to(out).as_posix(), 'sha256': sha(path)})
        if args.oblique_only:
            continue
        # Front/side views at the same phase make width/depth differences legible.
        tile, rowheight = 192, 224
        sheet = Image.new('RGB', (tile * len(states), 55 + rowheight * 4), (21, 25, 31))
        draw = ImageDraw.Draw(sheet)
        draw.text((10, 7), sid + ' | front / side | original above own', font=heading, fill='white')
        draw.text((10, 31), 'Same camera per pair; source PNG/rig/clips untouched by this audit', font=font, fill=(200, 207, 215))
        for view_index, view in enumerate(('front', 'side')):
            for version_index, variant in enumerate(('baseline', 'current')):
                resource = resources[variant][sid]
                folder = out / ('native_original' if variant == 'baseline' else 'native_current') / 'envelopes'
                y = 55 + (view_index * 2 + version_index) * rowheight
                for col, state in enumerate(states):
                    phase = .5 if state == 'Action' else 0. if state == 'Idle' else .25
                    frame = next(f for f in resource['frames'] if f['state'] == state and f['phase'] == phase and f['view'] == view)
                    sheet.paste(overlay(folder, frame, resource['collision_max'], tile, args.light_background), (col * tile, y + 30))
                    draw.text((col * tile + 3, y + 3), f'{variant} {state} {phase:.2f}', font=font, fill='white')
        path = sheets / (sid + '_views.png')
        sheet.save(path)
        files.append({'id': sid, 'kind': 'front_side', 'file': path.relative_to(out).as_posix(), 'sha256': sha(path)})
    report = {'schema': 'creature-native-envelope-evidence-audit.v1', 'evidence_consistent': True,
              'scope': manifests['current']['scope'], 'art_acceptance': 'pending', 'contact_acceptance': 'pending',
              'exe_sha256': contexts['current']['exe_sha256'], 'native_main_images': len(native_images),
              'sheet_background': 'neutral_light' if args.light_background else 'dark',
              'max_offline_native_bounds_error_blocks': max(errors),
              'profile_sha256': sha(profile_file), 'offline_metrics_sha256': sha(out / 'offline_metrics.json'),
              'confirmed_false_culls': {v: sum(w['confirmed_false_cull'] for w in witnesses if w['variant'] == v) for v in contexts},
              'visual_envelope_required': args.require_visual_envelope,
              'oblique_only_composition': args.oblique_only,
              'recovered_false_culls': {v: sum(w['confirmed_false_cull'] and w['new_visual_culling_rejects'] is False
                  for w in witnesses if w['variant'] == v) for v in contexts} if args.require_visual_envelope else None,
              'sheets': files, 'culling_witnesses': witnesses, 'native_frames': native_images}
    (out / 'native_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('sheets', 'culling_witnesses', 'native_frames')}, indent=2))


if __name__ == '__main__':
    main()
