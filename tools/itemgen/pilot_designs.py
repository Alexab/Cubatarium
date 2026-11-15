"""Original authored pilot construction templates, never imported meshes.

Used only to seed a missing design source. Existing design.yaml and paint layers
remain authoritative and are never overwritten by this utility.
"""
from pathlib import Path
import yaml


def box(name,material,size,offset=(0,0,0),rotation=(0,0,0)):
    return dict(name=name,material=material,shape='bevel_box',size=size,
                bevel=min(size[0],size[1])*.18,offset=list(offset),rotation=list(rotation))


def profile(name,material,points,depth,offset=(0,0,0)):
    # A monotone convex hull keeps every exported face well defined. Curved
    # silhouettes are deliberately assembled from separate convex members.
    points=sorted(set(tuple(p) for p in points))
    def cross(o,a,b): return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    low=[]; high=[]
    for p in points:
        while len(low)>=2 and cross(low[-2],low[-1],p)<=0: low.pop()
        low.append(p)
    for p in reversed(points):
        while len(high)>=2 and cross(high[-2],high[-1],p)<=0: high.pop()
        high.append(p)
    return dict(name=name,material=material,shape='extrude',contour=[list(p) for p in low[:-1]+high[:-1]],depth=depth,offset=list(offset))


def turned(name,material,rings,sides=8,offset=(0,0,0)):
    return dict(name=name,material=material,shape='revolve',profile=[list(r) for r in rings],sides=sides,offset=list(offset))


def materials(metal='iron'):
    colors={'iron':[142,158,165],'stone':[104,107,99],
            'copper':[184,102,60],'gold':[224,177,72]}
    return dict(wood=dict(color=[109,69,37],pattern='wood'),
                binding=dict(color=[82,53,35],pattern='leather',seam=True),
                head=dict(color=colors[metal],pattern='stone' if metal=='stone' else 'hammered' if metal=='copper' else 'forged',metallic=0 if metal=='stone' else .7,roughness=.8 if metal=='stone' else .6),
                edge=dict(color=[min(255,c+30) for c in colors[metal]],pattern='plain',metallic=0 if metal=='stone' else .8,roughness=.45),
                dark=dict(color=[63,74,80],pattern='plain',metallic=.5,roughness=.7))


def design(item_id,parts,mats,grip=(0,-.24,0),atlas=256,density=240,scale=.7,budget=1200):
    return dict(version=3,id=item_id,atlas_size=atlas,texels_per_metre=density,triangle_budget=budget,
                presentation=dict(tp_euler_deg=[-65,0,0],right_grip=list(grip),left_grip=list(grip),fp_euler_deg=[-26,0,-20],fp_scale=scale,fp_hand_offset=[-.04,.12,0]),
                materials=mats,parts=parts)


def make_pilots():
    result={}
    for material in ('stone','iron'):
        parts=[box('shaft','wood',[.05,.65,.05],(0,-.025,0)),
               box('eye','binding' if material=='stone' else 'dark',[.09,.11,.09],(0,.25,0))]
        if material=='stone':
            parts += [profile('knapped_bridge','head',[(-.24,.25),(-.19,.35),(.18,.36),(.24,.29),(.16,.24)],.085),
                      profile('chipped_pick','head',[(.12,.31),(.24,.30),(.33,.13),(.27,.16)],.07),
                      profile('adze','edge',[(-.27,.15),(-.31,.23),(-.16,.34),(-.12,.26)],.075)]
        else:
            parts += [box('forged_bridge','head',[.34,.08,.065],(0,.30,0)),
                      profile('tapered_pick','head',[(.12,.33),(.20,.30),(.34,.12),(.31,.11),(.18,.24)],.05),
                      profile('chisel','edge',[(-.28,.18),(-.30,.25),(-.16,.34),(-.10,.28)],.055),
                      box('wedge','edge',[.065,.025,.085],(0,.35,0))]
        for i,y in enumerate((-.18,-.23,-.28)):
            parts.append(box(f'grip_wrap{i}','binding',[.059,.025,.059],(0,y,0)))
        result[f'{material}_pickaxe']=design(f'{material}_pickaxe',parts,materials(material),density=200 if material=='stone' else 240)
    parts=[box('haft','wood',[.055,.71,.055],(0,-.02,0),(0,0,-5)),
           profile('broad_copper_cheek','head',[(.02,.17),(.13,.12),(.30,.22),(.29,.43),(.17,.47),(.03,.38)],.075),
           profile('polished_edge','edge',[(.26,.19),(.31,.21),(.31,.42),(.26,.46)],.03,(0,0,.038)),
           box('socket','dark',[.085,.13,.09],(.015,.32,0)),
           box('eye_wedge','edge',[.045,.03,.055],(.015,.395,0))]
    for i,y in enumerate((-.17,-.22,-.27)):
        parts.append(box(f'wrap{i}','binding',[.07,.026,.07],(.02,y,0)))
    for i,y in enumerate((.25,.36)):
        parts.append(turned(f'rivet{i}','edge',((-.01,.012),(.01,.012)),6,(.10,y,.05)))
    result['copper_axe']=design('copper_axe',parts,materials('copper'))
    parts=[box('grip','binding',[.055,.23,.05],(0,-.20,0)),
           turned('pommel','head',((-.33,.03),(-.31,.045),(-.28,.03)),6),
           profile('blade','head',[(-.065,-.07),(.065,-.07),(.075,.30),(.008,.50),(-.06,.38)],.045),
           profile('fuller','dark',[(-.012,.01),(.012,.01),(.017,.29),(0,.37),(-.017,.29)],.005,(0,0,.025)),
           profile('guard_left','head',[(-.19,-.11),(-.16,-.055),(-.01,-.075),(-.02,-.13)],.07),
           profile('guard_right','head',[(.02,-.13),(.01,-.075),(.16,-.055),(.19,-.11)],.07)]
    for i,y in enumerate((-.14,-.19,-.24)):
        parts.append(box(f'grip_band{i}','wood',[.06,.014,.055],(0,y,0)))
    result['gold_sword']=design('gold_sword',parts,materials('gold'),scale=.68)
    food=dict(skin=dict(color=[173,46,35],pattern='food'),stem=dict(color=[89,59,31],pattern='wood'),leaf=dict(color=[63,112,47],pattern='plain'))
    parts=[turned('fruit','skin',((-.11,.035),(-.08,.085),(0,.115),(.075,.10),(.11,.035))),
           turned('stem','stem',((.10,.012),(.17,.009)),5),
           profile('leaf','leaf',[(0,0),(.08,-.006),(.105,.025),(.04,.045)],.006,(.005,.13,0))]
    result['apple']=design('apple',parts,food,grip=(0,-.06,0),density=330,scale=.45,budget=600)
    bread=dict(crust=dict(color=[174,113,49],pattern='food'),score=dict(color=[230,190,122],pattern='plain'))
    parts=[turned('loaf','crust',((-.06,.10),(0,.15),(.06,.12),(.09,.045)))]
    for i,x in enumerate((-.06,0,.06)):
        parts.append(box(f'score{i}','score',[.022,.014,.11],(x,.067,0),(0,0,-12 if i==0 else 12)))
    result['bread']=design('bread',parts,bread,grip=(0,-.02,0),density=370,scale=.45,budget=600)
    ore=dict(rock=dict(color=[75,76,72],pattern='stone'),ore=dict(color=[152,102,68],pattern='stone'),metal=dict(color=[169,170,157],pattern='plain',metallic=.25))
    parts=[turned('matrix','rock',((-.09,.065),(0,.13),(.09,.06)),5),
           turned('lobe','ore',((-.04,.04),(.03,.07),(.08,.025)),5,(.07,0,.02))]
    for i,(x,y,z) in enumerate(((-.04,.03,.10),(.05,.06,.07),(-.08,-.02,.07))):
        parts.append(profile(f'vein{i}','metal',[(-.02,-.01),(.02,-.015),(.026,.012),(-.01,.025)],.008,(x,y,z)))
    result['raw_iron']=design('raw_iron',parts,ore,grip=(0,-.03,0),atlas=128,density=100,scale=.45,budget=600)
    parts=[box('grip','binding',[.055,.18,.045],(0,0,0)),
           profile('upper_limb','wood',[(-.015,.07),(.025,.07),(.12,.28),(.06,.43),(.025,.45),(.065,.28)],.04),
           profile('lower_limb','wood',[(-.015,-.07),(.025,-.07),(.12,-.28),(.06,-.43),(.025,-.45),(.065,-.28)],.04),
           box('string','edge',[.007,.86,.007],(.03,0,0))]
    # The curved plane follows YZ, matching the existing physical profile.
    for p in parts:
        p['rotation']=[0,90,0]
        x,y,z=p['offset']; p['offset']=[z,y,-x]
    wood=materials('iron'); wood['edge']=dict(color=[193,177,136],pattern='fiber')
    result['wood_bow']=design('wood_bow',parts,wood,grip=(0,0,0),density=230,scale=.65,budget=1800)
    result['wood_bow']['presentation']['icon_euler_deg']=[0,90,0]
    bow=result['wood_bow']
    bow['presentation'].update(support_grip_node='pull',fp_aim_euler_deg=[0,0,-8],tp_aim_euler_deg=[0,0,0],fp_euler_deg=[-26,28,-12],fp_scale=.9,fp_hand_offset=[-.12,.45,-.12])
    bow['parts']=[p for p in bow['parts'] if p['name']!='string']
    for p in bow['parts']:
        if p['name'] in ('upper_limb','lower_limb'):p['node']=p['name']
    for name,y in (('string_upper',.225),('string_lower',-.225)):
        p=box(name,'edge',[.007,.45,.007],(0,y,-.025));p.update(node=name,fit_reference=False);bow['parts'].append(p)
    bow['nodes']={name:dict(pivot=pivot) for name,pivot in (
        ('upper_limb',[0,.07,0]),('lower_limb',[0,-.07,0]),('pull',[0,0,-.025]),
        ('string_upper',[0,.225,-.025]),('string_lower',[0,-.225,-.025]))}
    bow['nodes']['nocked_arrow']=dict(parent='pull',pivot=[0,0,-.025])
    bow['visual_bindings']=[dict(node='upper_limb',channel='bow_charge',rotation_to=[12,0,0]),
        dict(node='lower_limb',channel='bow_charge',rotation_to=[-12,0,0]),
        dict(node='pull',channel='bow_charge',translation_to=[0,0,.30]),
        dict(node='string_upper',span=dict(start_node='upper_limb',start_point=[0,.38,-.025],end_node='pull',end_point=[0,0,0])),
        dict(node='string_lower',span=dict(start_node='pull',start_point=[0,0,0],end_node='lower_limb',end_point=[0,-.38,-.025])),
        dict(node='nocked_arrow',channel='bow_loaded',visible_above=.001)]
    bow['components']=[dict(name='loaded_arrow',item='arrow',parent='nocked_arrow',source_anchor='nock',rotation_deg=[-90,0,0])]
    steel=materials('iron')
    parts=[profile('blade_left','head',[(-.065,.005),(-.015,-.005),(.03,.20),(0,.23)],.02),
           profile('blade_right','edge',[(.065,.005),(.015,-.005),(-.03,.20),(0,.23)],.02,(0,0,-.025)),
           box('hinge','dark',[.045,.045,.04],(0,.008,0))]
    for i,x in enumerate((-.06,.06)):
        parts.append(dict(name=f'finger_loop{i}',material='dark',shape='ring',outer_radius=.05,inner_radius=.031,depth=.02,offset=[x,-.12,0]))
        parts.append(box(f'tang{i}','head',[.022,.10,.02],(x*.55,-.045,0),(0,0,-25 if x<0 else 25)))
    result['shears']=design('shears',parts,steel,grip=(.04,-.12,0),density=230,scale=.65,budget=1800)
    steel=materials('iron')
    parts=[dict(name='cap',material='head',shape='shell',size=[.34,.17,.31],thickness=.022,rotation=[180,0,0],offset=[0,.12,0]),
           box('crown_ridge','dark',[.055,.04,.30],(0,.145,0)),
           box('brow','edge',[.35,.038,.035],(0,.002,.16))]
    for i,x in enumerate((-.16,.16)):
        parts.append(box(f'cheek{i}','head',[.035,.10,.10],(x,-.03,.10)))
        parts.append(box(f'cheek_rivet{i}','edge',[.018,.018,.02],(x,-.015,.16)))
    result['iron_head']=design('iron_head',parts,steel,grip=(0,-.05,0),atlas=512,density=420,scale=.5,budget=2000)
    leather=dict(leather=dict(color=[128,78,41],pattern='leather',seam=True),strap=dict(color=[66,47,32],pattern='leather',seam=True),buckle=dict(color=[160,117,66],pattern='hammered',metallic=.3))
    outline=[(-.21,.17),(-.13,.23),(.13,.23),(.21,.17),(.16,-.20),(-.16,-.20)]
    parts=[profile('front_panel','leather',outline,.035,(0,0,.12)),
           profile('back_panel','leather',outline,.035,(0,0,-.12)),
           box('waist_belt','strap',[.37,.045,.025],(0,-.13,.15)),
           box('front_seam','strap',[.018,.36,.012],(0,0,.144))]
    for i,x in enumerate((-.15,.15)):
        parts.append(box(f'shoulder_strap{i}','strap',[.055,.045,.27],(x,.20,0)))
    for i,(size,off) in enumerate((([.065,.012,.015],(.08,-.165,.17)),([.065,.012,.015],(.08,-.105,.17)),([.012,.065,.015],(.048,-.135,.17)),([.012,.065,.015],(.112,-.135,.17)))):
        parts.append(box(f'buckle_frame{i}','buckle',size,off))
    result['leather_chest']=design('leather_chest',parts,leather,grip=(0,-.12,0),atlas=512,density=400,scale=.55,budget=2000)
    refine_pilot_surfaces(result)
    return result


def refine_pilot_surfaces(designs):
    """Construction refinements for the authored reference group; no imported data."""
    if 'apple' in designs:
        fruit=designs['apple']
        fruit['materials']['skin']['pattern']='fruit'
        fruit['materials']['leaf'].update(pattern='leaf',uv_direction=[1,0,0],grain_axis='u')
        for part in fruit['parts']:
            if part['name']=='fruit':part['radial_scales']=[1.04,.95,1.07,1.02,.94,1.06,.98,1.0]
            if part['name']=='stem':part['profile']=[list(part['profile'][0]),[.145,.009]]
            if part['name']=='leaf':part['scale']=[.78,.85,1]
    if 'iron_head' in designs:
        helmet=designs['iron_head']
        helmet['materials']['head'].update(marks='forge',uv_direction=[1,0,0])
        for part in helmet['parts']:
            if part['name']=='cap':part.update(shape='ridged_shell',ridge_rise=.035,front_rim_drop=.07)
    pick=designs['iron_pickaxe']
    pick['materials']['head']['marks']='forge'
    pick['materials']['wood'].update(uv_direction=[0,1,0],grain_axis='u')
    pick['parts']=[p for p in pick['parts'] if not p['name'].startswith('socket_pin_')]
    for part in pick['parts']:
        if part['name'] in ('tapered_pick','chisel'):
            part.update(shape='blade',material='head',edge_depth=.008,bevel_fraction=.24,edge_material='edge')
        if part['name']=='eye':part['size'][2]=.084
    for label,z in (('front',.043),('back',-.043)):
        pin=turned('socket_pin_'+label,'edge',((-.002,.013),(.002,.013)),6,(0,.25,z))
        pin['rotation']=[90,0,0];pick['parts'].append(pin)
    axe=designs['copper_axe']
    axe['materials']['head']['marks']='rivet'
    axe['materials']['wood'].update(uv_direction=[0,1,0],grain_axis='u')
    for part in axe['parts']:
        if part['name'] in ('broad_copper_cheek','polished_edge'):
            part.update(shape='blade',edge_depth=part['depth']*.16,bevel_fraction=.18,edge_material=part['material'])
    sword=designs['gold_sword']
    sword['materials']['head']['marks']='inlay'
    sword['materials']['recess']=dict(color=[151,111,46],pattern='forged',metallic=.6,roughness=.72)
    sword['parts']=[p for p in sword['parts'] if p['name']!='fuller_back']
    for part in sword['parts']:
        if part['name']=='blade':part.update(shape='blade',edge_depth=.006,bevel_fraction=.22,edge_material='edge')
        if part['name']=='fuller':
            # A narrow warm-metal relief reads as a groove, not an open hole.
            if part['material']!='recess':part['contour']=[[x*.45,y] for x,y in part['contour']]
            part.update(material='recess',depth=.001,offset=[0,0,.0228])
    import copy
    back=copy.deepcopy(next(p for p in sword['parts'] if p['name']=='fuller'))
    back['name']='fuller_back';back['offset'][2]=-.0228;sword['parts'].append(back)
    loaf=designs['bread']
    loaf['materials']['score_shadow']=dict(color=[130,75,35],pattern='food')
    loaf['parts']=[p for p in loaf['parts'] if p['name']=='loaf']
    loaf['parts'][0].update(profile=[[-.06,.09],[-.02,.14],[.045,.135],[.09,.095]],scale=[1.25,1,.8])
    for index,x in enumerate((-.065,0,.065)):
        for suffix,material,width,y,depth in (('rim','score_shadow',.018,.0915,.003),('crumb','score',.010,.093,.002)):
            part=profile(f'score{index}_{suffix}',material,[[-width/2,-depth/2],[width/2,-depth/2],[width/2,depth/2],[-width/2,depth/2]],.095,(x,y,0))
            part['rotation']=[0,-22,0];loaf['parts'].append(part)


def seed_missing(root: Path):
    created=[]
    for item_id,data in make_pilots().items():
        target=root/'tools/item_designs'/item_id/'design.yaml'
        if target.exists(): continue
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(yaml.safe_dump(data,sort_keys=False),encoding='utf-8')
        created.append(item_id)
    return created


if __name__=='__main__':
    print(seed_missing(Path(__file__).resolve().parents[2]))
