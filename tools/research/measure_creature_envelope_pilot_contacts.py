"""Dense pure-clip clearance evidence for isolated own motion proposals."""
import argparse
import hashlib
import json
from pathlib import Path
import yaml

from measure_creature_gltf_motion import measure, ROOT
from creaturegen.anatomy import build_model


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=1025)
    args = parser.parse_args()
    if args.samples < 257:
        parser.error('Use at least 257 samples per clip')
    result = {'schema': 'creature-proposal-pure-contact.v1',
              'scope': 'Offline pure asset clips; no fade, terrain, stride or art acceptance',
              'samples_per_clip': args.samples, 'species': {}}
    for sid in ('badger', 'treeman', 'lobster', 'seahorse'):
        recipe_path = args.catalog / 'recipes' / (sid + '.yaml')
        recipe = yaml.load(recipe_path.read_text(encoding='utf8'), Loader=yaml.CSafeLoader)
        folder = args.catalog / 'models' / 'creatures' / sid
        entry = {'clips': measure(folder, build_model(recipe), args.samples),
                 'source_sha256': {str(p.relative_to(args.catalog)): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in (recipe_path, folder / 'model.gltf', folder / 'model.bin')}}
        result['species'][sid] = entry
        print(sid, {k: v['min_geometry_y_blocks'] for k, v in entry['clips'].items()}, flush=True)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
