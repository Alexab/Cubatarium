"""Authored surface-space materials for fidelity pilots, no reference pixels.

Large pigmentation fields are sampled in anatomical coordinates, then baked
into box-UV islands. Small clustered marks describe fur rather than replacing
the animal's colour pattern with uncorrelated noise.
"""
from __future__ import annotations

import math

from PIL import Image


def surface_point(cube, face, a, b):
    x, y, z = cube.origin
    w, h, d = cube.size
    if face == "south":
        return x+a*w, y+(1-b)*h, z+d
    if face == "north":
        return x+(1-a)*w, y+(1-b)*h, z
    if face == "east":
        return x+w, y+(1-b)*h, z+(1-a)*d
    if face == "west":
        return x, y+(1-b)*h, z+a*d
    if face == "up":
        return x+a*w, y+h, z+(1-b)*d
    return x+a*w, y, z+b*d


def _shade(rgb, amount):
    return tuple(max(0, min(255, round(c+amount))) for c in rgb)


def _field(x, y, z):
    return (math.sin(z*.51 + math.sin(y*.61)*.8) +
            math.cos(x*.47 + z*.19)*.8 + math.sin(y*.43-x*.23)*.5)


def _pixel(style, cube, face, point, palette):
    x, y, z = point
    name = cube.name
    rgb = palette.get(cube.role, palette["body"])
    shade = {"up": 10, "down": -15, "east": -3, "west": 0, "north": 3, "south": 3}[face]
    if style == "bovine":
        if cube.role in ("body", "limb", "face"):
            field = _field(x, y, z)
            # Cream coat and distinct, broad chestnut patches. Different
            # positions on the same surface share the same pigmentation field.
            if field > .38 and not (y < 3.1 and cube.role == "limb"):
                rgb = palette["mark"]
            else:
                rgb = palette["body"]
        if name == "muzzle":
            rgb = palette["muzzle"]
            if face == "north" and abs(x) > 1.4 and abs(x) < 3 and 15.5 < y < 17:
                rgb = palette["eye"]
        if name == "head" and face in ("east", "west") and 19.0 < y < 21.2 and z < -11.0:
            rgb = palette["eye"]
        if name == "head" and face == "north" and 19 < y < 21.2 and abs(x) > 2.2:
            rgb = palette["eye"]
        if "ear" in name and face in ("north", "south"):
            rgb = palette["muzzle"] if 20.5 < y < 21.5 else palette["mark"]
        if cube.role == "horn":
            rgb = palette["horn"]
        if "hoof" in name or "tail_tuft" in name:
            rgb = palette["accent"]
    elif style in ("traveler", "undead"):
        undead = style == "undead"
        if name == "head":
            rgb = palette["face"]
            if y >= 30 or (face == "south" and y > 26):
                rgb = palette["accent"]
            elif face == "north":
                if 27 < y < 29 and 1 < abs(x) < 3:
                    rgb = palette["eye"]
                elif 25 < y < 26 and abs(x) < 2:
                    rgb = palette["mark"]
                elif undead and x > 1 and y < 27:
                    rgb = palette["belly"]
            elif face in ("east", "west") and y > 28:
                rgb = palette["accent"]
        elif name in ("right_sleeve", "left_sleeve"):
            rgb = palette["face"] if y < (18 if not undead else 16) else palette["body"]
            if 17 < y < 19 and not undead:
                rgb = palette["belly"]
        elif cube.role == "body":
            rgb = palette["body"]
            if face == "north":
                if abs(x) < 1 or (y > 21 and abs(x) < 2):
                    rgb = palette["belly"]
                if 14 < y < 15:
                    rgb = palette["accent"]
                if 15 < y < 18 and 1 < abs(x) < 3:
                    rgb = palette["mark"] if undead else _shade(palette["body"], -13)
        elif cube.role == "limb":
            rgb = palette["limb"] if y > 3 else palette["accent"]
            if y > 10 and undead and face == "north":
                rgb = palette["body"]
    elif style == "osseous":
        if name == "head" and face == "north":
            if 28 < y < 30 and 1 < abs(x) < 3:
                rgb = palette["eye"]
            elif 26.5 < y < 28 and abs(x) < .8:
                rgb = palette["eye"]
            elif 25 < y < 26.2 and abs(x) < 2.6 and int(x*2)%2 == 0:
                rgb = palette["mark"]
        if cube.role in ("body", "limb"):
            shade += 3*math.sin(y*.8+z*.4)
        if name == "head" and face in ("east", "west") and y>29 and abs(z)<1:
            rgb = palette["mark"]
    elif style in ("porcine", "ovine", "canine", "vulpine", "lagomorph"):
        if style == "ovine" and cube.role in ("body", "wool"):
            rgb = palette["wool"]
            tuft = (int(x//2)*7+int(z//2)*11+int(y//2)*3)%9
            shade += 6 if tuft<2 else (-5 if tuft>6 else 0)
        elif style in ("canine", "vulpine") and cube.role == "body":
            rgb = palette["body"]
            if face == "down" or y < cube.origin[1]+cube.size[1]*.3:
                rgb = palette["belly"]
            if style == "canine" and face == "up":
                rgb = palette["mark"]
        if name == "head":
            rgb = palette["face"]
            a=(x-cube.origin[0])/cube.size[0];b=(y-cube.origin[1])/cube.size[1]
            zz=(z-cube.origin[2])/cube.size[2]
            if face == "north" and .5 < b < .8 and (a<.2 or a>.8):
                rgb = palette["eye"]
            elif face in ("east", "west") and .5 < b < .8 and zz<.2:
                rgb = palette["eye"]
            elif style in ("vulpine", "canine") and b<.3:
                rgb = palette["belly"]
        if name == "muzzle":
            rgb = palette["muzzle"]
            if face == "north":
                if style == "porcine":
                    if abs(x)>.5 and abs(x)<1.6:
                        rgb = palette["mark"]
                elif style in ("canine", "vulpine", "ovine") and abs(x)<1.1 and (style != "ovine" or y>cube.origin[1]+cube.size[1]*.5):
                    rgb = palette["eye"]
        if name.startswith("ear"):
            rgb = palette["face"]
            if face == "north":
                rgb = palette["muzzle"]
            if style in ("vulpine", "canine") and y>cube.origin[1]+cube.size[1]*.65:
                rgb = palette["accent"] if style == "vulpine" else palette["mark"]
        if "hoof" in name or "paw" in name:
            rgb = palette["accent"]
        if "tail_tip" in name or name == "chest":
            rgb = palette["belly"]
    elif style == "avian":
        if name == "head":
            if face in ("east", "west") and 10<y<12 and z< -3.5:
                rgb = palette["eye"]
            if face == "north" and 10<y<12 and abs(x)>1:
                rgb = palette["eye"]
        if cube.role == "wing":
            rgb = palette["wing"]
            shade += -8 if int(y+z)%3==0 else 2
        if name == "beak":
            rgb = palette["horn"]
        if "comb" in name or "wattle" in name:
            rgb = palette["accent"]
    elif style == "puffin":
        if cube.role=="body":
            rgb=palette["belly"] if face=="north" else palette["body"]
        if name=="head":
            rgb=palette["body"]
            if face in ("east","west") and z< -4 and y<15:
                rgb=palette["face"]
                if 13<y<14.5 and -7.5<z< -5.5:rgb=palette["eye"]
            if face=="north" and y<14.5:rgb=palette["face"]
        if name.startswith("beak"):
            rgb=palette["horn"] if z< -10 else palette["accent"]
            if face in ("east","west") and -10.5<z< -9.5:
                rgb=palette["muzzle"]
        if cube.role=="limb":rgb=palette["horn"]
        if cube.role=="wing":
            rgb=palette["wing"]
            shade += -6 if (int(y)+int(z))%3==0 else 2
    elif style == "vespid":
        if name.startswith("abdomen"):
            rgb=palette["body"] if int((y+z*.35)/2)%2==0 else palette["mark"]
        if name.startswith("thorax"):
            rgb=palette["mark"]
            if face=="up" or (face=="north" and abs(x)<1):rgb=palette["body"]
        if name=="head":
            rgb=palette["body"]
            if face in ("east","west") and 12<y<14.5 and z< -2:
                rgb=palette["eye"]
            if face=="north" and abs(x)>1 and 12<y<14.5:
                rgb=palette["eye"]
        if cube.role=="wing":
            rgb=palette["wing"]
            xx=(x-cube.origin[0])/cube.size[0]
            if abs(xx-.5)<.18 or (int(z-cube.origin[2])%3==0):
                rgb=palette["accent"]
        if name.startswith(("antenna","mandible","stinger")) or cube.role=="limb":
            rgb=palette["mark"]
    elif style == "penguin":
        if cube.role == "body":
            rgb = palette["belly"] if face=="north" or (face in ("east","west") and z < .2 and y<10) else palette["body"]
        if name == "head":
            rgb = palette["body"]
            if face=="north" and y<13:
                rgb = palette["belly"]
            if face in ("east","west") and 13<y<14 and z< -1.5:
                rgb = palette["eye"]
            if face in ("east","west") and y>14 and z< -1.5:
                rgb = palette["belly"]
            if face=="north" and abs(x)>1.5 and y>13:
                rgb = palette["belly"] if y>14 else palette["eye"]
            if face in ("east","west") and 12<y<13 and z> -1.5:
                rgb = palette["accent"]
        if name.startswith("beak") or cube.role=="limb":
            rgb = palette["horn"]
        if cube.role=="wing":
            rgb = palette["body"]
            if face=="north" and y<8:
                rgb = palette["mark"]
    elif style == "strigid":
        if name == "head":
            rgb = palette["body"]
            if face=="north":
                d=((abs(x)-2)/1.8)**2+((y-16)/2.5)**2
                if d<1:rgb=palette["face"]
                if 1<abs(x)<3 and 16<y<17:rgb=palette["accent"]
                if 1<abs(x)<2 and 16<y<17:rgb=palette["eye"]
                if y>18:rgb=palette["mark"]
        if cube.role=="body":
            rgb = palette["belly"] if face=="north" else palette["body"]
            if face=="north" and (int(y)+int(abs(x))*2)%4==0:
                rgb=palette["mark"]
        if cube.role=="wing":
            rgb=palette["wing"]
            shade += -9 if (int(y/2)+int(z))%3==0 else 3
        if name.startswith("ear"):rgb=palette["mark"]
        if name.startswith("beak") or cube.role=="limb":rgb=palette["horn"]
    elif style == "ursine":
        if cube.role == "body":
            rgb = palette["mark"] if -2 < z < 2.5 else palette["body"]
        if name == "head":
            rgb = palette["face"]
            if face == "north":
                dx,dy = abs(x)-3, y-10.5+(abs(x)-3)*.4
                if (dx/1.8)**2+(dy/2.2)**2 < 1:
                    rgb = palette["mark"]
                if 2 < abs(x) < 3.6 and 10 < y < 11.2:
                    rgb = palette["eye"]
            if face in ("east","west") and 9 < y < 12 and z < -6.5:
                rgb = palette["mark"]
        if name.startswith("ear") or cube.role == "limb":
            rgb = palette["mark"]
        if name == "nose":
            rgb = palette["eye"]
        if name in ("muzzle","jaw"):
            rgb = palette["face"]
            if face == "north" and y < 7:
                rgb = palette["accent"]
    elif style == "suid":
        if cube.role in ("body","face"):
            rgb = palette["body"] if cube.role=="body" else palette["face"]
            shade += 4*math.sin(z*.7+x*.35)+2*math.sin(y*1.6+z*.4)
        if name == "head" and face in ("east","west") and 10 < y < 12 and z < -8:
            rgb = palette["eye"]
        if name == "rhinarium":
            rgb = palette["muzzle"]
            if face == "north" and 1 < abs(x) < 2.3 and 7 < y < 8.5:
                rgb = palette["eye"]
        if name.startswith("wart"):
            rgb = palette["accent"]
        if name.startswith("ear") and face in ("north","south"):
            rgb = palette["accent"]
        if name.startswith("mane") or name == "tail_tuft":
            rgb = palette["mark"]
        if cube.role == "limb" and y < 1.1:
            rgb = palette["mark"]
        if cube.role == "horn":
            rgb = palette["horn"]
    elif style in ("rodent", "feline", "erinaceid"):
        if cube.role == "body":
            rgb = palette["body"]
            if face == "down" or y < (2.5 if style != "feline" else 5):
                rgb = palette["belly"]
            if style == "feline":
                # Broad tabby bands follow the trunk and leave a cream chest.
                if (int(z+2)%4 == 0 and y>5) or (z < -3 and face == "north"):
                    rgb = palette["mark"]
            if style == "rodent":
                shade += 3*math.sin(x*.6+z*.5)
        if name in ("head", "muzzle", "cheek"):
            rgb = palette["face"]
            if style == "feline" and y>8:
                rgb = palette["body"]
            if name == "head" and face in ("east", "west"):
                eye_y = (2.5,3.7) if style=="rodent" else ((6,7.4) if style=="feline" else (3,4.5))
                if eye_y[0] < y < eye_y[1] and z < (-5 if style=="rodent" else -6):
                    rgb = palette["eye"]
            if style=="feline" and name=="muzzle" and face=="north" and abs(x)>.7:
                rgb = palette["belly"]
        if name == "nose":
            rgb = palette["muzzle"]
        if name.startswith("ear"):
            rgb = palette["muzzle"] if face in ("north","south") else palette["body"]
        if name.startswith("tail"):
            rgb = palette["limb"] if style=="rodent" else palette["body"]
            if style=="rodent":
                shade += -8 if int(z)%2 == 0 else 2
            elif style=="feline" and int(z)%3==0:
                rgb = palette["mark"]
        if cube.role == "limb":
            rgb = palette["limb"]
            if style=="feline":
                rgb = palette["belly"] if y<2 else palette["body"]
        if style == "erinaceid" and cube.role == "horn":
            rgb = palette["horn"] if z > cube.origin[2]+cube.size[2]*.6 else palette["mark"]
            if face == "up":
                shade += 7
    elif style == "lava_flan":
        rgb=palette.get(cube.role,palette["body"])
        # Continuous gravity-aligned flow channels under broken dark crust.
        if cube.role in ("body","face","accent"):
            channel=math.sin(x*1.05+z*.67+.4*math.sin(y*.8))
            crust=math.sin(y*.9+x*.4+z*.3)>.45
            rgb=palette["accent"] if crust and channel<.5 else palette["face"] if channel>.65 else palette["body"]
            shade+=4*math.sin(y*.7)
        if name=="head" and face=="north":
            if 13.3<y<15.8 and 1<abs(x)<3.5:rgb=palette["mark"]
            if 14<y<15 and 1.7<abs(x)<3:rgb=palette["eye"]
        if name=="mouth_interior":rgb=palette["mark"]
        if "tooth" in name:rgb=palette["horn"]
    elif style == "mese_monster":
        # Mineral facets, dark wing membranes and coherent violet veins.
        rgb = palette.get(cube.role, palette["body"])
        if cube.role in ("body","face"):
            if abs(x)<2.2 and y>22:
                rgb=palette["accent"]
            shade += 7 if int(y+z*.4)%5<2 else -4
        if cube.role=="wing":
            edge=min(abs(z-cube.origin[2]),abs(z-cube.origin[2]-cube.size[2]))
            rgb=palette["body"] if edge<1.2 else palette["wing"]
            if int(abs(x)*.6+z*.8)%7==0:rgb=palette["accent"]
        if name=="head" and face=="south" and 21<y<24 and 3<abs(x)<5.5:
            rgb=palette["eye"] if abs(x)>4 else palette["mark"]
        if name.startswith("pinch_"):rgb=palette["accent"] if z< -28 else palette["horn"]
    elif style in ("tree_monster","treeman"):
        tall=style=='treeman'
        if cube.role=='wing':
            # Large leaf clusters and smaller grouped edges, as in our plant
            # material workflow, sampled continuously through the canopy.
            field=_field(x*.48,y*.48,z*.48)
            rgb=palette['wing']
            if field>.65:rgb=palette['accent']
            elif field<-.8:rgb=palette['mark']
            shade+=4*math.sin(x*1.7+y*.9+z*1.1)
        else:
            gx,gy,gz=x,y,z
            if 'planform_y' in cube.shape:
                # Sample the actual sloped branch surface, then orient grain
                # along the branch instead of stamping vertical stripes on it.
                ox,oy,oz=cube.origin;w,h,d=cube.size
                fx,fy,fz=(x-ox)/w,(y-oy)/h,(z-oz)/d
                weights=[(1-fx)*(1-fz),fx*(1-fz),(1-fx)*fz,fx*fz]
                q=cube.shape['planform'];lo,hi=cube.shape['planform_y']
                x=ox+w*sum(t*p[0] for t,p in zip(weights,q))
                z=oz+d*sum(t*p[1] for t,p in zip(weights,q))
                y=oy+h*sum(t*(a*(1-fy)+b*fy) for t,a,b in zip(weights,lo,hi))
                start=[ox+w*(q[0][0]+q[1][0])*.5,oy+h*sum(lo[:2]+hi[:2])*.25,oz+d*(q[0][1]+q[1][1])*.5]
                end=[ox+w*(q[2][0]+q[3][0])*.5,oy+h*sum(lo[2:]+hi[2:])*.25,oz+d*(q[2][1]+q[3][1])*.5]
                axis=[b-a for a,b in zip(start,end)];length=math.sqrt(sum(t*t for t in axis));axis=[t/length for t in axis] if length>1e-8 else [0,1,0]
                horizontal=math.hypot(axis[0],axis[2]);u=[-axis[2]/horizontal,0,axis[0]/horizontal] if horizontal>1e-8 else [1,0,0]
                v=[axis[1]*u[2],axis[2]*u[0]-axis[0]*u[2],-axis[1]*u[0]]
                delta=[x-start[0],y-start[1],z-start[2]]
                gx=sum(a*b for a,b in zip(delta,u));gy=sum(a*b for a,b in zip(delta,axis));gz=sum(a*b for a,b in zip(delta,v))
            grain=abs(math.sin(gx*1.45+gz*1.1+.35*math.sin(gy*.3)))
            if grain<.2:rgb=palette['mark']
            elif grain>.88:rgb=palette['belly']
            shade+=3*_field(x*.6,y*.4,z*.6)
            if tall and _field(x*.4,y*.3,z*.4)>.8:rgb=palette['accent']
        if cube.role=='face' and face=='north':
            if (53<y<54.4 if tall else 25.2<y<26.5) and .7<abs(x)<2.5:rgb=palette['eye']
            if (49.5<y<50.8 if tall else 22.5<y<23.8) and abs(x)<1.5:rgb=palette['mark']
            if not tall and abs(x+.5+.3*math.sin(y*.6))<.4:rgb=palette['mark']
    elif style == "golem":
        shade+=3*_field(x*.7,y*.6,z*.65)
        if 'joint' in name or name=='waist_connector':rgb=_shade(palette['mark'],15)
        elif name!='head':
            layer=abs(math.sin(y*.6+.2*math.sin(x*.4+z*.5)))
            if layer<.13:rgb=palette['belly']
            elif _field(x,y*.8,z)>1.5:shade+=7
        if name=='head' and face=='north':
            rgb=palette['mark']
            if abs(x)>2.4 or y<33 or y>36.7:rgb=palette['belly']
            elif 34.3<y<35.4 and .8<abs(x)<2.2:rgb=palette['eye']
    elif style in ("dungeon_master","land_guard"):
        master=style=='dungeon_master'
        shade+=(3 if master else 5)*_field(x*.65,y*.55,z*.6)
        if name in ('core','chest'):
            shade-=abs(x)*.6
            if master:
                seam=abs(math.sin(y*.34+.2*math.sin(x*.45+z*.35)))<.12
                if seam:rgb=palette['mark']
                elif _field(x*.7,y*.7,z)>1.3:rgb=palette['belly']
            else:
                # Moss follows upper ledges and larger material fields;
                # exposed stone remains visible, without borrowed block tiles.
                if y>35 or (_field(x*.65,y*.45,z)>1 and y>20):rgb=palette['accent']
                if abs(math.sin(y*.46+.4*math.sin(x*.4+z*.3)))<.1:rgb=palette['mark']
        if name.startswith('foot'):rgb=palette['mark'] if y<1 else palette['limb']
        if name=='head' and face=='north':
            if not master and abs(x)>2.7:rgb=palette['belly']
            if 39<y<40.4 and 1.3<abs(x)<3.2:rgb=palette['eye']
            elif 36.5<y<37.6 and abs(x)<2.3:rgb=palette['mark']
        if name.startswith('brow'):rgb=palette['mark']
        if name=='jaw' and face=='north':
            rgb=palette['mark'] if abs(x)<2.5 else palette['face']
            if master and y>35 and .5<abs(x)<2.5:rgb=palette['horn']
    elif style in ("oerkki","orc","ogre"):
        if style=='oerkki':
            # Charcoal anatomy stays dark while planes and face remain legible.
            shade+=3*_field(x*.7,y*.6,z)
            if name in ('core','chest') and face=='north' and abs(x)<1.5:rgb=palette['belly']
            if name=='head' and face=='north':
                if 27<y<28.3 and .7<abs(x)<2.1:rgb=palette['eye']
                elif y<26 and abs(x)<1.7:rgb=palette['mark']
            if name=='jaw' and face=='north':
                rgb=palette['mark']
                if y>24 and .4<abs(x)<2.5:rgb=palette['horn']
        else:
            shade+=3*_field(x*.6,y*.5,z*.7)
            # A diagonal leather harness, waist band and boots belong to the
            # character's body coordinates, with no borrowed tiled armor.
            if name in ('core','chest'):
                if face=='north' and abs(x+(y-(30 if style=='ogre' else 27))*.65)<1.1:rgb=palette['accent']
                if (15<y<18 if style=='ogre' else 18<y<20):rgb=palette['accent']
            if name.startswith('foot') or (name.startswith('shin') and y<(6 if style=='ogre' else 8)):rgb=palette['accent']
            if name.startswith('shoulder'):
                rgb=palette['accent'] if name.endswith('left') else _shade(palette['mark'],10)
                if abs(y-(40 if style=='ogre' else 33))<.7:rgb=palette['belly']
            if name.startswith('club'):
                rgb=palette['accent'];shade+=4*math.sin(z*1.7+x*.2)
                if name=='club_head' and (abs(z+37)<.65 or abs(z+31)<.65):rgb=palette['mark']
            if name=='head' and face=='north':
                floor=cube.origin[1]
                if floor+3<y<floor+4.2 and 1.5<abs(x)<3.5:rgb=palette['eye']
                elif y<floor+1 and abs(x)<2.5:rgb=palette['mark']
            if name=='jaw' and face=='north' and abs(x)<2.5:rgb=palette['mark']
    elif style in ("sand_monster","stone_monster","dirt_monster"):
        # Geological layers run through anatomical space, never copied block
        # tile grids. Each material has its own scale and fracture language.
        if style == "sand_monster":
            layer=math.sin(y*1.3+.4*math.sin(x*.7+z*.6))
            shade += 7*layer
            if name=='head' and face=='up':rgb=palette['accent']
            if name.startswith(('foot','hand')):rgb=palette['mark']
        elif style == "stone_monster":
            seam=abs(math.sin(y*.73+.65*math.sin(x*.55+z*.45)))<.1
            if seam:rgb=palette['mark']
            elif _field(x*1.4,y*1.5,z)>1.6:rgb=palette['belly']
            shade+=4*_field(x*.8,y*.7,z)
        else:
            field=_field(x*1.7,y*1.8,z*1.5)
            if field>1.7:rgb=palette['belly']
            elif field<-1.6:rgb=palette['mark']
            shade+=5*field+3*math.sin(y*1.8+x*.3)
        if name=='head' and face=='north':
            top=cube.origin[1]+cube.size[1]
            eye_x=(.8<abs(x)<1.7) if style=='sand_monster' else (1.3<abs(x)<2.3)
            if top-2.8<y<top-1.8 and eye_x:
                rgb=palette['eye']
            elif y<cube.origin[1]+1.1 and abs(x)<1.6:
                rgb=palette['mark']
        if name=='jaw' and face=='north':rgb=palette['mark'] if abs(x)<1 else palette['face']
    elif style in ("brachyuran","nephropid"):
        rgb=palette.get(cube.role,palette['body'])
        if cube.role in ('body','face','wing'):
            rgb=palette['belly'] if face=='down' else palette['body']
            if face=='up' and _field(x*1.4,y,z*1.4)>.8:rgb=palette['accent']
            if name.startswith('tail') and face in ('east','west') and int(z*1.5)%4==0:rgb=palette['accent']
        if 'finger' in name:
            rgb=palette['horn'] if z<cube.origin[2]+cube.size[2]*.35 else palette['body']
        if name.startswith('eye'):
            rgb=palette['eye'] if 'ball' in name else palette['mark']
            if 'ball' in name and face=='north' and x>cube.origin[0]+.5:rgb=palette['belly']
        if name.startswith('antenna'):rgb=palette['accent']
        if cube.role=='limb':
            rgb=palette['limb']
            if int(y+z)%5==0:shade-=8
    elif style == "octopod":
        rgb=palette['body']
        if cube.role in ('body','face'):
            if _field(x,y,z)>.7:rgb=palette['accent']
            if face=='down':rgb=palette['belly']
            if name=='mantle' and _field(x,y,z)>1.2:rgb=palette['belly']
            if name=='head' and face in ('east','west') and 5.4<y<7.8 and z< -3:
                rgb=palette['horn']
                if 6<y<7.2 and z< -3.8:rgb=palette['eye']
            if name=='head' and face=='north' and 5.4<y<7.8 and abs(x)>1.8:
                rgb=palette['horn']
                if 6<y<7.2 and abs(x)>2.5:rgb=palette['eye']
        if cube.role=='limb':
            rgb=palette['body'] if face!='down' else palette['belly']
            if face=='down' and (int(x*2)+int(z*2))%3==0:rgb=palette['sucker'] if 'sucker' in palette else palette['horn']
        if name=='siphon' and face=='north':rgb=palette['mark']
    elif style == "rorqual":
        if cube.role in ("body","face"):
            rgb=palette["belly"] if y<4 or face=="down" else palette["body"]
            if y<5 and z< -3 and face in ("down","east","west") and int(x*2)%3==0:
                rgb=palette["accent"]  # own longitudinal throat pleats
            if name=="head" and face in ("east","west") and 7<y<9 and -17<z< -14:
                rgb=palette["eye"]
            if name=="rostrum" and face=="up" and abs(x)<1 and z< -20:
                rgb=palette["accent"]
            if name=="jaw" and y>3.3 and face in ("east","west"):
                rgb=palette["mark"]
        if cube.role=="wing":
            rgb=palette["wing"] if face!="down" else palette["belly"]
            if name.startswith('flipper') and face in ('up','down'):rgb=palette['belly']
            if name.startswith('fluke') and face=='down' and _field(x,y,z)>.1:rgb=palette["mark"]
        if name=='blowhole':rgb=palette["eye"]
    elif style == "sea_serpent":
        rgb=palette["body"]
        if cube.role in ("body","face"):
            if face=="north":rgb=palette["belly"]
            if (int(y*1.2)+int(z*.7))%4==0:shade-=12
            if face in ("east","west") and int(y+x+z*2)%7==0:rgb=palette["accent"]
            if name=='head' and face in ('east','west') and 37<y<39 and z< -5:
                rgb=palette['eye']
            if name=='snout' and face=='north' and abs(x)>1 and y>37.5:rgb=palette['mark']
            if name=='jaw':rgb=palette['muzzle']
        if cube.role=='horn':rgb=palette['horn'] if y>43 else palette['accent']
        if cube.role=='wing':rgb=palette['wing']
    elif style in ("sirenian", "phocid"):
        if style == "phocid":
            y += .5  # Coat coordinates before grounding the authored body.
        if cube.role in ("body", "face"):
            belly = 4.5 if style == "sirenian" else 3.4
            rgb = palette["belly"] if y < belly or face=="down" else palette["body"]
            if style == "phocid" and y>3.4:
                # Sparse coherent coat spots, sampled in anatomy space.
                field=math.sin(x*1.7+z*.9)*math.sin(y*1.8-z*.6)
                if field>.72:rgb=palette["mark"]
            if style == "sirenian":
                shade += 3*math.sin(z*.8+y*.7)
                if name.startswith("lip"):
                    rgb=palette["muzzle"]
            if name == "head" and face in ("east","west"):
                eye=(7 < y < 8.5 and -14 < z < -12) if style=="sirenian" else (6 < y < 7.5 and z < -14)
                if eye:rgb=palette["eye"]
            if name == "muzzle" or name.startswith("lip"):
                if face=="north":
                    rgb=palette["muzzle"]
                    if abs(x)>1 and abs(x)<2.5 and y>4.5:rgb=palette["mark"]
                    if y<4.5 and int(abs(x)*2+y)%3==0:rgb=palette["accent"]
        if cube.role=="wing":
            rgb=palette["wing"] if face!="down" else palette["belly"]
            if "flipper" in name and face in ("up","down") and int(z)%3==0:
                shade-=8
        if name=="nose":rgb=palette["eye"]
        if cube.role=="horn":rgb=palette["horn"]
    elif style == "selachian":
        if cube.role in ("body", "face"):
            rgb = palette["belly"] if y < 5.3 or face == "down" else palette["body"]
            if face in ("east", "west") and 5.3 <= y < 6.3:
                rgb = palette["accent"]
            if name == "head" and face in ("east", "west"):
                if 7 < y < 8.5 and z < -10:
                    rgb = palette["eye"]
                elif -8.7 < z < -6.2 and int(-z*2)%2==0 and 5.7 < y < 7.7:
                    rgb = palette["mark"]  # five anatomical gill slits
            if name == "jaw" and face != "down" and y > 4.8:
                rgb = palette["muzzle"]
            if name == "snout" and face == "north" and abs(x)>1.7 and 6 < y < 7:
                rgb = palette["mark"]
        if cube.role == "wing":
            rgb = palette["wing"] if face != "down" else palette["belly"]
            shade += -10 if int(z*2+y)%5==0 else 0
        if name.startswith("tooth"):
            rgb = palette["horn"]
    elif style == "hippocampal":
        rgb = palette["body"] if cube.role != "wing" else palette["wing"]
        if cube.role in ("body", "face"):
            if face == "north" or (name.startswith("tail") and face=="down"):
                rgb = palette["belly"]
            if (int(y*1.5)+int(z*.7))%4 == 0:
                rgb = palette["accent"]
            if face in ("east", "west") and int(y*2+z)%5==0:
                shade -= 16
            if name == "head" and face in ("east", "west") and 23 < y < 24.5 and z< -4:
                rgb = palette["eye"]
        if name == "snout" and face=="north":
            rgb = palette["muzzle"]
        if cube.role == "horn":
            rgb = palette["accent"]
        if cube.role == "wing":
            shade += 14 if int(y+z*2)%3==0 else -8
    elif style == "salmonid":
        if cube.role in ("body", "face"):
            rgb = palette["body"] if y > 5.8 else palette["belly"]
            if face in ("east", "west"):
                if 4 < y < 5.8:
                    rgb = palette["accent"]
                if y > 5 and (int(z)*7+int(y)*3)%11 < 2:
                    rgb = palette["mark"]
            if name == "head" and face in ("east", "west") and 4.5 < y < 5.8 and z < -10:
                rgb = palette["eye"]
            if name == "head" and face == "north" and y < 3.6:
                rgb = palette["muzzle"]
        elif cube.role == "wing":
            rgb = palette["wing"]
            if face in ("east", "west"):
                shade += -18 if int(z+y)%3==0 else 4
    elif style == "cetacean":
        if cube.role in ("body", "face"):
            rgb = palette["belly"] if y < 4 or face == "down" else palette["body"]
            if face in ("east", "west") and 4 < y < 5.5:
                rgb = palette["face"]
            if name == "head" and face in ("east", "west") and 5 < y < 7 and z < -10:
                rgb = palette["eye"]
            if name == "rostrum" and face in ("east", "west") and y < 3:
                rgb = palette["mark"]
        elif cube.role == "wing":
            rgb = palette["wing"] if face != "down" else palette["belly"]
    elif style == "cephalopod":
        if cube.role == "body":
            rgb = palette["body"]
            if face in ("east", "west") and abs(z)<2 and y>25:
                rgb = palette["belly"]
            shade += 5*math.sin(y*.35+z*.5+x*.4)
            if face in ("east", "west") and 21 < y < 24 and -4 < z < -1:
                rgb = palette["accent"]
                if 22 < y < 24 and -3 < z < -1:
                    rgb = palette["eye"]
        if cube.role == "limb":
            cx = cube.origin[0]+cube.size[0]*.5
            cz = cube.origin[2]+cube.size[2]*.5
            inward = ("west" if cx>0 else "east") if abs(cx)>abs(cz) else ("north" if cz>0 else "south")
            if face == inward and int(y)%3==0:
                rgb = palette["accent"]
            if y < 2:
                rgb = palette["belly"]
    elif style == "chelonian":
        if cube.role == "shell":
            rgb = palette["shell"]
            if face == "up":
                # Staggered scutes follow the body surface, not UV islands.
                row = math.floor(z/4.3)
                sites = []
                for rr in range(row-1, row+2):
                    shift = 2.5 if rr%2 else 0
                    column = math.floor((x-shift)/5)
                    for cc in range(column-1, column+2):
                        sites.append(((x-(cc*5+shift))**2+(z-rr*4.3)**2, rr, cc))
                sites.sort()
                if sites[1][0]-sites[0][0] < 6.5:
                    rgb = palette["mark"]
                else:
                    shade += 7-2*sites[0][0]+3*math.sin(sites[0][1]*2+sites[0][2])
            elif y < 3.1:
                rgb = palette["accent"]
                shade += -7 if (int(z)+int(x))%4 == 0 else 0
            else:
                shade += 4*math.sin(z*.8+x*.5)
        elif name == "head":
            rgb = palette["face"]
            if face in ("east", "west") and 3 < y < 5 and z < -12:
                rgb = palette["eye"]
            if face == "north" and 1 < y < 2.5:
                rgb = palette["belly"]
        elif cube.role == "limb":
            if face == "up" and (int(x)+int(z)*2)%5 == 0:
                rgb = palette["mark"]
            if face == "down":
                rgb = palette["belly"]
    elif style == "arachnid":
        if name == "abdomen":
            # A restrained paired dorsal field, continuous across UV seams.
            if face == "up" and .8 < abs(x) < 3.6 and 3 < z < 10:
                rgb = palette["mark"]
                shade += 4*math.cos(z*1.2)
        if name == "head" and face == "north":
            # Six separate one-texel ocelli instead of two red slabs.
            if (5 < y < 6 and (abs(x) < 1 or abs(x) > 2)) or (y > 6 and 1 < abs(x) < 2):
                rgb = palette["eye"]
        if "toe" in name or "fang" in name:
            rgb = palette["accent"]
        if cube.role == "limb":
            shade += 3*math.sin(abs(x)*1.3+z*.5)
    elif style == "apian":
        if name == "abdomen":
            rgb = palette["body"] if int(z/1.8)%2==0 else palette["mark"]
        if name == "thorax":
            rgb = palette["accent"]
        if name == "head":
            rgb = palette["face"]
            if face == "north" and 4<y<7 and abs(x)>1:
                rgb = palette["eye"]
            if face in ("east", "west") and 4<y<7 and z< -6:
                rgb = palette["eye"]
        if cube.role == "wing":
            rgb = palette["wing"]
            shade += -28 if (int(abs(x))+int(z)*2)%5==0 else 4
    elif style == "hermitcrab":
        if cube.role == "shell":
            rgb = palette["shell"]
            if face in ("east", "west"):
                yy, zz = y-8, z+3
                radius = math.hypot(yy, zz*.65)
                spiral = (radius*1.1-math.atan2(yy,zz)*.75) % 3.8
                if spiral < .85:
                    rgb = palette["mark"]
            elif face == "south" and y < 8 and abs(x) < 4:
                rgb = palette["accent"]
            else:
                shade += 5*math.sin((z+y)*.7)
        elif cube.role == "eye":
            rgb = palette["eye"]
        else:
            rgb = palette.get(cube.role,palette["body"])
            shade += 3*math.sin(z*1.6+x*.8)
            if "finger" in name and z>19:
                rgb = palette["horn"]
    elif style == "ray":
        rgb = palette["belly"] if face == "down" else palette["body"]
        if face == "up":
            field = math.sin(x*1.9+z*.7)*math.cos(z*1.3-x*.3)
            if field > .86 and abs(x) > 2:
                rgb = palette["mark"]
        if "eye" in name:
            rgb = palette["eye"] if face in ("up", "south") else palette["body"]
        if name.startswith("tail"):
            rgb = palette["accent"]
    elif style == "butterfly":
        if cube.role == "wing":
            rgb = palette["wing"]
            radial = abs(x)*.6+abs(z)*.3
            if radial > 5.6 or abs(math.sin(z*.65+x*.3)) < .16:
                rgb = palette["accent"]
            if (abs(x)-4.8)**2+(z-2.2)**2 < 2:
                rgb = palette["mark"]
            if (abs(x)-4.8)**2+(z-2.2)**2 < .4:
                rgb = palette["eye"]
        else:
            rgb = palette["body"]
            if name == "head" and face == "south":
                rgb = palette["eye"]
    elif style == "badger":
        if cube.role == "body":
            rgb = palette["body"]
            shade += -6 if abs(x)>3.9 else 3
        if name in ("head", "cheek"):
            # Two dark eye stripes running along an ivory face, not a black
            # rectangle with a second generic yellow muzzle.
            stripe = .9 < abs(x) < 2.35
            rgb = palette["mark"] if stripe or face in ("east", "west") else palette["face"]
            if y < 5.4:
                rgb = palette["face"]
            if face in ("east", "west") and 7.0 < y < 8.1 and z > 9.5:
                rgb = palette["eye"]
        if name == "muzzle":
            rgb = palette["face"]
        if name == "nose":
            rgb = palette["accent"]
        if "ear" in name:
            rgb = palette["face"] if face == "south" else palette["mark"]
        if cube.role == "horn":
            rgb = palette["horn"]
    # Texture clusters share direction and physical scale, with restrained
    # contrast. No random per-island spots or copied reference patches.
    if cube.role in ("body", "face", "limb", "belly") and "hoof" not in name:
        cluster = (int(math.floor(x/2))*17 + int(math.floor(y/2))*11 + int(math.floor(z))*7) % 19
        shade += 3 if cluster in (0, 1, 2) else (-3 if cluster in (10, 11) else 0)
    if rgb == palette.get("eye"):
        shade = 0
    return _shade(rgb, shade)+(255,)


def bake_surface_atlas(model, palette, style):
    image = Image.new("RGBA", model.texture_size, (0, 0, 0, 0))
    pixels = image.load()
    for cube in model.cubes():
        u, v = cube.uv
        w, h, d = cube.size
        regions = {"west": (u, v+d, d, h), "north": (u+d, v+d, w, h),
                   "east": (u+d+w, v+d, d, h), "south": (u+2*d+w, v+d, w, h),
                   "up": (u+d, v, w, d), "down": (u+d+w, v, w, d)}
        for face, (u0, v0, width, height) in regions.items():
            for j in range(height):
                for i in range(width):
                    point = surface_point(cube, face, (i+.5)/width, (j+.5)/height)
                    pixels[u0+i, v0+j] = _pixel(style, cube, face, point, palette)
    return image

