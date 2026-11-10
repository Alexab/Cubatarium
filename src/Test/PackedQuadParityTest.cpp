// Parity of the packed vertex-pulling shader's quad triangulation with the CPU
// emitter. A wrong corner table leaves a triangular hole in every GPU-meshed
// quad (shader had triangles (0,1,2),(1,2,3) instead of (0,1,2),(0,2,3)).
#include "Render/Mesh/GreedyMeshEmitter.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace cutum
{
void CubatariumLogInfo(const char *, const std::string &) {}
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "packed_quad_parity_test: " << message << std::endl;
    std::exit(1);
  }
}

using Tri = std::array<uint32_t, 3>;

static std::set<Tri> Canonical(const std::vector<Tri> &tris)
{
  std::set<Tri> out;
  for (Tri t : tris)
  {
    std::sort(t.begin(), t.end());
    out.insert(t);
  }
  return out;
}

static std::vector<int> ParseShaderCornerTable()
{
  std::ifstream in(CUBATARIUM_PACKED_SHADER_PATH);
  Expect(in.good(), "cannot open vshader_packed_greedy.glsl");
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string src = ss.str();
  const std::string key = "kCornerIdx[6] = int[6](";
  const size_t pos = src.find(key);
  Expect(pos != std::string::npos, "kCornerIdx table not found in shader");
  const size_t end = src.find(')', pos + key.size());
  std::string body = src.substr(pos + key.size(), end - pos - key.size());
  std::replace(body.begin(), body.end(), ',', ' ');
  std::istringstream nums(body);
  std::vector<int> table;
  int v;
  while (nums >> v)
  {
    table.push_back(v);
  }
  return table;
}

int main()
{
  const std::vector<int> table = ParseShaderCornerTable();
  Expect(table.size() == 6, "corner table must have 6 entries");
  for (int v : table)
  {
    Expect(v >= 0 && v <= 3, "corner index out of range");
  }
  const std::vector<Tri> shader_tris = {
      {(uint32_t)table[0], (uint32_t)table[1], (uint32_t)table[2]},
      {(uint32_t)table[3], (uint32_t)table[4], (uint32_t)table[5]}};

  // Every axis/sign: CPU emitter's vertex indices relative to quad base.
  for (int axis = 0; axis < 3; ++axis)
  {
    for (int sign = -1; sign <= 1; sign += 2)
    {
      cutum::GreedyQuad q;
      q.axis = axis;
      q.slice = 3;
      q.u = 1;
      q.v = 2;
      q.width = 4;
      q.height = 5;
      q.Id = 1;
      q.faceSign = sign;
      std::vector<cutum::GreedyMeshVertex> verts;
      std::vector<uint32_t> idx;
      cutum::AppendGreedyQuad(q, glm::ivec3(0), verts, idx);
      Expect(verts.size() == 4 && idx.size() == 6, "emitter quad shape");
      std::vector<Tri> cpu_tris = {{idx[0], idx[1], idx[2]},
                                   {idx[3], idx[4], idx[5]}};
      Expect(Canonical(cpu_tris) == Canonical(shader_tris),
             "packed shader triangulation differs from CPU emitter");
    }
  }

  // The two triangles must together reference all four corners.
  std::set<int> used(table.begin(), table.end());
  Expect(used.size() == 4, "shader triangles must cover all 4 quad corners");

  std::cout << "packed_quad_parity_test OK" << std::endl;
  return 0;
}
