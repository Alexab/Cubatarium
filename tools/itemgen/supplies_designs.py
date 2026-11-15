"""Authored equipment/supply constructions; seed only missing source recipes.

These coordinate drawings use no meshes, UVs or pixels from external packs.
Gameplay definitions, physical profiles and existing author sources are read
only. Individual recipes and overlays remain the editing authority after seed.
"""
from pathlib import Path
import yaml

from itemgen.pilot_designs import box, profile, turned, design, materials
from itemgen.workshop_designs import cutting


def loop(name,material,outer,inner,depth,offset=(0,0,0),rotation=(0,0,0),sides=8):
    return dict(name=name,material=material,shape='ring',outer_radius=outer,
                inner_radius=inner,depth=depth,sides=sides,offset=list(offset),rotation=list(rotation))


def flattened(part,scale):
    part['scale']=list(scale)
    return part


def resource(id,parts,mats,*,grip=(0,0,0),small=False,scale=.5,density=240,budget=600):
    result=design(id,parts,mats,grip=grip,atlas=128 if small else 256,
                  density=density,scale=scale,budget=budget)
    result['authorship']=dict(construction='Cubatarium supply coordinate drawings',
        reference_kind='functional real-world objects and materials',external_meshes=False,external_pixels=False)
    result['presentation'].update(tp_euler_deg=[-30,0,20],tp_scale=1.5)
    return result


def equipment():
    result={};m=materials('iron')
    m['wood'].update(uv_direction=[0,1,0],grain_axis='u')
    parts=[turned('spear_shaft','wood',((-.39,.014),(-.28,.018),(.23,.015),(.30,.012)),8),
           cutting('leaf_point',[(-.043,.27),(.043,.27),(.031,.36),(0,.48),(-.031,.36)],'iron',.023),
           turned('socket','dark',((.20,.021),(.28,.018)),6),
           turned('butt_cap','dark',((-.41,.019),(-.38,.020)),6)]
    for i,y in enumerate((-.06,-.095,-.13,.225,.25)):
        parts.append(box(f'haft_binding{i}','binding',[.038,.013,.034],(0,y,0),(0,0,9)))
    result['wood_spear']=resource('wood_spear',parts,m,grip=(0,-.08,0),scale=.75,density=145,budget=1200)
    m=dict(shaft=dict(color=[158,112,60],pattern='wood',uv_direction=[0,1,0],grain_axis='u'),
           steel=dict(color=[151,164,167],pattern='forged',metallic=.7,roughness=.5),
           feather=dict(color=[216,204,166],pattern='feather'),wrap=dict(color=[81,57,38],pattern='fiber'))
    parts=[turned('arrow_shaft','shaft',((-.31,.006),(.24,.006)),6),
           dict(name='forged_arrow_point',material='steel',shape='blade',contour=[[-.027,.23],[.027,.23],[0,.34]],depth=.014,edge_depth=.003,bevel_fraction=.2)]
    for i,angle in enumerate((0,120,240)):
        vane=profile(f'feather{i}','feather',[(.005,-.27),(.026,-.25),(.020,-.15),(.005,-.13)],.003)
        vane['rotation']=[0,angle,0];parts.append(vane)
    parts.extend((box('nock_binding','wrap',[.016,.025,.016],(0,-.285,0)),
                  box('nock_left','shaft',[.004,.025,.009],(-.005,-.32,0)),
                  box('nock_right','shaft',[.004,.025,.009],(.005,-.32,0))))
    result['arrow']=resource('arrow',parts,m,grip=(0,-.20,0),scale=.65,density=165,budget=1200)
    result['arrow']['nodes']={'nock':dict(pivot=[0,-.32,0])}
    for metal in (False,True):
        id='iron_shield' if metal else 'wood_shield';m=materials('iron')
        m['paint']=dict(color=[62,94,102] if metal else [137,62,39],pattern='painted')
        m['wood'].update(uv_direction=[0,1,0],grain_axis='u')
        outline=[(-.22,-.04),(-.18,-.23),(0,-.29),(.18,-.23),(.22,-.04),(.20,.23),(-.20,.23)]
        if metal: outline=[(-.23,-.11),(-.15,-.24),(.09,-.26),(.23,-.10),(.23,.14),(.13,.23),(-.13,.23),(-.23,.14)]
        parts=[profile('rim','dark',outline,.055),
               profile('face_panel','head' if metal else 'wood',[(x*.92,y*.93) for x,y in outline],.035,(0,0,.03)),
               box('front_spine','head' if metal else 'paint',[.046,.46,.018],(0,-.005,.059))]
        if not metal:
            m['wood']['marks']='planks'
            parts[1].update(depth=.060,offset=[0,0,0])
            parts[2]['offset']=[0,-.005,.040]
        else:
            parts[1].update(shape='blade',edge_depth=.008,bevel_fraction=.19,edge_material='head')
            boss=turned('hammered_boss','head',((-.01,.045),(.015,.061),(.035,.035)),6,(0,0,.055));boss['rotation']=[90,0,0];parts.append(boss)
        for i,(x,y) in enumerate(((-.17,.16),(.17,.16),(-.13,-.16),(.13,-.16))):
            rivet=turned(f'rim_rivet{i}','head' if metal else 'dark',((-.004,.012),(.004,.012)),6,(x,y,.060 if metal else .034));rivet['rotation']=[90,0,0];parts.append(rivet)
        # The reverse side is modeled independently: an open hand loop and
        # separate arm strap remain visible in orbit/TP, not painted on.
        parts.extend((loop('back_grip','binding',.062,.044,.025,(0,0,-.065)),
                      box('reverse_arm_strap','binding',[.24,.04,.018],(0,-.12,-.045)),
                      box('left_grip_mount','dark',[.03,.05,.035],(-.06,0,-.04)),
                      box('right_grip_mount','dark',[.03,.05,.035],(.06,0,-.04))))
        result[id]=resource(id,parts,m,grip=(0,-.045,-.065),scale=.55,density=120 if metal else 140,budget=1200)
        result[id]['presentation'].update(fp_euler_deg=[-26,180,-20],tp_euler_deg=[-65,180,0],fp_hand_offset=[-.10,.24,0])
    m=dict(wood=dict(color=[102,66,34],pattern='wood',uv_direction=[0,1,0],grain_axis='u'),
           binding=dict(color=[74,49,29],pattern='fiber'),char=dict(color=[45,35,26],pattern='char'),
           flame=dict(color=[227,112,28],pattern='flame',emissive=[.55,.12,.005]),core=dict(color=[252,196,70],pattern='plain',emissive=[.8,.4,.06]))
    parts=[turned('torch_staff','wood',((-.30,.018),(.15,.014)),6),
           turned('resin_bundle','char',((.11,.021),(.18,.035),(.25,.029)),7),
           turned('flame','flame',((.23,.021),(.27,.030),(.32,.017),(.36,.002)),5),
           turned('hot_core','core',((.23,.010),(.27,.016),(.30,.002)),5,(0,0,.018))]
    for i,y in enumerate((.13,.17,.21)):
        parts.append(turned(f'binding{i}','binding',((y-.008,.035),(y+.008,.035)),7))
    result['wood_torch']=resource('wood_torch',parts,m,grip=(0,-.19,0),scale=.6,density=210,budget=1200)
    m=dict(steel=dict(color=[112,128,134],pattern='forged',metallic=.7,roughness=.45),
           flint=dict(color=[74,77,82],pattern='stone'),edge=dict(color=[141,147,151],pattern='stone'),
           cord=dict(color=[126,89,49],pattern='fiber'))
    parts=[loop('forged_striker','steel',.050,.034,.014,(.026,.022,0),sides=8),
           profile('knapped_flint','flint',[(-.044,-.058),(.006,-.052),(.020,-.017),(-.006,.010),(-.048,-.008)],.024,(-.01,0,.008)),
           profile('fresh_flint_facet','edge',[(-.034,-.047),(-.005,-.038),(-.016,-.012),(-.038,-.020)],.003,(-.01,0,.022)),
           box('link_cord','cord',[.012,.055,.012],(-.008,-.002,-.010),(0,0,-25))]
    for p in parts:
        if 'flint' in p['name']:p['node']='flint'
    result['flint_and_steel']=resource('flint_and_steel',parts,m,grip=(.026,.045,0),scale=.55,density=450,budget=1800)
    result['flint_and_steel']['presentation']['fp_hand_offset']=[-.04,.65,0]
    result['flint_and_steel'].update(nodes=dict(flint=dict(pivot=[-.02,-.02,.008])),visual_bindings=[dict(node='flint',channel='use_phase',curve='pulse',rotation_to=[0,0,-24])])
    m=dict(leather=dict(color=[128,77,39],pattern='leather'),edge=dict(color=[73,47,30],pattern='leather',seam=True),
           cork=dict(color=[166,123,63],pattern='wood'),tie=dict(color=[166,137,88],pattern='fiber'))
    parts=[flattened(turned('sewn_pouch','leather',((-.14,.020),(-.11,.065),(-.03,.078),(.07,.055),(.11,.024)),8),(1,1,.68)),
           turned('neck','edge',((.08,.018),(.14,.017)),6),
           turned('stopper','cork',((.135,.019),(.16,.020)),6)]
    for side in (-1,1):
        prefix='left_edge_seam' if side<0 else 'right_edge_seam'
        for j,(a,b) in enumerate((((.065,-.11),(.078,-.03)),((.078,-.03),(.055,.07)))):
            points=[(side*(a[0]+delta),a[1]) for delta in (-.004,.004)]+[(side*(b[0]+delta),b[1]) for delta in (-.004,.004)]
            parts.append(profile(prefix+str(j),'edge',points,.010))
    parts.extend((loop('stopper_tie','tie',.026,.021,.005,(0,.12,0),(90,0,0)),
                  box('tether','tie',[.006,.08,.006],(.023,.08,0),(0,0,-12))))
    for p in parts:
        if p['name']=='sewn_pouch' or 'edge_seam' in p['name']:p['node']='pouch'
    result['waterskin']=resource('waterskin',parts,m,grip=(0,.10,0),scale=.48,density=290,budget=1800)
    result['waterskin'].update(nodes=dict(pouch=dict(pivot=[0,.09,0])),visual_bindings=[dict(node='pouch',channel='fill_units',range=[0,4],scale_from=[.92,1,.62],scale_to=[1,1,1])])
    result['waterskin']['presentation'].update(fp_euler_deg=[76,0,-20],fp_hand_offset=[-.04,.65,0],tp_scale=2)
    return result


def supplies():
    result={}
    for metal,color in (('copper',[178,98,60]),('iron',[145,156,156]),('gold',[215,170,65])):
        mats=dict(cast=dict(color=color,pattern='cast',metallic=.7,roughness=.55,marks='inlay' if metal=='gold' else 'forge'))
        slab=box('cast_bar','cast',[.30,.15,.10]);slab.update(rotation=[90,0,0],taper=.87)
        result[f'{metal}_ingot']=resource(f'{metal}_ingot',[slab],mats,small=True,scale=.45,density=150)
    for metal in ('copper','gold'):
        mats=dict(matrix=dict(color=[64,77,72] if metal=='copper' else [153,145,120],pattern='stone'),
                  exposed=dict(color=[171,98,57] if metal=='copper' else [214,177,65],pattern='ore',metallic=.45,roughness=.65),
                  oxide=dict(color=[72,115,98] if metal=='copper' else [211,206,174],pattern='stone'))
        parts=[turned('matrix','matrix',((-.09,.05),(-.03,.11),(.05,.10),(.09,.04)),5),
               turned('ore_lobe','exposed',((-.04,.035),(.015,.060),(.065,.025)),5,(.065,.005,.015)),
               turned('oxide_lobe','oxide',((-.04,.022),(0,.045),(.045,.018)),5,(-.06,-.025,.045))]
        if metal=='gold':parts[0]['rotation']=[0,0,24];parts[1]['offset']=[.025,.025,.070]
        result[f'raw_{metal}']=resource(f'raw_{metal}',parts,mats,small=True,scale=.45,density=110)
    coal=dict(mineral=dict(color=[44,45,51],pattern='coal',roughness=.42),cleft=dict(color=[63,62,72],pattern='coal',roughness=.38))
    parts=[flattened(turned('coal_mass','mineral',((-.075,.045),(-.025,.10),(.04,.085),(.075,.025)),5),(1,1,.75)),
           turned('cleaved_lobe','cleft',((-.04,.028),(0,.055),(.045,.018)),4,(.06,0,.015))]
    result['coal']=resource('coal',parts,coal,small=True,scale=.45,density=125)
    char=dict(charred=dict(color=[46,39,32],pattern='char',uv_direction=[0,1,0],grain_axis='u'),end=dict(color=[70,58,42],pattern='char'))
    parts=[turned('charred_branch','charred',((-.13,.035),(-.09,.065),(.10,.055),(.14,.025)),7),
           turned('exposed_end','end',((.115,.036),(.13,.032)),7)]
    result['charcoal']=resource('charcoal',parts,char,small=True,scale=.5,density=110)
    rope=dict(fiber=dict(color=[159,130,79],pattern='fiber'),tie=dict(color=[105,79,43],pattern='fiber'))
    parts=[loop(f'coil{i}','fiber',.085,.067,.014,(0,i*.017,0),(90,0,0),sides=6) for i in range(3)]
    parts.extend((box('loose_tail','fiber',[.085,.012,.012],(.085,.011,.025),(0,0,-12)),
                  box('coil_tie','tie',[.015,.065,.025],(.067,.02,0))))
    result['cord']=resource('cord',parts,rope,small=True,scale=.45,density=75)
    wood=dict(bark=dict(color=[107,73,40],pattern='wood',uv_direction=[0,1,0],grain_axis='u'),cut=dict(color=[169,132,78],pattern='wood'))
    parts=[turned('branch','bark',((-.26,.018),(-.08,.025),(.18,.017),(.25,.012)),6),
           turned('cut_end','cut',((.246,.011),(.255,.010)),6),
           turned('branch_knot','bark',((-.015,.011),(.035,.006)),5,(.025,.06,0))]
    parts[2]['rotation']=[0,0,-55]
    result['stick']=resource('stick',parts,wood,small=True,scale=.6,density=100)
    hide=dict(hide=dict(color=[145,97,55],pattern='leather'),edge=dict(color=[88,56,32],pattern='leather',seam=True))
    parts=[profile('cut_hide','hide',[(-.12,-.08),(.08,-.095),(.13,-.015),(.10,.08),(-.07,.095),(-.13,.015)],.012),
           profile('folded_corner','edge',[(.06,-.08),(.13,-.015),(.10,.04),(.07,.005)],.015,(0,0,.011))]
    for p in parts:p['rotation']=[90,0,0]
    result['leather']=resource('leather',parts,hide,small=True,scale=.45,density=100)
    wool=dict(fleece=dict(color=[216,207,180],pattern='wool'),warm=dict(color=[197,183,150],pattern='wool'),binding=dict(color=[122,91,51],pattern='fiber'))
    parts=[turned('fleece_bundle','fleece',((-.09,.065),(-.04,.13),(.05,.12),(.10,.06)),7),
           turned('side_lock','warm',((-.04,.035),(0,.075),(.06,.033)),6,(.095,-.025,.025)),
           box('tie','binding',[.014,.23,.20],(0,0,0))]
    result['wool']=resource('wool',parts,wool,small=True,scale=.45,density=60)
    fiber=dict(fiber=dict(color=[191,172,115],pattern='fiber'),tie=dict(color=[125,94,48],pattern='fiber'))
    parts=[]
    for i,x in enumerate((-.045,-.025,-.005,.016,.037)):
        stalk=turned(f'stripped_fiber{i}','fiber',((-.14,.006),(.09,.007),(.145,.002)),4,(x,0,(i%2)*.012));stalk['rotation']=[0,0,(i-2)*-6];parts.append(stalk)
    parts.append(box('fiber_tie','tie',[.12,.025,.029],(0,-.035,.006)))
    result['plant_fiber']=resource('plant_fiber',parts,fiber,small=True,scale=.5,density=105)
    for id in ('seeds','grain'):
        mats=dict(kernel=dict(color=[117,75,36] if id=='seeds' else [201,158,70],pattern='grain'),germ=dict(color=[169,129,67] if id=='seeds' else [235,204,131],pattern='plain'))
        parts=[]
        points=[(-.075,0,-.035),(-.025,0,.028),(.03,0,-.025),(.075,0,.035),(-.065,0,.045),(.012,0,.05)] if id=='seeds' else [(-.04,-.05,0),(0,-.04,.004),(.04,-.035,0),(-.025,.002,.004),(.025,.015,0),(0,.054,.003)]
        for i,point in enumerate(points):
            kernel=turned(f'kernel{i}','kernel',((-.017,.003),(0,.012),(.022,.004)),5,point)
            kernel.update(scale=[1,1,.55],rotation=[90,0,i*27] if id=='seeds' else [0,0,(i%3-1)*15]);parts.append(kernel)
        result[id]=resource(id,parts,mats,small=True,scale=.45,density=155)
    meat=dict(flesh=dict(color=[158,57,53],pattern='meat'),fat=dict(color=[220,186,159],pattern='food'),bone=dict(color=[223,212,170],pattern='plain'))
    steak=profile('fresh_cut','flesh',[(-.12,-.025),(-.07,-.075),(.065,-.065),(.125,.015),(.05,.08),(-.085,.065)],.055);steak['rotation']=[90,0,0]
    parts=[steak,turned('marrow','bone',((.027,.022),(.032,.020)),6,(-.025,0,.012)),
           box('fat_ribbon','fat',[.095,.008,.015],(.028,.030,-.040),(0,-25,0))]
    result['raw_meat']=resource('raw_meat',parts,meat,scale=.48,density=230)
    cooked=dict(crust=dict(color=[113,65,31],pattern='roast'),fat=dict(color=[180,133,68],pattern='food'))
    roast=flattened(turned('roasted_cut','crust',((-.055,.025),(-.035,.105),(.025,.12),(.060,.035)),7),(1,1,.65))
    parts=[roast,box('rendered_fat','fat',[.10,.008,.018],(.012,.040,.025),(0,-22,0))]
    result['cooked_meat']=resource('cooked_meat',parts,cooked,scale=.48,density=230)
    return result


def make_supplies():
    return {**equipment(),**supplies()}


def seed_missing(root: Path):
    created=[]
    for id,data in make_supplies().items():
        target=root/'tools/item_designs'/id/'design.yaml'
        if target.exists():continue
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(yaml.safe_dump(data,sort_keys=False),encoding='utf-8');created.append(id)
    return created
