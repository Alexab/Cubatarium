"""Validate draw-time GPU field evidence without using it as expected truth."""


def uint(value, bits=64, *, positive=False):
    return type(value) is int and (0 < value if positive else 0 <= value) and value < 1 << bits


def read_bound_field(row):
    evidence=row.get("renderer_pixel_opaque_bound_field")
    if evidence is None:
        return None
    if not isinstance(evidence,dict) or evidence.get("schema")!="bound-section-light-pixel.v1":
        raise ValueError("malformed bound-field witness")
    for key in ("requested","captured","valid","cpu_matches"):
        if type(evidence.get(key)) is not bool:
            raise ValueError("malformed bound-field flag")
    if not evidence["requested"]:
        if evidence["captured"] or evidence["valid"]:
            raise ValueError("unexpected bound-field capture")
        return None
    if not evidence["captured"]:
        return None
    if not evidence["valid"] or not evidence["cpu_matches"]:
        raise ValueError("captured GPU binding/field mismatch")
    for key in ("program","atlas_texture","table_texture"):
        if not uint(evidence.get(key),32,positive=True):
            raise ValueError("missing GPU object identity")
    for key in ("frame_epoch","world_epoch","field_incarnation","geometry_incarnation"):
        if not uint(evidence.get(key),positive=True):
            raise ValueError("missing owner identity")
    for key in ("field_revision","geometry_content"):
        if not uint(evidence.get(key)):
            raise ValueError("invalid owner revision")
    handle=evidence.get("handle")
    if (not isinstance(handle,list) or len(handle)!=4 or any(not uint(v,32) for v in handle) or
            not any(handle[2:]) or not uint(evidence.get("packed"),8)):
        raise ValueError("invalid shader handle or packed light")
    if evidence["world_epoch"] != row.get("renderer_pixel_opaque_world_epoch"):
        raise ValueError("bound field from another world")
    if evidence["frame_epoch"] != row.get("frame_epoch"):
        raise ValueError("bound field from another frame")
    if 'draw_table' in evidence:
        if type(evidence['draw_table']) is not bool:
            raise ValueError('invalid draw-table mode')
        if evidence['draw_table']:
            command=evidence.get('indirect_command')
            geometry=evidence.get('geometry_command')
            if (not uint(evidence.get('draw_table_buffer'),32,positive=True) or
                    not uint(evidence.get('indirect_buffer'),32,positive=True) or
                    not uint(evidence.get('draw_table_row'),32) or
                    not isinstance(command,list) or len(command)!=5 or
                    any(not uint(v,32) for v in command) or command[0]==0 or
                    command[1]!=1 or command[4]!=evidence['draw_table_row'] or
                    not isinstance(geometry,list) or len(geometry)!=3 or
                    any(not uint(v,32) for v in geometry) or
                    [command[0],command[2],command[3]]!=geometry):
                raise ValueError('missing GPU draw-table command identity')
    return evidence
