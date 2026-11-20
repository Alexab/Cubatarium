"""Independent declared-voxel model for one removed opaque floor source.

This model consumes no engine light, mesh, depth or RGB observations. It does
not itself grant native acceptance: callers must join its predictions to actual
same-frame evidence. Only the prepared roofed cave and one floor hole are modeled.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import math

from engine_fixture_reference import near_cave_surface


# Public material face order: front(+Z), right, back(-Z), left, top, bottom.
NORMALS = ((0, 0, 1), (1, 0, 0), (0, 0, -1), (-1, 0, 0),
           (0, 1, 0), (0, -1, 0))


def _cell(value):
    return (isinstance(value, (list, tuple)) and len(value) == 3 and
            all(type(v) is int for v in value))


def _vector(value):
    return (isinstance(value, (list, tuple)) and len(value) == 3 and
            all(type(v) in (int, float) and math.isfinite(v) for v in value))


@dataclass(frozen=True)
class Surface:
    distance: float
    solid: tuple[int, int, int]
    face: int
    air: tuple[int, int, int]
    material: int
    hole: bool


class HoleReference:
    def __init__(self, scene, emitters, hole=None):
        bounds = scene['cave_bounds']
        light = scene['lighting_fixture']
        if (scene.get('schema') != 'engine-smoke-fixture.v1' or
                scene.get('name') != 'dry-negative-cave' or
                scene.get('expected_eye_in_fluid') is not False or
                bounds != dict(floor=-48.5, ceiling=-16.5, z_low=34.5, z_high=67.5) or
                scene.get('air_border_fixture', {}).get('schema') !=
                'engine-positive-air-border-fixture.v1' or
                light.get('schema') != 'engine-positive-blocklight-fixture.v1' or
                light.get('material_id') != 376 or light.get('emission') != 15):
            raise ValueError('unsupported roofed source-hole fixture')
        xbounds = scene['chunk_x_bounds']
        if (not isinstance(xbounds, list) or len(xbounds) != 2 or
                any(type(v) is not int for v in xbounds) or xbounds[0] >= xbounds[1]):
            raise ValueError('invalid cave extent')
        self.xmin, self.xmax = xbounds[0]*16, (xbounds[1]+1)*16-1
        if (not isinstance(emitters, (list, tuple)) or not emitters or
                any(not _cell(e) or tuple(e[1:]) != (-49, 51) or
                    not self.xmin <= e[0] <= self.xmax for e in emitters)):
            raise ValueError('invalid declared emitters')
        self.emitters = frozenset(tuple(e) for e in emitters)
        if len(self.emitters) != len(emitters):
            raise ValueError('duplicate source')
        if hole is not None and (not _cell(hole) or tuple(hole[1:]) != (-49, 51) or
                not self.xmin+16 <= hole[0] <= self.xmax-16 or tuple(hole) in self.emitters):
            raise ValueError('hole must be a removed interior source')
        self.hole = tuple(hole) if hole is not None else None
        self.scene = scene
        # Multi-source breadth-first propagation is bounded by emission, and
        # enters only declared AIR. Opaque source cells emit but never transmit.
        self.levels = {e: 15 for e in self.emitters}
        pending = deque(self.emitters)
        while pending:
            cell = pending.popleft()
            level = self.levels[cell]-1
            if level <= 0:
                continue
            for normal in NORMALS:
                neighbor = tuple(a+b for a, b in zip(cell, normal))
                if self.is_air(neighbor) and self.levels.get(neighbor, 0) < level:
                    self.levels[neighbor] = level
                    pending.append(neighbor)

    def is_air(self, cell):
        return (cell == self.hole or
                self.xmin <= cell[0] <= self.xmax and -48 <= cell[1] <= -17 and
                35 <= cell[2] <= 67)

    def block_level(self, cell):
        if not _cell(cell):
            raise ValueError('light query requires integer cell')
        cell = tuple(cell)
        if not self.is_air(cell) and cell not in self.emitters:
            raise ValueError('unmodeled or opaque light query')
        return self.levels.get(cell, 0)

    def surface(self, origin, direction):
        """Nearest ordinary floor or hole face, within 32 blocks.

        Cave walls/roof and voxel edges are explicitly outside this surface
        contract. The former still occlude the floor through near_cave_surface.
        The camera must start in the upper cave, not inside the hole or solid.
        """
        if (not _vector(origin) or not _vector(direction) or direction[1] >= -1e-8 or
                abs(sum(v*v for v in direction)-1) > .0001):
            return None
        depth = near_cave_surface(self.scene, origin, direction)
        floor_depth = (-48.5-origin[1])/direction[1]
        if depth is None or abs(depth-floor_depth) > 1e-7:
            return None
        hit = [a+b*depth for a, b in zip(origin, direction)]
        x, z = math.floor(hit[0]+.5), math.floor(hit[2]+.5)
        if not self.xmin+16 <= x <= self.xmax-16:
            return None
        hole_hit = self.hole is not None and (x, -49, z) == self.hole
        if hole_hit:
            # Enter at the former top face; exit the AIR unit box through its
            # bottom or one of four walls. The normal points toward hole AIR.
            candidates = []
            for axis in range(3):
                component = direction[axis]
                if abs(component) < 1e-10:
                    continue
                sign = 1 if component > 0 else -1
                boundary = self.hole[axis]+sign*.5
                t = (boundary-origin[axis])/component
                if t > depth+1e-8:
                    candidates.append((t, axis, sign))
            depth, axis, sign = min(candidates)
            if depth > 32:
                return None
            hit = [a+b*depth for a, b in zip(origin, direction)]
            solid = list(self.hole)
            solid[axis] += sign
            normal = [0, 0, 0]
            normal[axis] = -sign
            face = NORMALS.index(tuple(normal))
        else:
            solid, axis, face = [x, -49, z], 1, 4
        # Exclude seams on every face, not just X/Z seams of the former floor.
        if any(abs(hit[i]-solid[i]) > .45 for i in range(3) if i != axis):
            return None
        solid = tuple(solid)
        air = tuple(a+b for a, b in zip(solid, NORMALS[face]))
        if not self.is_air(air):
            return None
        return Surface(depth, solid, face, air,
                       376 if solid in self.emitters else 560, hole_hit)
