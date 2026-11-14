"""Seed missing tool designs from Cubatarium workshop construction drawings.

No mesh, UV, texture or coordinate data is read from external packs. Existing
author recipes are never rewritten. Seeding is distinct from build/publication.
"""
from pathlib import Path
import yaml

from itemgen.pilot_designs import box, profile, turned, design, materials

TIERS=('wood','stone','copper','iron','gold')
FAMILIES=('pickaxe','axe','shovel','hoe','sword')


def tool_materials(tier):
    result=materials(tier if tier!='wood' else 'iron')
    if tier=='wood':
        result['head']=dict(color=[153,109,59],pattern='wood')
        result['edge']=dict(color=[195,150,85],pattern='wood')
        result['dark']=dict(color=[86,55,31],pattern='wood')
    result['head']['marks']='joinery' if tier=='wood' else 'knapped' if tier=='stone' else 'rivet' if tier=='copper' else 'forge' if tier=='iron' else 'inlay'
    result['wood']['uv_direction']=[0,1,0]
    result['wood']['grain_axis']='u'
    if tier=='wood':
        result['head']['uv_direction']=[0,1,0]
        result['head']['grain_axis']='u'
    return result


def cutting(name,points,tier,depth=.045):
    part=profile(name,'head',points,depth)
    part.update(shape='blade',edge_depth=depth*(.32 if tier=='stone' else .16),
                bevel_fraction=.24 if tier=='stone' else .13,edge_material='edge')
    return part


def fasteners(tier,y=.23):
    if tier in ('wood','stone'):
        return [box('head_lashing_a','binding',[.088,.019,.08],(0,y-.022,0),(0,0,18)),
                box('head_lashing_b','binding',[.088,.019,.08],(0,y+.018,0),(0,0,-18)),
                box('wooden_key','wood',[.023,.035,.087],(0,y+.06,0))]
    parts=[box('forged_socket' if tier=='iron' else 'socket','dark',[.077,.12,.075],(0,y,0))]
    if tier=='copper':
        for i,dy in enumerate((-.035,.035)):
            rivet=turned(f'socket_rivet{i}','edge',((-.008,.012),(.008,.012)),6,(.019,y+dy,.044))
            rivet['rotation']=[90,0,0];parts.append(rivet)
    elif tier=='iron':
        parts.append(box('socket_wedge','edge',[.038,.018,.077],(0,y+.07,0),(0,0,6)))
    else:
        parts.extend((box('lower_fine_collar','edge',[.083,.016,.079],(0,y-.055,0)),
                      box('upper_fine_collar','edge',[.083,.016,.079],(0,y+.055,0))))
    return parts


def haft(tier):
    # A faceted, tapered wooden handle remains wooden at every tier.
    sides=6 if tier in ('wood','stone') else 8
    parts=[turned('shaped_haft','wood',((-.39,.025),(-.30,.029),(.13,.023),(.32,.020)),sides)]
    for i,y in enumerate((-.19,-.24,-.29)):
        parts.append(box(f'grip_binding{i}','binding',[.059,.022,.057],(0,y,0),(0,0,-7)))
    if tier=='gold': parts.append(box('grip_end_collar','edge',[.056,.015,.052],(0,-.33,0)))
    return parts


def pickaxe(tier):
    if tier=='wood':
        return [cutting('laminated_bridge',[(-.21,.25),(-.17,.33),(.16,.33),(.21,.25)],tier,.06),
                cutting('hardwood_point',[(.13,.31),(.21,.27),(.30,.12),(.26,.15),(.15,.25)],tier,.045),
                cutting('hardwood_adze',[(-.28,.18),(-.30,.22),(-.17,.32),(-.13,.25)],tier,.055),
                box('cross_grain_spline','dark',[.25,.026,.067],(0,.29,0))]
    if tier=='copper':
        return [cutting('cast_bridge',[(-.18,.28),(-.12,.36),(.12,.36),(.20,.28)],tier,.055),
                cutting('cast_point',[(.09,.33),(.17,.34),(.30,.17),(.28,.13),(.19,.23)],tier,.045),
                cutting('broad_cast_adze',[(-.27,.19),(-.28,.25),(-.14,.35),(-.10,.28)],tier,.05),
                box('reinforcing_web','dark',[.18,.034,.064],(0,.30,0))]
    if tier=='gold':
        return [cutting('slender_bridge',[(-.18,.29),(-.14,.34),(.13,.34),(.18,.29)],tier,.035),
                cutting('needle_pick',[(.10,.33),(.17,.31),(.31,.13),(.29,.12),(.15,.25)],tier,.027),
                cutting('gold_chisel',[(-.27,.22),(-.27,.25),(-.14,.33),(-.10,.29)],tier,.031),
                profile('decorative_spine','dark',[(-.16,.30),(.12,.30),(.15,.32),(-.15,.32)],.041)]
    # Stone/iron pilot recipes already exist; these outlines also support a
    # fresh missing source without copying a foreign model.
    return [cutting('rock_pick' if tier=='stone' else 'forged_pick',
                    [(-.29,.18),(-.20,.34),(.16,.35),(.32,.13),(.27,.16),(.10,.27),(-.16,.25)],tier,.065 if tier=='stone' else .045)]


def axe(tier):
    outlines={
        'wood':[(0,.15),(.18,.17),(.26,.29),(.23,.42),(.04,.35)],
        'stone':[(-.025,.17),(.14,.12),(.27,.22),(.24,.41),(.10,.45),(-.035,.32)],
        'copper':[(0,.15),(.20,.13),(.29,.23),(.28,.40),(.13,.44),(.01,.34)],
        'iron':[(0,.18),(.13,.13),(.27,.20),(.28,.38),(.09,.40),(-.01,.31)],
        'gold':[(0,.20),(.17,.15),(.27,.21),(.24,.43),(.10,.39),(.01,.30)]}
    parts=[cutting('axe_cheek',outlines[tier],tier,.070 if tier=='stone' else .045)]
    if tier=='wood': parts.append(box('blade_dowel','dark',[.025,.18,.070],(.10,.28,0),(0,0,-12)))
    elif tier=='stone': parts.append(profile('flaked_ear','edge',[(.09,.18),(.20,.22),(.14,.32),(.065,.29)],.012,(0,0,.039)))
    elif tier=='iron': parts.append(box('square_poll','dark',[.07,.095,.065],(-.055,.28,0)))
    elif tier=='gold': parts.append(profile('cheek_inlay','dark',[(.10,.22),(.14,.22),(.16,.34),(.12,.33)],.003,(0,0,.025)))
    return parts


def shovel(tier):
    # Broad central web plus two angled cheeks produce a shallow dished spade,
    # visibly different from a flat box at front and three-quarter angles.
    shape={
        'wood':[(-.07,.15),(.07,.15),(.085,.40),(.045,.46),(-.045,.46),(-.085,.40)],
        'stone':[(-.06,.15),(.065,.17),(.09,.36),(.025,.46),(-.04,.43),(-.09,.33)],
        'copper':[(-.055,.15),(.055,.15),(.09,.35),(.045,.46),(-.045,.46),(-.09,.35)],
        'iron':[(-.055,.15),(.055,.15),(.09,.36),(0,.48),(-.09,.36)],
        'gold':[(-.05,.16),(.05,.16),(.083,.34),(.036,.47),(-.036,.47),(-.083,.34)]}[tier]
    parts=[cutting('dished_spade_web',shape,tier,.022)]
    for side in (-1,1):
        cheek=cutting('left_spade_cheek' if side<0 else 'right_spade_cheek',
                      [(-.012,.20),(.012,.20),(.017,.36),(-.017,.36)],tier,.016)
        cheek.update(offset=[side*.074,0,.012],rotation=[0,side*24,0]);parts.append(cheek)
    parts.append(box('spade_spine','dark',[.023,.19,.027],(0,.22,-.018)))
    return parts


def hoe(tier):
    # Neck turns the working blade away from the shaft into the XZ plane.
    parts=[profile('angled_neck','dark',[(-.025,.22),(.03,.22),(.11,.30),(.10,.35),(.02,.29)],.045)]
    width={'wood':.095,'stone':.11,'copper':.125,'iron':.12,'gold':.105}[tier]
    pts=[(-width,.0),(width,.0),(width*.82,.16),(-width*.82,.16)]
    if tier=='stone': pts=[(-width,.015),(width,.0),(.09,.14),(-.06,.18)]
    blade=cutting('hoe_working_blade',pts,tier,.030 if tier=='stone' else .022)
    blade.update(offset=[.105,.30,0],rotation=[-72,0,0]);parts.append(blade)
    if tier in ('copper','iron','gold'):
        parts.append(box('blade_tang','edge',[.035,.024,.08],(.105,.30,-.035)))
    return parts


def sword(tier):
    widths={'wood':.058,'stone':.071,'copper':.064,'iron':.052,'gold':.046}
    w=widths[tier]
    if tier=='wood': pts=[(-w,-.05),(w,-.05),(w,.34),(.018,.45),(-.023,.44),(-w,.30)]
    elif tier=='stone': pts=[(-w,-.05),(w,-.05),(.080,.15),(.052,.32),(0,.46),(-.062,.31),(-.077,.12)]
    elif tier=='copper': pts=[(-w,-.05),(w,-.05),(.078,.22),(0,.47),(-.078,.22)]
    elif tier=='iron': pts=[(-w,-.05),(w,-.05),(.048,.34),(0,.49),(-.048,.34)]
    else: pts=[(-w,-.05),(w,-.05),(.05,.30),(.012,.49),(-.04,.36)]
    parts=[cutting('working_blade',pts,tier,.045 if tier=='stone' else .032),
           turned('grip_core','binding',((-.30,.026),(-.26,.029),(-.09,.024),(-.06,.03)),6),
           turned('pommel','dark',((-.34,.023),(-.325,.041),(-.30,.027)),6)]
    spread=.12 if tier=='wood' else .085 if tier=='stone' else .15 if tier=='copper' else .17
    for side in (-1,1):
        parts.append(box('left_guard' if side<0 else 'right_guard','head',
                         [spread,.035,.045],(side*spread*.48,-.065,0),(0,0,side*(14 if tier=='copper' else -8))))
    if tier=='wood': parts.append(box('laminate_key','dark',[.014,.34,.038],(0,.15,0)))
    elif tier=='stone':
        for i,y in enumerate((-.075,-.035)):
            parts.append(box(f'blade_lashing{i}','binding',[.15,.02,.06],(0,y,0),(0,0,(-1)**i*12)))
    else:
        for side in (-1,1):
            parts.append(profile('front_fuller' if side>0 else 'back_fuller','dark',
                                 [(-.009,.03),(.009,.03),(.007,.28),(0,.34),(-.007,.28)],.002,(0,0,side*.017)))
    return parts


def make_workshop_tools():
    result={}
    builders=dict(pickaxe=pickaxe,axe=axe,shovel=shovel,hoe=hoe,sword=sword)
    for family in FAMILIES:
        for tier in TIERS:
            id=f'{tier}_{family}'
            parts=builders[family](tier)
            if family!='sword': parts=haft(tier)+parts+fasteners(tier)
            source=design(id,parts,tool_materials(tier),grip=(0,-.22,0),density=150 if family=='pickaxe' else 185,scale=.67)
            source['presentation']['tp_euler_deg']=[-25,0,20]
            source['presentation']['tp_scale']=1.65
            source['authorship']=dict(construction='Cubatarium workshop coordinate drawings',
                reference_kind='functional real-world archetypes',external_meshes=False,external_pixels=False)
            result[id]=source
    return result


def seed_missing(root: Path):
    created=[]
    for id,data in make_workshop_tools().items():
        target=root/'tools/item_designs'/id/'design.yaml'
        if target.exists(): continue
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(yaml.safe_dump(data,sort_keys=False),encoding='utf-8')
        created.append(id)
    return created


if __name__=='__main__':
    print(seed_missing(Path(__file__).resolve().parents[2]))
