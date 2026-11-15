"""Original layered item surfaces, painted in each named island's local space.

No reference images or asset packs are sampled. Optional RGBA artist overlays
are source files, read only, and must match the declared surface resolution.
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path
from PIL import Image, ImageDraw


def paint_surface(width, height, material, name):
    color=material['color']
    style=material.get('pattern','plain')
    if material.get('grain_axis','v') not in ('u','v'):
        raise ValueError('Wood grain axis must be u or v')
    if style not in ('plain','wood','leather','forged','hammered','stone','food','fiber','water','feather','painted','char','flame','cast','ore','coal','wool','grain','meat','roast','fruit','leaf'):
        raise ValueError(f'Unknown surface pattern: {style}')
    phase=int.from_bytes(hashlib.sha256(name.encode()).digest()[:2],'little')/1000
    image=Image.new('RGBA',(width,height))
    for y in range(height):
        for x in range(width):
            u,v=x/max(width-1,1),y/max(height-1,1)
            # Broad variation, then clustered manufacturing marks; no white noise.
            shade=3*math.sin(u*4+phase)+2*math.sin(v*3-phase)
            if style=='wood':
                along,across=(u,v) if material.get('grain_axis')=='u' else (v,u)
                grain=math.sin(across*35+math.sin(along*7+phase)*1.8)
                shade+=-10*max(0,grain)**8+4*math.sin(across*12+along)
            elif style=='leather':
                shade+=2*math.sin(x*.7)*math.sin(y*.9)
            elif style in ('forged','hammered'):
                shade+=4*math.sin(u*20+phase)*math.sin(v*16)
                if style=='hammered': shade+=3*math.sin(x*.65+y*.35)
            elif style=='stone':
                shade+=5*math.sin(u*17+v*11)+3*math.cos(v*24-u*6)
            elif style=='fruit':
                shade+=8*math.sin(u*3.5+phase*.12)*math.cos(v*2.2)+3*math.sin(u*8+v*5)
            elif style=='leaf':
                shade+=5*math.sin(u*4)-6*abs(v-.5)
            elif style=='food':
                shade+=7*math.sin(u*6+phase)*math.sin(v*4)+2*math.sin(x*.8+y)
            elif style=='water':
                shade+=2*math.sin(u*8+v*3)
            elif style=='fiber':
                shade+=5*math.sin(u*30+v*12)
            elif style=='feather':
                shade+=5*math.sin(u*28-v*18)-12*max(0,1-abs(v-.5)*20)
            elif style=='cast':
                shade+=3*math.sin(u*9)+2*math.sin(v*23+phase)
            elif style=='ore':
                shade+=8*math.sin(u*14+v*7)*math.cos(v*11-phase)
            elif style=='coal':
                shade+=4*math.sin(u*11+v*7)+3*math.sin(v*17-u*4)
            elif style=='char':
                shade-=14*max(0,math.sin(v*20+math.sin(u*6)))**10
            elif style=='wool':
                shade+=4*math.sin(u*14+math.sin(v*14))*math.cos(v*17)
            elif style=='grain':
                shade-=12*math.exp(-((v-.5)*15)**2)
            elif style=='meat':
                shade+=7*math.sin(u*7+v*3)*math.sin(v*5+phase)
            elif style=='roast':
                shade-=16*max(0,math.sin(u*13+v*5))**12
            elif style=='flame':
                shade+=14*(.5-u)
            pixel=tuple(max(0,min(255,round(c+shade))) for c in color)
            image.putpixel((x,y),pixel+(255,))
    draw=ImageDraw.Draw(image)
    if min(width,height)>=8:
        if style=='fruit' and '.ring' in name:
            # A small, offset cluster of peel pores, not uniform texture noise.
            warm=tuple(min(255,c+17) for c in color)+(255,)
            for i in range(3):
                x=width*(.27+.12*i+.035*math.sin(phase+i))
                y=height*(.42+.11*(i%2)+.05*math.cos(phase-i))
                draw.line((x,y,x+max(1,width//60),y),fill=warm,width=1)
        elif style=='leaf' and name.endswith(('.front','.back')):
            vein=tuple(min(255,c+25) for c in color)+(255,)
            draw.line((width*.10,height*.60,width*.84,height*.43),fill=vein,width=1)
            for u in (.34,.58):
                v=.60-(u-.10)*.17/.74
                draw.line((width*u,height*v,width*(u-.12),height*(v-.19)),fill=vein,width=1)
                draw.line((width*u,height*v,width*(u-.10),height*(v+.19)),fill=vein,width=1)
        elif style=='meat' and name.endswith(('.front','.back')):
            for shift in (.18,.60):
                draw.arc((width*.12,height*(shift-.15),width*.83,height*(shift+.25)),15,160,fill=(211,157,136,255),width=max(1,min(width,height)//40))
        elif style=='painted':
            # A restrained hand-painted lozenge on the shield spine, without
            # borrowing a crest, national symbol or external texture pixels.
            accent=tuple(min(255,c+24) for c in color)+(255,)
            draw.line((width*.5,height*.18,width*.72,height*.37,width*.5,height*.57,width*.28,height*.37,width*.5,height*.18),fill=accent,width=max(1,width//35))
        elif style=='wool':
            accent=tuple(max(0,c-12) for c in color)+(255,)
            for i in range(3):
                x=width*(.12+.26*i);y=height*(.25+.12*(i%2))
                draw.arc((x,y,x+width*.26,y+height*.24),15,235,fill=accent,width=1)
    marks=material.get('marks','')
    if marks not in ('','joinery','knapped','rivet','forge','inlay','grain_knots','planks'):
        raise ValueError(f'Unknown surface marks: {marks}')
    # Construction marks belong to broad working faces, not every side/bevel.
    if min(width,height)>=10 and name.endswith(('.front','.back')):
        dark=tuple(max(0,c-25) for c in color)+(255,)
        light=tuple(min(255,c+24) for c in color)+(255,)
        line=max(1,round(min(width,height)/70))
        if marks=='forge':
            for i in range(3):
                x=width*(.22+i*.13);y=height*(.40+i*.06)
                draw.line((x,y,x+width*.13,y-height*.05),fill=dark,width=line)
            draw.line((width*.62,height*.68,width*.67,height*.58,width*.72,height*.68),fill=light,width=line)
        elif marks=='inlay':
            draw.line((width*.34,height*.24,width*.45,height*.35,width*.34,height*.46,width*.23,height*.35,width*.34,height*.24),fill=dark,width=line)
        elif marks=='knapped':
            for i in range(3):
                x=width*(.22+i*.22);y=height*(.25+(i%2)*.25)
                draw.line((x,y,x+width*.09,y+height*.05,x+width*.04,y+height*.13),fill=dark,width=line)
        elif marks=='rivet':
            draw.line((width*.2,height*.32,width*.6,height*.34),fill=dark,width=line)
        elif marks=='joinery':
            draw.line((width*.30,height*.16,width*.33,height*.83),fill=dark,width=line)
            draw.line((width*.34,height*.16,width*.37,height*.83),fill=light,width=line)
        elif marks=='planks':
            for fraction in (.20,.40,.60,.80):
                y=height*fraction
                draw.line((0,y,width,y),fill=dark,width=line)
                draw.line((0,y+line,width,y+line),fill=light,width=line)
    if style=='water':
        accent=tuple(min(255,c+18) for c in color)+(255,)
        for shift in (.15,.48):
            draw.arc((width*.12,height*shift,width*.82,height*(shift+.25)),15,150,fill=accent,width=max(1,width//50))
    if material.get('seam'):
        for y in range(3,height-3,5):
            draw.line((2,y,3,y+2),fill=(184,150,105,255))
    return image


def bake(islands, materials, size, destination: Path, *, overlays: Path | None = None, gutter=4):
    atlas=Image.new('RGBA',(size,size),(0,0,0,0))
    for island in islands:
        x,y,w,h=island.rect
        if island.role not in materials: raise ValueError(f'Undefined material: {island.role}')
        tile=paint_surface(w,h,materials[island.role],island.name)
        if overlays:
            source=(overlays/(island.name+'.png')).resolve()
            if not source.is_relative_to(overlays.resolve()): raise ValueError('Overlay escapes source directory')
            if source.is_file():
                with Image.open(source) as authored:
                    if authored.size!=(w,h): raise ValueError(f'Overlay size mismatch: {island.name}, expected {w}x{h}')
                    tile=Image.alpha_composite(tile,authored.convert('RGBA'))
        # Dilate colour into the complete gutter including corners for mipmaps.
        padded=Image.new('RGBA',(w+2*gutter,h+2*gutter))
        padded.paste(tile,(gutter,gutter))
        padded.paste(tile.crop((0,0,1,h)).resize((gutter,h)),(0,gutter))
        padded.paste(tile.crop((w-1,0,w,h)).resize((gutter,h)),(gutter+w,gutter))
        padded.paste(padded.crop((0,gutter,w+2*gutter,gutter+1)).resize((w+2*gutter,gutter)),(0,0))
        padded.paste(padded.crop((0,gutter+h-1,w+2*gutter,gutter+h)).resize((w+2*gutter,gutter)),(0,gutter+h))
        atlas.paste(padded,(x-gutter,y-gutter))
    destination.parent.mkdir(parents=True,exist_ok=True)
    atlas.save(destination)
