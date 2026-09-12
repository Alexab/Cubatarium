#include "Blocks/BlockCatalogQueries.h"
#include "World/Chunks/ChunkInputStamp.h"
#include "World/Streaming/MeshInputs.h"

#include <iostream>
#include <memory>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

/// Q4: MeshInputs geom-only + catalog pin identity across conceptual reload.
int main()
{
  using cutum::BlockDefinitionCatalog;
  using cutum::CatalogIsTransparent;
  using cutum::ChunkInputStamp;
  using cutum::MeshInputsFromSnapshotStamps;

  auto catalog_a = std::make_shared<BlockDefinitionCatalog>();
  cutum::BlockDefinition glass{};
  glass.Render.Transparent = true;
  catalog_a->ById[10] = glass;
  const uint64_t rev_a = 7;

  std::array<ChunkInputStamp, 7> stamps{};
  stamps[0].content = 3;
  stamps[0].light = 5;
  stamps[0].incarnation = 1;
  const auto inputs = MeshInputsFromSnapshotStamps(stamps, true, rev_a);
  Expect(inputs.content_revision == 3, "content from center stamp");
  Expect(inputs.light_revision == 5, "light from center stamp");
  Expect(inputs.material_catalog_revision == rev_a, "catalog revision pinned");
  Expect(inputs.stamps_valid, "stamps valid");

  // Reload replaces Active catalog; MeshInputs keeps the submit-time revision.
  auto catalog_b = std::make_shared<BlockDefinitionCatalog>();
  const uint64_t rev_b = 8;
  Expect(rev_a != rev_b, "reload bumps catalog revision");
  Expect(inputs.material_catalog_revision == rev_a,
         "MeshInputs keeps pre-reload catalog revision");
  Expect(CatalogIsTransparent(catalog_a.get(), 10),
         "pinned catalog still answers after reload");
  Expect(!CatalogIsTransparent(catalog_b.get(), 10),
         "new catalog lacks glass until filled");
  Expect(inputs.material_catalog_revision != rev_b,
         "apply would see catalog revision mismatch vs reload");

  // Q4 mesher face-policy helpers: style/transparent from pinned catalog.
  cutum::BlockDefinition cutout{};
  cutout.Render.Style = cutum::BlockRenderStyle::Cutout;
  cutout.Render.Transparent = true;
  catalog_a->ById[11] = cutout;
  Expect(cutum::CatalogGetRenderStyle(catalog_a.get(), 11) ==
             cutum::BlockRenderStyle::Cutout,
         "pinned catalog RenderStyle for mesher faces");
  Expect(CatalogIsTransparent(catalog_a.get(), 11),
         "pinned catalog Transparent for mesher faces");

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "MeshInputsCatalogTest: PASS\n";
  return 0;
}
