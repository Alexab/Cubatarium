"""Cubatarium armor construction drawings, never overwriting authored sources."""
from pathlib import Path
import yaml

from itemgen.pilot_designs import box,profile,turned,design

TIERS=('leather','copper','iron','gold')
SLOTS=('head','chest','legs','feet')


def palette(tier):
    colors={'leather':[126,79,43],'copper':[179,103,65],'iron':[135,152,157],'gold':[212,171,72]}
    c=colors[tier]
    return dict(body=dict(color=c,pattern='leather' if tier=='leather' else 'hammered' if tier=='copper' else 'forged',metallic=0 if tier=='leather' else .65,roughness=.85 if tier=='leather' else .55),
                trim=dict(color=[min(255,v+27) for v in c],pattern='leather' if tier=='leather' else 'plain',metallic=0 if tier=='leather' else .7,roughness=.7),
                lining=dict(color=[66,49,35],pattern='leather'),
                strap=dict(color=[87,57,34],pattern='leather',seam=True),
                fastener=dict(color=[153,118,65] if tier=='leather' else [min(255,v+40) for v in c],pattern='hammered',metallic=.6,roughness=.6))


def sleeve(name,material,size,offset,wall=.012):
    return dict(name=name,material=material,shape='tube',size=list(size),thickness=wall,offset=list(offset))


def section(node,bones,slot,low,high,region='slot'):
    return dict(node=node,bones=bones,slot=slot,region=region,low=list(low),high=list(high),euler_deg=[0,180,0])


def bind(parts,node):
    for p in parts:p['node']=node
    return parts


def buckle(name,position,width=.045):
    x,y,z=position
    return [box(name+str(i),'fastener',size,(x+dx,y+dy,z)) for i,(size,dx,dy) in enumerate((([width,.009,.009],0,-width/2),([width,.009,.009],0,width/2),([.009,width,.009],-width/2,0),([.009,width,.009],width/2,0)))]


def helmet(tier):
    parts=[]
    if tier=='leather':
        parts=[box('sewn_crown','body',[.32,.025,.30],(0,.12,0)),
               box('rear_panel','body',[.32,.13,.022],(0,.055,-.14))]
        for side in (-1,1):
            parts.append(profile('left_ear_flap' if side<0 else 'right_ear_flap','body',[(side*.13,-.09),(side*.16,-.06),(side*.16,.10),(side*.13,.12)],.20,(0,0,-.015)))
        parts.extend((box('crown_cross_seam','strap',[.022,.018,.30],(0,.137,0)),box('forehead_binding','strap',[.32,.032,.024],(0,.045,.145))))
    else:
        parts=[dict(name='cap_shell',material='body',shape='shell',size=[.33,.17,.30],thickness=.017,rotation=[180,0,0],offset=[0,.12,0]),
               box('brow','trim',[.34,.032,.026],(0,-.004,.15))]
        if tier=='copper':
            for i,x in enumerate((-.09,0,.09)):
                parts.append(box(f'crown_segment{i}','trim',[.08,.025,.30],(x,.13,0),(0,0,-8 if x<0 else 8)))
            for i,x in enumerate((-.12,-.06,0,.06,.12)):
                parts.append(box(f'brow_rivet{i}','fastener',[.014,.014,.012],(x,-.005,.17)))
        elif tier=='iron':
            parts.append(box('forged_crown_ridge','lining',[.045,.036,.30],(0,.14,0)))
            for side in (-1,1):
                parts.append(box('left_cheek' if side<0 else 'right_cheek','body',[.025,.11,.10],(side*.16,-.06,.08),(0,0,side*5)))
        else:
            parts.extend((box('thin_crown_band','trim',[.025,.015,.30],(0,.128,0)),
                          profile('forehead_inlay','lining',[(-.035,-.01),(0,.03),(.035,-.01),(0,-.035)],.004,(0,.025,.166))))
    bind(parts,'helmet')
    return parts,{'helmet':dict(pivot=[0,0,0])},[section('helmet',['hat'],0,[-.145,-.05,-.13],[.145,.12,.13])]


def cuirass(tier):
    outline=[(-.19,-.22),(.19,-.22),(.22,.15),(.13,.23),(-.13,.23),(-.22,.15)]
    parts=[profile('front_panel','body',outline,.022,(0,0,.12)),profile('rear_panel','body',outline,.022,(0,0,-.12))]
    for side in (-1,1):
        parts.extend((box('left_shoulder_strap' if side<0 else 'right_shoulder_strap','strap',[.055,.03,.26],(side*.145,.21,0)),
                      box('left_side_lacing' if side<0 else 'right_side_lacing','strap',[.018,.23,.21],(side*.185,-.04,0))))
    if tier=='leather':
        parts.extend((box('central_join','strap',[.018,.35,.009],(0,-.015,.137)),box('waist_belt','strap',[.38,.04,.025],(0,-.15,.14))))
        parts+=buckle('belt_buckle',(.07,-.15,.16))
    elif tier=='copper':
        for i,y in enumerate((-.13,-.025,.08)):
            parts.append(box(f'overlapping_lamella{i}','body',[.37,.085,.015],(0,y,.14)))
            for side in (-1,1):parts.append(box(f'lamella_rivet{i}_{side}','fastener',[.015,.015,.011],(side*.145,y,.154)))
    elif tier=='iron':
        for side in (-1,1):
            p=profile('left_breastplate' if side<0 else 'right_breastplate','body',[(side*.008,-.13),(side*.16,-.17),(side*.19,.11),(side*.04,.17)],.022,(0,0,.14));p.update(shape='blade',edge_depth=.006,bevel_fraction=.13,edge_material='trim');parts.append(p)
        parts.append(box('lower_fauld','lining',[.37,.052,.03],(0,-.20,.13)))
    else:
        parts.append(profile('fine_chest_chevron','trim',[(-.16,.13),(0,.02),(.16,.13),(0,-.06)],.010,(0,0,.143)))
        parts.append(profile('dark_inlay','lining',[(-.025,.13),(0,.17),(.025,.13),(0,.09)],.003,(0,0,.15)))
        parts+=buckle('side_clasp',(.15,-.12,.15),.035)
    bind(parts,'torso')
    return parts,{'torso':dict(pivot=[0,0,0])},[section('torso',['body'],1,[-.19,-.22,-.105],[.19,.22,.105])]


def leggings(tier):
    parts=bind([sleeve('waist_band','strap' if tier=='leather' else 'lining',[.33,.052,.15],(0,.19,0)),*buckle('waist_buckle',(0,.19,.083),.035)],'waist')
    nodes={'waist':dict(pivot=[0,.19,0])}
    sections=[section('waist',['body'],2,[-.15,.165,-.065],[.15,.215,.065],'waist')]
    for side,x in (('left',-.085),('right',.085)):
        for region,y,bone in (('thigh',.085,side+'Leg'),('shin',-.11,side+'Shin')):
            node=side+'_'+region;nodes[node]=dict(pivot=[x,y,0])
            segment=[sleeve(node+'_liner','lining' if tier!='leather' else 'body',[.14,.18,.15],(x,y,0),.010)]
            if tier=='leather':
                segment.extend((box(node+'_front_patch','body',[.112,.13,.012],(x,y,.081)),sleeve(node+'_cuff','strap',[.147,.024,.157],(x,y-.06,0),.006)))
            elif tier=='copper':
                for i,dy in enumerate((-.05,0,.05)):
                    segment.append(box(node+f'_lamella{i}','body',[.12,.052,.014],(x,y+dy,.083)))
                segment.append(box(node+'_rivet','fastener',[.015,.015,.009],(x,y,.095)))
            elif tier=='iron':
                p=profile(node+'_forged_plate','body',[(-.052,-.08),(.052,-.08),(.060,.055),(0,.085),(-.060,.055)],.018,(x,y,.083));p.update(shape='blade',edge_depth=.005,bevel_fraction=.18,edge_material='trim');segment.append(p)
                if region=='shin':segment.append(box(node+'_knee_guard','body',[.125,.045,.026],(x,y+.065,.09)))
            else:
                segment.append(profile(node+'_slender_plate','body',[(-.05,-.075),(.05,-.075),(.048,.065),(0,.085),(-.048,.065)],.012,(x,y,.082)))
                for i,dy in enumerate((-.055,.055)):segment.append(sleeve(node+f'_gold_binding{i}','trim',[.146,.012,.158],(x,y+dy,0),.004))
            parts+=bind(segment,node)
            sections.append(section(node,[bone],2,[x-.06,y-.09,-.065],[x+.06,y+.09,.065],'full'))
    return parts,nodes,sections


def footwear(tier):
    parts=[];nodes={};sections=[]
    for side,x in (('left',-.083),('right',.083)):
        node=side+'_boot';nodes[node]=dict(pivot=[x,0,0])
        boot=[box(node+'_sole','lining',[.145,.025,.21],(x,.0125,.025)),
              box(node+'_toe','body',[.14,.055,.12],(x,.051,.065)),
              sleeve(node+'_ankle','body',[.13,.11,.12],(x,.11,-.02),.012)]
        if tier=='leather':
            for i,y in enumerate((.08,.105,.13)):boot.append(box(node+f'_lace{i}','strap',[.07,.008,.008],(x,y,.045),(0,0,(-1)**i*12)))
        elif tier=='copper':
            boot.append(box(node+'_toe_plate','trim',[.13,.014,.10],(x,.082,.065)))
            for sign in (-1,1):boot.append(box(node+f'_toe_rivet{sign}','fastener',[.014,.011,.014],(x+sign*.045,.094,.09)))
        elif tier=='iron':
            boot.extend((box(node+'_toe_ridge','trim',[.028,.013,.11],(x,.086,.065)),box(node+'_ankle_band','lining',[.137,.024,.127],(x,.13,-.02))))
        else:
            boot.append(box(node+'_fine_toe_band','trim',[.145,.012,.035],(x,.087,.09)))
            boot+=buckle(node+'_clasp',(x,.12,.049),.027)
        parts+=bind(boot,node)
        sections.append(section(node,[side+'Foot'],3,[x-.065,0,-.08],[x+.065,.16,.13],'full'))
    return parts,nodes,sections


def make_armor():
    result={}
    builders=dict(head=helmet,chest=cuirass,legs=leggings,feet=footwear)
    for tier in TIERS:
        for slot in SLOTS:
            id=f'{tier}_{slot}';parts,nodes,sections=builders[slot](tier)
            recipe=design(id,parts,palette(tier),grip=(0,-.05,0),atlas=512,density=180 if id=='gold_legs' else 230,scale=.5,budget=2000)
            if slot=='feet':
                recipe['presentation'].update(right_grip=[0,0,.025],left_grip=[0,0,.025])
            recipe.update(nodes=nodes,wear_sections=sections,authorship=dict(construction='Cubatarium armor coordinate drawings',reference_kind='functional sewn and worked armor',external_meshes=False,external_pixels=False))
            result[id]=recipe
    return result


def seed_missing(root: Path):
    created=[]
    for id,data in make_armor().items():
        target=root/'tools/item_designs'/id/'design.yaml'
        if target.exists():continue
        target.parent.mkdir(parents=True,exist_ok=True);target.write_text(yaml.safe_dump(data,sort_keys=False),encoding='utf-8');created.append(id)
    return created
