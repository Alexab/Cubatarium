"""Minimal Blitz3D (.b3d) reader for Luanti mob UV extraction."""

from __future__ import annotations

import struct
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class B3DVertex:
  x: float
  y: float
  z: float
  u: float
  v: float


@dataclass
class B3DMesh:
  vertices: list[B3DVertex] = field(default_factory=list)
  indices: list[int] = field(default_factory=list)


@dataclass
class B3DKeyframe:
  frame: int
  position: tuple[float, float, float] | None = None
  scale: tuple[float, float, float] | None = None
  rotation: tuple[float, float, float, float] | None = None


@dataclass
class B3DVertexWeight:
  vertex_id: int
  weight: float


@dataclass
class B3DBone:
  name: str
  position: tuple[float, float, float] = (0.0, 0.0, 0.0)
  scale: tuple[float, float, float] = (1.0, 1.0, 1.0)
  rotation: tuple[float, float, float, float] = (1.0, 0.0, 0.0, 0.0)
  mesh: B3DMesh = field(default_factory=B3DMesh)
  vertex_weights: list[B3DVertexWeight] = field(default_factory=list)
  keyframes: list[B3DKeyframe] = field(default_factory=list)
  children: list["B3DBone"] = field(default_factory=list)


class B3DReader:
  def __init__(self, data: bytes) -> None:
    self._data = data
    self._pos = 0
    self._limit = len(data)

  @contextmanager
  def _bounded(self, end: int):
    prev = self._limit
    self._limit = end
    try:
      yield
    finally:
      self._limit = prev

  def _remaining(self) -> bool:
    return self._pos < self._limit

  def _read(self, n: int) -> bytes:
    chunk = self._data[self._pos : self._pos + n]
    if len(chunk) != n:
      raise EOFError("unexpected end of b3d data")
    self._pos += n
    return chunk

  def _i32(self) -> int:
    return struct.unpack("<i", self._read(4))[0]

  def _f32(self) -> float:
    return struct.unpack("<f", self._read(4))[0]

  def _id(self) -> int:
    return self._i32() + 1

  def _optional_id(self) -> int | None:
    value = self._i32()
    return None if value == -1 else value + 1

  def _string(self) -> str:
    out = bytearray()
    while self._remaining():
      b = self._read(1)[0]
      if b == 0:
        break
      out.append(b)
    return out.decode("utf-8", errors="replace")

  def _vec3(self) -> tuple[float, float, float]:
    return self._f32(), self._f32(), self._f32()

  def _quat(self) -> None:
    for _ in range(4):
      self._f32()

  def _read_chunk(self) -> tuple[str, int]:
    tag = self._read(4).decode("ascii", errors="replace")
    length = self._i32()
    return tag, length

  def _skip_chunk(self, length: int) -> None:
    self._pos += length

  def _parse_vrts(self) -> list[B3DVertex]:
    flags = self._i32()
    tex_sets = self._i32()
    tex_size = self._i32()
    has_normal = (flags & 1) != 0
    has_color = (flags & 2) != 0
    verts: list[B3DVertex] = []
    while self._remaining():
      x, y, z = self._vec3()
      if has_normal:
        self._vec3()
      if has_color:
        for _ in range(4):
          self._f32()
      u, v = 0.0, 0.0
      if tex_sets > 0 and tex_size >= 2:
        u = self._f32()
        v = self._f32()
        for _ in range(tex_sets - 1):
          for _ in range(tex_size):
            self._f32()
        if tex_size > 2:
          for _ in range(tex_size - 2):
            self._f32()
      else:
        for _ in range(tex_sets):
          for _ in range(tex_size):
            self._f32()
      verts.append(B3DVertex(x=x, y=y, z=z, u=u, v=v))
    return verts

  def _parse_tris(self) -> list[int]:
    self._id()
    indices: list[int] = []
    while self._remaining():
      i1 = self._id()
      i2 = self._id()
      i3 = self._id()
      indices.extend([i1 - 1, i2 - 1, i3 - 1])
    return indices

  def _parse_mesh(self) -> B3DMesh:
    self._optional_id()
    mesh = B3DMesh()
    while self._remaining():
      tag, length = self._read_chunk()
      start = self._pos
      end = start + length
      with self._bounded(end):
        if tag == "VRTS":
          mesh.vertices.extend(self._parse_vrts())
        elif tag == "TRIS":
          mesh.indices.extend(self._parse_tris())
        else:
          pass
      self._pos = end
    return mesh

  def _parse_keys(self) -> list[B3DKeyframe]:
    flags = self._i32()
    has_rot = (flags % 8) >= 4
    rem = flags % 8 - (4 if has_rot else 0)
    has_scale = rem >= 2
    rem -= 2 if has_scale else 0
    has_pos = rem >= 1
    keyframes: list[B3DKeyframe] = []
    while self._remaining():
      frame = self._i32()
      position = self._vec3() if has_pos else None
      scale = self._vec3() if has_scale else None
      rotation = None
      if has_rot:
        rotation = (
            self._f32(),
            self._f32(),
            self._f32(),
            self._f32(),
        )
      keyframes.append(
          B3DKeyframe(
              frame=frame,
              position=position,
              scale=scale,
              rotation=rotation,
          )
      )
    return keyframes

  def _parse_node(self) -> B3DMesh:
    self._string()
    self._vec3()
    self._vec3()
    self._quat()
    mesh = B3DMesh()
    while self._remaining():
      tag, length = self._read_chunk()
      start = self._pos
      end = start + length
      with self._bounded(end):
        if tag == "MESH":
          sub = self._parse_mesh()
          mesh.vertices.extend(sub.vertices)
        elif tag == "NODE":
          sub = self._parse_node()
          mesh.vertices.extend(sub.vertices)
        elif tag == "KEYS":
          self._parse_keys()
        elif tag == "ANIM":
          self._i32()
          self._i32()
          self._f32()
        elif tag == "BONE":
          pass
        else:
          pass
      self._pos = end
    return mesh

  def _parse_bone_weights(self) -> list[B3DVertexWeight]:
    weights: list[B3DVertexWeight] = []
    while self._remaining():
      vertex_id = self._i32()
      strength = self._f32()
      if strength > 0.0:
        weights.append(B3DVertexWeight(vertex_id=vertex_id, weight=strength))
    return weights

  def _parse_node_pose(self) -> B3DBone:
    name = self._string()
    position = self._vec3()
    scale = self._vec3()
    rotation = (
        self._f32(),
        self._f32(),
        self._f32(),
        self._f32(),
    )
    bone = B3DBone(
        name=name,
        position=position,
        scale=scale,
        rotation=rotation,
    )
    while self._remaining():
      tag, length = self._read_chunk()
      start = self._pos
      end = start + length
      with self._bounded(end):
        if tag == "MESH":
          bone.mesh = self._parse_mesh()
        elif tag == "NODE":
          bone.children.append(self._parse_node_pose())
        elif tag == "KEYS":
          bone.keyframes = self._parse_keys()
        elif tag == "ANIM":
          self._i32()
          self._i32()
          self._f32()
        elif tag == "BONE":
          bone.vertex_weights = self._parse_bone_weights()
        else:
          pass
      self._pos = end
    return bone

  def _parse_bb3d_pose(self) -> list[B3DBone]:
    self._i32()
    roots: list[B3DBone] = []
    while self._remaining():
      tag, length = self._read_chunk()
      start = self._pos
      end = start + length
      with self._bounded(end):
        if tag == "NODE":
          roots.append(self._parse_node_pose())
        elif tag == "TEXS":
          while self._remaining():
            self._string()
            self._i32()
            self._i32()
            self._f32()
            self._f32()
            self._f32()
            self._f32()
        elif tag == "BRUS":
          n_texs = self._i32()
          while self._remaining():
            self._string()
            for _ in range(4):
              self._f32()
            self._f32()
            self._i32()
            self._i32()
            for _ in range(n_texs):
              self._optional_id()
      self._pos = end
    return roots

  def _parse_bb3d(self) -> B3DMesh:
    self._i32()
    mesh = B3DMesh()
    while self._remaining():
      tag, length = self._read_chunk()
      start = self._pos
      end = start + length
      with self._bounded(end):
        if tag == "NODE":
          sub = self._parse_node()
          mesh.vertices.extend(sub.vertices)
        elif tag == "TEXS":
          while self._remaining():
            self._string()
            self._i32()
            self._i32()
            self._f32()
            self._f32()
            self._f32()
            self._f32()
        elif tag == "BRUS":
          n_texs = self._i32()
          while self._remaining():
            self._string()
            for _ in range(4):
              self._f32()
            self._f32()
            self._i32()
            self._i32()
            for _ in range(n_texs):
              self._optional_id()
      self._pos = end
    return mesh

  def read_mesh(self) -> B3DMesh:
    self._pos = 0
    tag, length = self._read_chunk()
    if tag != "BB3D":
      raise ValueError(f"not a BB3D file (got {tag!r})")
    end = self._pos + length
    with self._bounded(end):
      return self._parse_bb3d()

  def read_document(self) -> list[B3DBone]:
    self._pos = 0
    tag, length = self._read_chunk()
    if tag != "BB3D":
      raise ValueError(f"not a BB3D file (got {tag!r})")
    end = self._pos + length
    with self._bounded(end):
      return self._parse_bb3d_pose()


def load_b3d_vertices(path: Path) -> list[B3DVertex]:
  return B3DReader(path.read_bytes()).read_mesh().vertices


def load_b3d_pose(path: Path) -> list[B3DBone]:
  return B3DReader(path.read_bytes()).read_document()


def load_b3d_document(path: Path) -> list[B3DBone]:
  return B3DReader(path.read_bytes()).read_document()


def iter_b3d_bones(roots: list[B3DBone]):
  """Depth-first yield of every bone with keyframes or not."""
  stack = list(roots)
  while stack:
    bone = stack.pop()
    yield bone
    stack.extend(reversed(bone.children))


def find_b3d_bone(roots: list[B3DBone], name: str) -> B3DBone | None:
  for bone in iter_b3d_bones(roots):
    if bone.name == name:
      return bone
  return None
