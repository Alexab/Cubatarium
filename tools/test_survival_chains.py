"""Check that the published baseline can be reached from actual natural drops."""
from pathlib import Path
import json
import math
ROOT=Path(__file__).resolve().parents[1]

def main():
    items={p.stem:json.loads(p.read_text()) for p in (ROOT/'content/items').glob('*.json')}
    recipes=[json.loads(p.read_text()) for p in (ROOT/'content/recipes').glob('*.json')]
    # Exact natural sources handled by BlockDrops and the creature drop tables.
    reachable={'wood','stone','plant_fiber','seeds','raw_copper','raw_iron','raw_gold','coal','grain','raw_meat','leather'}
    stations=set()
    changed=True
    while changed:
        before=(len(reachable),len(stations))
        for r in recipes:
            if all(i['id'] in reachable or any(a in reachable for a in i.get('alternatives',[])) for i in r['inputs']) and (not r.get('station') or r['station'] in stations):
                reachable.add(r['output']['id'])
        stations|=reachable & {'crafting_table','furnace','anvil'}
        changed=before!=(len(reachable),len(stations))
    required={f'{tier}_{tool}' for tier in ['wood','stone','copper','iron','gold'] for tool in ['pickaxe','axe','shovel','hoe','sword']}
    required|={f'{tier}_{slot}' for tier in ['leather','copper','iron','gold'] for slot in ['head','chest','legs','feet']}
    required|={'crafting_table','furnace','anvil','copper_ingot','iron_ingot','gold_ingot','bucket','waterskin','shears','flint_and_steel','wood_bow','wood_spear','arrow','wood_shield','iron_shield','wood_torch','bread','cooked_meat'}
    assert not (required-reachable),f'Unreachable baseline: {sorted(required-reachable)}'
    for id,d in items.items():
        p=d['physical']
        assert math.isfinite(p['mass']) and p['mass']>0,id
        assert len(p['size'])==3 and all(math.isfinite(v) and v>0 for v in p['size']),id
        if any(t in d['types'] for t in ['armor','mining','cutting','digging']):assert d['stack_max']==1,id
    blocks={}
    for p in (ROOT/'resource_packs').glob('*/blocks/*.json'):
        d=json.loads(p.read_text(encoding='utf-8-sig'));blocks.setdefault(d.get('name'),[]).append(d)
    for r in recipes:
        assert r['output']['id'] in items or r['output']['id'] in blocks,r['id']
        assert r['output']['count']>0 and all(i['count']>0 and (i['id'] in items|blocks or (i.get('alternatives') and all(a in items for a in i['alternatives']))) for i in r['inputs']),r['id']
    for stage in ['sprout','young','ripe']:
        d=blocks['cu_crop_'+stage][0]
        assert type(d['id']) is int,d['name']
        assert (ROOT/'resource_packs/cubatarium_worldgen_cc0/textures/blocks'/(d['name']+'.png')).is_file(),d['name']
        assert d['render']['style']=='cross' and d['physics']['movement']['occupancy']==0
        assert all(other.get('id')!=d['id'] for name,definitions in blocks.items() if name!=d['name'] for other in definitions),d['name']
    print(f'PASS: {len(required)} reachable results, {len(items)} physical profiles, 3 crop stages')
    return 0
if __name__=='__main__':raise SystemExit(main())
