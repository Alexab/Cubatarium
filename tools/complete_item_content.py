"""Author the base survival chain and physical profiles. No imported art."""
from pathlib import Path
import hashlib
import json
import sys
import yaml
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from itemgen.builders import build_parts
from itemgen.pipeline import build_item

def write(path, data):
    if "name" in data and "blocks" in path.parts and path.exists():
        old=json.loads(path.read_text(encoding="utf-8-sig"))
        if "id" in old:data["id"]=int(old["id"])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')

def recipe(id, inputs, count=1, station='', category='Materials', seconds=0):
    ingredients=[dict(id=k,count=v,**({"alternatives":["coal","charcoal"]} if k=="fuel" else {})) for k,v in inputs]
    write(ROOT/'content/recipes'/f'{id}.json', dict(id=id, shapeless=True,
          inputs=ingredients, output=dict(id=id,count=count),
          station=station, category=category, seconds=seconds))

def part(role, offset, size): return dict(role=role,offset=offset,size=size)

def main():
    additions={
      'stick':('wood', [part('handle',[0,0,0],[.055,.55,.055]),part('binding',[0,-.15,0],[.07,.08,.07])]),
      'plant_fiber':('leather',[part('binding',[x,0,0],[.035,.3,.035]) for x in [-.06,0,.06]]),
      'cord':('leather',[part('binding',[0,0,z],[.28,.045,.045]) for z in [-.07,0,.07]]),
      'grain':('gold',[part('head',[x,y,0],[.065,.08,.05]) for x,y in [(-.05,-.04),(.05,-.04),(0,.04)]]),
      'wool':('leather',[part('head',[0,0,0],[.28,.25,.25]),part('binding',[0,0,0],[.32,.045,.29])]),
      'cooked_meat':('wood',[part('head',[0,0,0],[.26,.12,.18]),part('accent',[0,.065,0],[.12,.025,.12])]),
      'coal':('stone',[part('head',[0,0,0],[.2,.17,.16]),part('edge',[.07,.06,0],[.12,.1,.13])]),
      'charcoal':('wood',[part('head',[0,0,0],[.12,.28,.14]),part('binding',[0,-.04,0],[.15,.04,.17])]),
    }
    for metal in ['copper','iron','gold']:
        additions['raw_'+metal]=(metal,[part('head',[0,0,0],[.24,.19,.21]),part('edge',[.08,.08,-.04],[.13,.09,.1]),part('binding',[-.07,-.03,.08],[.12,.13,.07])])
        additions[metal+'_ingot']=(metal,[part('head',[0,0,0],[.32,.11,.16]),part('accent',[0,.06,0],[.16,.015,.09]),part('binding',[.11,0,0],[.025,.12,.17])])
    palettes={
      'coal':dict(head=[46,47,53],edge=[68,67,70],binding=[34,35,41]),
      'charcoal':dict(head=[53,45,43],edge=[76,61,51],binding=[32,29,29]),
      'plant_fiber':dict(head=[130,139,81],binding=[141,150,95],accent=[182,180,109]),
      'cord':dict(head=[158,131,92],binding=[164,141,105],accent=[187,158,113]),
      'wool':dict(head=[222,220,204],binding=[201,198,177],accent=[242,237,218]),
      'grain':dict(head=[208,168,75],binding=[161,124,61],accent=[231,191,93]),
      'cooked_meat':dict(head=[128,68,42],accent=[186,117,66],binding=[85,44,31]),
    }
    for metal,color in [('copper',[187,112,71]),('iron',[174,166,150]),('gold',[212,173,74])]:
        palettes['raw_'+metal]=dict(head=[88,86,86],binding=[66,63,64],edge=color,accent=color)
    palette_path=ROOT/'tools/item_materials.yaml'
    original=palette_path.read_text(encoding='utf-8');existing=yaml.safe_load(original)
    missing={key:value for key,value in palettes.items() if key not in existing}
    if missing:palette_path.write_text(original+'\n# Original survival materials: rough ore and organic fibers.\n'+yaml.safe_dump(missing,sort_keys=False,default_flow_style=None),encoding='utf-8')
    for id,(material,parts) in additions.items():
        write(ROOT/'content/items'/f'{id}.json',dict(id=id,displayName=id.replace('_',' ').title(),types=['misc', 'food' if id=='cooked_meat' else 'material'],stack_max=32,wear_end='indestructible',**({'use':dict(action='eat',satiety=32,duration=1.1),'use_action':'eat'} if id=='cooked_meat' else {})))
        data=dict(id=id,family='material',material=material,palette=id if id in palettes else material,recipe_version=2,parts=parts,provenance='Original Cubatarium recipe geometry and procedural texture')
        (ROOT/'tools/item_recipes'/f'{id}.yaml').write_text(yaml.safe_dump(data,sort_keys=False),encoding='utf-8')
        # build_parts is bypassed by the explicit parts below in pipeline v2.
        build_item(id)
    for path in (ROOT/'tools/item_recipes').glob('*.yaml'):
        r=yaml.safe_load(path.read_text());id=r['id'];p=ROOT/'content/items'/f'{id}.json'
        if not p.exists():continue
        d=json.loads(p.read_text());family=r['family'];material=r.get('material','wood')
        parts=r.get('parts') or build_parts(family,material)
        low=[min(a['offset'][i]-a['size'][i]/2 for a in parts) for i in range(3)]
        high=[max(a['offset'][i]+a['size'][i]/2 for a in parts) for i in range(3)]
        size=[round(max(.03,b-a),4) for a,b in zip(low,high)]
        mass={'wood':.8,'stone':1.8,'leather':.5,'copper':2.2,'iron':2.6,'gold':4.5}.get(material,1)
        if family.startswith('armor_'):mass*=dict(armor_head=.8,armor_chest=2,armor_legs=1.5,armor_feet=.7)[family]
        if id=='arrow':mass=.06
        if id=='grain' or id=='seeds':mass=.02
        if id=='wood_spear':d['types']=['combat','piercing'];d.setdefault('visual',{})['swing']={'melee':'spear_thrust'}
        presentation=d.setdefault('visual',{}).setdefault('presentations',{})
        for key in ['icon_scale','fp_scale','tp_scale','ground_scale']:presentation.setdefault(key,1.0)
        d['physical']=dict(mass=mass,size=size,hardness=.7 if material in ['wood','leather'] else 1)
        use=d.setdefault('use',{})
        if family=='hoe':use['interaction']='till'
        if id in ['bucket','waterskin']:use['interaction']='container'
        if id=='seeds':use['interaction']='plant'
        if id=='flint_and_steel':use['interaction']='ignite'
        if id=='shears':use['interaction']='shear'
        if d.get('repair') and material in ['copper','iron','gold']:d['repair']['materials']=[material+'_ingot']
        write(p,d)
    recipe('stick',[('wood',1)],4)
    recipe('cord',[('plant_fiber',3)],2)
    recipe('crafting_table',[('wood',4)],category='Stations')
    recipe('furnace',[('stone',8)],station='crafting_table',category='Stations')
    recipe('anvil',[('iron_ingot',12),('stone',3)],station='crafting_table',category='Stations')
    recipe('charcoal',[('wood',2)],station='furnace',seconds=4)
    for metal in ['copper','iron','gold']:
        recipe(metal+'_ingot',[('raw_'+metal,1),('fuel',1)],station='furnace',seconds=3)
        for family,n in [('pickaxe',3),('axe',3),('shovel',1),('hoe',2),('sword',2)]:recipe(metal+'_'+family,[(metal+'_ingot',n),('stick',2)],station='crafting_table',category='Tools')
        for slot,n in [('head',5),('chest',8),('legs',7),('feet',4)]:recipe(metal+'_'+slot,[(metal+'_ingot',n),('cord',1)],station='crafting_table',category='Armor')
    for tier,source in [('wood','wood'),('stone','stone')]:
        for family,n in [('pickaxe',3),('axe',3),('shovel',1),('hoe',2),('sword',2)]:recipe(tier+'_'+family,[(source,n),('stick',2)],station='' if tier=='wood' else 'crafting_table',category='Tools')
    for slot,n in [('head',3),('chest',5),('legs',4),('feet',2)]:recipe('leather_'+slot,[('leather',n),('cord',1)],station='crafting_table',category='Armor')
    for id,inputs,count in [('wood_bow',[('stick',3),('cord',3)],1),('arrow',[('stick',1),('stone',1),('plant_fiber',1)],4),('wood_spear',[('stick',2),('stone',1),('cord',1)],1),('wood_shield',[('wood',4),('cord',2)],1),('iron_shield',[('iron_ingot',4),('wood',2)],1),('bucket',[('iron_ingot',3)],1),('shears',[('iron_ingot',2)],1),('flint_and_steel',[('stone',1),('iron_ingot',1)],1),('waterskin',[('leather',2),('cord',1)],1),('wood_torch',[('stick',1),('charcoal',1)],4)]:recipe(id,inputs,count,station='crafting_table' if id not in ['arrow','wood_torch'] else '',category='Utility')
    recipe('bread',[('grain',3),('fuel',1)],station='furnace',category='Food',seconds=3)
    recipe('cooked_meat',[('raw_meat',1),('fuel',1)],station='furnace',category='Food',seconds=3)
    # Seeds must come from vegetation; remove the old conversion from decorative blocks.
    old=ROOT/'content/recipes/seeds.json'
    if old.exists():old.unlink()
    pack=ROOT/'resource_packs/cubatarium_worldgen_cc0'
    # Original transparent stalks; deterministic pixel texture for each growth stage.
    for stage,height,color in [('sprout',5,(87,133,65,255)),('young',10,(100,149,68,255)),('ripe',15,(208,170,77,255))]:
        id='cu_crop_'+stage;im=Image.new('RGBA',(16,16));draw=ImageDraw.Draw(im)
        for x in [4,8,12]:
            draw.rectangle((x,16-height,x+1,15),fill=color)
            for y in range(17-height,14,3):draw.line((x-2,y,x+2,y+1),fill=color,width=1)
        (pack/'textures'/'blocks').mkdir(parents=True,exist_ok=True);im.save(pack/'textures'/'blocks'/f'{id}.png')
        write(pack/'blocks'/f'{id}.json',dict(name=id,displayName='Grain '+stage,hardness=.05,dig=dict(level=0,groups={'oddly_breakable_by_hand':3}),types=['natural','plants'],textures=[id]*6,render=dict(transparent=True,style='cross',double_sided=True,shape='tuft'),physics=dict(movement=dict(occupancy=0)),license='CC0-1.0',provenance=dict(author='Cubatarium',source_kind='Original procedural crop texture')))
    from itemgen.manifest import update_manifest
    manifest=update_manifest(ROOT)
    print(f"Authored {len(manifest['items'])} items and survival recipes")
if __name__=='__main__':main()
