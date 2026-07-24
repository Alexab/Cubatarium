#include "Render/Mesh/ChunkMeshRevisionRegistry.h"

#include <glm/glm.hpp>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "chunk_mesh_revision_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::UChunkMeshRevisionRegistry revisions;

  const glm::ivec3 chunk_a{1, 0, 2};
  const glm::ivec3 chunk_b{4, 1, 5};

  Expect(revisions.Current(chunk_a) == 0, "unset chunk revision should be 0");
  const uint64_t a1 = revisions.Bump(chunk_a);
  Expect(a1 == 1, "first bump should be 1");
  Expect(revisions.Current(chunk_a) == 1, "current should match last bump");

  const uint64_t b1 = revisions.Bump(chunk_b);
  Expect(b1 == 1, "neighbor bump should be independent");
  Expect(revisions.Current(chunk_a) == 1,
          "bumping neighbor must not change chunk_a revision");

  revisions.Bump(chunk_a);
  Expect(revisions.Current(chunk_a) == 2, "second bump on chunk_a");
  Expect(revisions.Current(chunk_b) == 1, "chunk_b revision unchanged");

  revisions.Erase(chunk_a);
  Expect(revisions.Current(chunk_a) == 0, "erased chunk revision resets to 0");

  std::cout << "chunk_mesh_revision_test: OK" << std::endl;
  return 0;
}
