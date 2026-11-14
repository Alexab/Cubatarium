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
    if style not in ('plain','wood','leather','forged','hammered','stone','food','fiber','water'):
        raise ValueError(f'Unknown surface pattern: {style}')
    phase=int.from_bytes(hashlib.sha256(name.encode()).digest()[:2],'little')/1000
    image=Image.new('RGBA',(width,height))
    for y in range(height):
        for x in range(width):
            u,v=x/max(width-1,1),y/max(height-1,1)
            # Broad variation, then clustered manufacturing marks; no white noise.
            shade=3*math.sin(u*4+phase)+2*math.sin(v*3-phase)
            if style=='wood':
                grain=math.sin(u*35+math.sin(v*7+phase)*1.8)
                shade+=-10*max(0,grain)**8+4*math.sin(u*12+v)
            elif style=='leather':
                shade+=2*math.sin(x*.7)*math.sin(y*.9)
            elif style in ('forged','hammered'):
                shade+=4*math.sin(u*20+phase)*math.sin(v*16)
                if style=='hammered': shade+=3*math.sin(x*.65+y*.35)
            elif style=='stone':
                shade+=5*math.sin(u*17+v*11)+3*math.cos(v*24-u*6)
            elif style=='food':
                shade+=7*math.sin(u*6+phase)*math.sin(v*4)+2*math.sin(x*.8+y)
            elif style=='water':
                shade+=2*math.sin(u*8+v*3)
            elif style=='fiber':
                shade+=5*math.sin(u*30+v*12)
            pixel=tuple(max(0,min(255,round(c+shade))) for c in color)
            image.putpixel((x,y),pixel+(255,))
    draw=ImageDraw.Draw(image)
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
