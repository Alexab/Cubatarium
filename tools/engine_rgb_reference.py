"""Independent normal-color floor reference for a constant-texture lit cave.

Expected light comes from the declared source geometry, never observed vertex
or field values. Runtime atlas/catalog witnesses bind the authored texture to
the sampled frame. This certifies scoped world-buffer RGB, not a whole screen.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

from engine_fixture_reference import near_cave_surface
from engine_floor_light_reference import expected_floor_light

COLORS = {"stone": [192, 128, 64, 255], "glowstone": [96, 192, 224, 255]}


def constant_png(rgba):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind+payload))
    rows = (b"\0" + bytes(rgba)*16)*16
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB",16,16,8,6,0,0,0)) +
            chunk(b"IDAT",zlib.compress(rows)) + chunk(b"IEND",b""))


def prepare(scene: dict, world: Path) -> dict:
    """Add a unique, world-selected secondary pack; retain it for reproduction."""
    world = world.resolve()
    if world.parent.name != "worlds" or not world.name.startswith("EngineSmoke_"):
        raise ValueError("RGB fixture requires an isolated world")
    if scene.get("lighting_fixture",{}).get("schema") != "engine-positive-blocklight-fixture.v1":
        raise ValueError("RGB fixture requires the independently defined lit cave")
    packs = world.parent.parent / "resource_packs"
    pack_id = "EngineSmoke_rgb_" + world.name.removeprefix("EngineSmoke_")
    overlay = packs / pack_id
    overlay.mkdir(exist_ok=False)
    (overlay / "blocks").mkdir()
    (overlay / "textures/blocks").mkdir(parents=True)
    manifest = dict(id=pack_id,name="Isolated constant-color RGB reference",version=1,
                    pack_format=1,resolution=16,worldgen_role="secondary",merge_mode="override",priority=50)
    (overlay / "pack.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    # Overrides from the primary pack are applied after block definitions.
    # Supply the secondary pack's own final six-face override as well.
    overrides={name:[dict(stem="reference_"+name,
                          faces=["front","right","back","left","top","bottom"])]
               for name in COLORS}
    (overlay / "texture_overrides.json").write_text(json.dumps(overrides,indent=2)+"\n",encoding="utf-8")
    materials=[]
    source_hashes={}
    for name, identifier, emission in (("stone",560,0),("glowstone",376,15)):
        original = packs / "cubatarium_worldgen_cc0/blocks" / (name+".json")
        payload = original.read_bytes()
        definition = json.loads(payload)
        if (definition.get("name") != name or definition.get("id") != identifier or
                definition.get("lighting",{}).get("emission",0) != emission or
                definition.get("render",{}).get("style","cube") != "cube" or
                definition.get("render",{}).get("transparent",False)):
            raise ValueError("RGB material optical/geometry contract changed")
        source_hashes[str(original)] = hashlib.sha256(payload).hexdigest()
        definition.setdefault("render",{})["texture_variation"] = 0.0
        definition["textures"] = ["reference_"+name]*6
        (overlay / "blocks" / (name+".json")).write_text(json.dumps(definition,indent=2)+"\n",encoding="utf-8")
        (overlay / "textures/blocks" / ("reference_"+name+".png")).write_bytes(constant_png(COLORS[name]))
        materials.append(dict(name=name,id=identifier,emission=emission,rgba=COLORS[name],
                              stem=pack_id+"/reference_"+name))
    metadata_path = world / "world_data.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    selection = metadata["resource_packs"]
    if selection.get("primary") != ["cubatarium_worldgen_cc0"] or selection.get("secondary",[]):
        raise ValueError("RGB fixture requires the unchanged worldgen primary")
    selection["secondary"] = [pack_id]
    metadata_path.write_text(json.dumps(metadata,indent=2)+"\n",encoding="utf-8")
    scene["rgb_fixture"] = dict(schema="engine-constant-floor-rgb-fixture.v1",pack_id=pack_id,
        pack_path=str(overlay),materials=materials,source_material_sha256=source_hashes,
        files_sha256={p.relative_to(overlay).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in overlay.rglob("*") if p.is_file()},
        scope="constant fully opaque textures, unchanged optics and ordinary color shader; independent dry floor blocklight")
    return scene


def number(value):
    return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value)


def unit(value):
    if not number(value) or not 0 <= value <= 1:
        raise ValueError("invalid normalized shader input")
    return value


def clamp(value, low=0.0, high=1.0):
    return min(high,max(low,value))


def shade_dry_floor(row: dict, rgba: list, light_level: int, distance: float, horizontal_distance: float):
    """Normal linear-color contract: opaque texel, sky0, independent block level.

    No shader source is parsed and no measured light or RGB enters prediction.
    Wet/underwater scenes require another independent model and cannot pass here.
    """
    prefix = "renderer_pixel_"
    minimum = unit(row[prefix+"shader_min_ambient"])
    day = unit(row[prefix+"shader_day_factor"])
    night = unit(row[prefix+"shader_night_factor"])
    sky_scale = unit(row[prefix+"shader_sky_scale"])
    precipitation = unit(row[prefix+"shader_precipitation"])
    if unit(row[prefix+"shader_wetness"]) != 0 or row[prefix+"shader_light_debug_mode"] != 0:
        raise ValueError("unsupported wet or debug color")
    daylight = clamp(day*sky_scale)
    moon = clamp(night*(1-daylight))
    ambient_day = minimum*(.42+.38*daylight)
    ambient_night = minimum*(.18+.32*moon)
    ambient = ambient_night*(1-daylight)+ambient_day*daylight
    block_floor = clamp(minimum*.04,.005,.1)
    source_strength = .9*(1-daylight)+.35*daylight
    intensity = clamp(max(ambient, block_floor+(source_strength-block_floor)*light_level/15))
    color = [v/255*intensity for v in rgba[:3]]
    if precipitation > .001:
        gray = sum(a*b for a,b in zip(color,[.299,.587,.114]))
        color = [c*(1-precipitation*.12)+g*precipitation*.12 for c,g in zip(color,[gray,gray,gray*1.05])]
        color = [c*(1-precipitation*.04)+g*precipitation*.04 for c,g in zip(color,[.72,.78,.88])]
    flags = [row[prefix+k] for k in ("fog_state_valid","fog_enabled","air_fog_enabled","fog_horizontal",
                                    "underwater_fog_enabled","underwater_fog_submerged")]
    if flags[0] != 1 or any(type(v) is not int or v not in (0,1) for v in flags):
        raise ValueError("missing fog state")
    if flags[-1] != 0:
        raise ValueError("underwater color requires separate reference")
    fog_factor=0.0
    if flags[1] or flags[2]:
        fog_start,fog_end,density,multiplier = [row[prefix+k] for k in
            ("fog_start","fog_end","fog_density","fog_env_multiplier")]
        if not all(number(v) for v in (fog_start,fog_end,density,multiplier)) or fog_end <= fog_start:
            raise ValueError("invalid distance fog parameters")
        d=horizontal_distance if not flags[1] and flags[3] else distance
        fog_factor=max(clamp((d-fog_start)/max(fog_end-fog_start,.001))**max(density*max(multiplier,.05),.1),
                       unit(row[prefix+"fog_min_blend"]))
        fog_color=[unit(row[prefix+"fog_color_"+c]) for c in "rgb"]
        color=[v*(1-fog_factor)+f*fog_factor for v,f in zip(color,fog_color)]
    return [clamp(v)*255 for v in color]+[255.0], intensity, fog_factor


class RgbContractError(ValueError):
    pass


def material_contract(scene: dict, logs: list[Path]):
    """Validate authored constant materials and index actual per-frame atlas witnesses.

    Shared by floor and hole geometry references; measured RGB/light never
    participates in selecting a material or its expected texel.
    """
    fixture=scene['rgb_fixture']
    if fixture.get('schema')!='engine-constant-floor-rgb-fixture.v1':
        raise ValueError('unsupported RGB fixture')
    materials={m['id']:m for m in fixture['materials']}
    if (len(fixture['materials'])!=2 or set(materials)!={560,376} or
            scene['name']!='dry-negative-cave' or scene.get('expected_eye_in_fluid') is not False):
        raise ValueError('unsupported RGB material/scene contract')
    for identifier,name,emission in ((560,'stone',0),(376,'glowstone',15)):
        material=materials[identifier]
        if (material['name']!=name or material['emission']!=emission or material['rgba']!=COLORS[name] or
                material['stem']!=fixture['pack_id']+'/reference_'+name):
            raise ValueError('RGB fixture material contract changed')
    pack=Path(fixture['pack_path'])
    files=fixture['files_sha256']
    if not files or any(hashlib.sha256((pack/relative).read_bytes()).hexdigest()!=digest
                        for relative,digest in files.items()):
        raise RgbContractError('rgb_fixture_pack_changed')
    sources=fixture['source_material_sha256']
    if not sources or any(hashlib.sha256(Path(path).read_bytes()).hexdigest()!=digest
                          for path,digest in sources.items()):
        raise RgbContractError('original_rgb_material_changed')
    witnesses={}
    for path in logs:
        for line in path.read_text(encoding='utf-8',errors='replace').splitlines():
            marker='[RgbReferenceMaterial] '
            if marker not in line: continue
            record=json.loads(line.split(marker,1)[1])
            key=(record['frame_epoch'],record['material_id'])
            if key in witnesses:
                raise ValueError('duplicate runtime RGB material witness')
            witnesses[key]=record
    return materials,witnesses


def valid_material_witness(witness, material):
    return (witness.get('schema')=='rgb-reference-material.v1' and
        witness.get('valid') is True and witness.get('rgba_min')==material['rgba']==witness.get('rgba_max') and
        witness.get('variation')==0 and witness.get('emission')==material['emission'] and
        witness.get('opaque_cube') is True and witness.get('frames')==1 and
        witness.get('stems')==[material['stem']]*6 and witness.get('linear_texture') is True and
        witness.get('framebuffer_srgb') is False)


def evaluate(scene: dict, perf: Path | None, logs: list[Path]) -> dict:
    def result(status,reason,**evidence):
        return dict(schema="engine-floor-rgb-reference.v1",status=status,reason=reason,
            scope="independent positive blocklight + constant opaque material normal-color world framebuffer at sampled floor rays; no whole-screen/wet/transparency/backend acceptance",**evidence)
    fixture=scene.get("rgb_fixture",{})
    if fixture.get("schema") != "engine-constant-floor-rgb-fixture.v1":
        return result("UNTESTED","missing_constant_rgb_fixture")
    if perf is None or not perf.is_file():
        return result("UNTESTED","missing_rgb_framebuffer_trace")
    witnesses={}
    try:
        materials,witnesses=material_contract(scene,logs)
        light=scene["lighting_fixture"]
        floor_y=round(scene["cave_bounds"]["floor"]-.5)
        if (light.get("schema") != "engine-positive-blocklight-fixture.v1" or light.get("emission") != 15 or
                light.get("material_id") != 376 or not light.get("emitters") or
                any(not isinstance(e,list) or len(e)!=3 or any(type(v) is not int for v in e) or e[1]!=floor_y
                    for e in light["emitters"])):
            raise ValueError("invalid independent source contract")
        if not witnesses:
            return result("UNTESTED","missing_runtime_atlas_material_witness")
        checked=faults=positive=skipped=unwitnessed=0
        epochs,cells,levels,draw_sources=set(),set(),set(),set()
        examples=[]
        emitters={tuple(e) for e in scene["lighting_fixture"]["emitters"]}
        for row in json_rows(perf):
            if row.get("kind") != "renderer_pixel_probe": continue
            origin=[row.get("renderer_pixel_ray_origin_"+c) for c in "xyz"]
            direction=[row.get("renderer_pixel_ray_direction_"+c) for c in "xyz"]
            if (not all(number(v) for v in origin+direction) or
                    abs(sum(v*v for v in direction)-1) > .01 or direction[1] >= -1e-6):
                skipped+=1; continue
            depth=near_cave_surface(scene,origin,direction)
            floor_depth=(scene["cave_bounds"]["floor"]-origin[1])/direction[1]
            if depth is None or abs(floor_depth-depth) > .001:
                skipped+=1; continue
            hit=[a+b*depth for a,b in zip(origin,direction)]
            x,z=math.floor(hit[0]+.5),math.floor(hit[2]+.5)
            lo,hi=scene["chunk_x_bounds"]
            if (abs(hit[0]-x)>.45 or abs(hit[2]-z)>.45 or not lo*16+16 <= x < (hi+1)*16-16):
                skipped+=1; continue
            expected=expected_floor_light(scene,x,z)
            if expected <= 0:
                skipped+=1; continue
            material=materials[376 if (x,floor_y,z) in emitters else 560]
            epoch=row.get("frame_epoch")
            witness=witnesses.get((epoch,material["id"]))
            if witness is None:
                unwitnessed+=1; continue
            valid_material=valid_material_witness(witness,material)
            # A scoped sample with a missing/mismatched surface must fail,
            # rather than disappearing from the RGB reference coverage.
            actual_depth=row.get("renderer_pixel_opaque_hit_distance")
            camera=[row.get("renderer_pixel_camera_pos_"+c) for c in "xyz"]
            if not all(number(v) for v in camera): raise ValueError("missing render camera")
            distance=math.dist(camera,hit)
            horizontal=math.hypot(hit[0]-camera[0],hit[2]-camera[2])
            predicted,intensity,fog=shade_dry_floor(row,material["rgba"],expected,distance,horizontal)
            actuals=[]
            for key in ("renderer_pixel_pretransparent_rgba","renderer_pixel_rgba"):
                packed=row.get(key)
                if type(packed) is not int or not 0 <= packed <= 0xffffffff:
                    raise ValueError("invalid framebuffer RGBA")
                actuals.append([(packed>>s)&255 for s in (24,16,8,0)])
            error=max(abs(a-b) for actual in actuals for a,b in zip(actual,predicted))
            surface_ok=(number(actual_depth) and abs(actual_depth-depth) <= .05+depth*.001 and
                        row.get("renderer_pixel_marker_mode") is False and
                        row.get("renderer_pixel_marker_visible") is False and
                        row.get("renderer_pixel_opaque_surface_valid")==1 and
                        row.get("renderer_pixel_opaque_vertex_light_block_id")==material["id"] and
                        row.get("renderer_pixel_opaque_vertex_light_face_index")==4)
            checked+=1; epochs.add(epoch); cells.add((x,z)); levels.add(expected)
            positive+=expected>0; draw_sources.add(row.get("renderer_pixel_opaque_light_source_kind"))
            fault=not valid_material or not surface_ok or error>2.0
            faults+=fault
            if fault and len(examples)<8:
                examples.append(dict(frame_epoch=epoch,cell=[x,floor_y,z],expected_light=expected,
                    predicted_rgba=predicted,actual_rgba=actuals,error_bytes=error,intensity=intensity,
                    fog_factor=fog,valid_material=valid_material,surface_ok=surface_ok))
        coverage=dict(samples=checked>=25,epochs=len(epochs)>=3,cells=len(cells)>=8,
                      levels=len(levels)>=3,all_scoped_materials_witnessed=unwitnessed==0)
        return result("FAIL" if faults else "PASS" if all(coverage.values()) else "UNTESTED",
            "analytic_positive_floor_normal_rgb",checked=checked,faults=faults,frame_epochs=len(epochs),
            distinct_cells=len(cells),expected_levels=sorted(levels),draw_sources=sorted(v for v in draw_sources if v is not None),
            unwitnessed=unwitnessed,skipped=skipped,examples=examples,
            missing_coverage=[k for k,v in coverage.items() if not v])
    except RgbContractError as error:
        return result('FAIL',str(error))
    except (ValueError,KeyError,TypeError,AttributeError,OSError,OverflowError):
        return result("FAIL","malformed_rgb_reference_evidence")


def json_rows(path):
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            if line.strip():
                yield json.loads(line)
