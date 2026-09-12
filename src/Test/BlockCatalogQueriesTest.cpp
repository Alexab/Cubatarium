#include "Blocks/BlockCatalogQueries.h"

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

int main()
{
  auto catalog = std::make_shared<cutum::BlockDefinitionCatalog>();
  cutum::BlockDefinition glass{};
  glass.Render.Transparent = true;
  glass.Render.Style = cutum::BlockRenderStyle::Cutout;
  glass.Lighting.Emission = 0;
  catalog->ById[10] = glass;

  cutum::BlockDefinition torch{};
  torch.Render.Transparent = false;
  torch.Render.Style = cutum::BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  catalog->ById[20] = torch;

  Expect(cutum::CatalogIsTransparent(catalog.get(), cutum::BLOCK_AIR),
         "air transparent");
  Expect(cutum::CatalogIsTransparent(catalog.get(), 10), "glass transparent");
  Expect(!cutum::CatalogIsTransparent(catalog.get(), 20), "torch opaque");
  Expect(cutum::CatalogGetRenderStyle(catalog.get(), 10) ==
             cutum::BlockRenderStyle::Cutout,
         "glass cutout");
  Expect(cutum::CatalogGetLightEmission(catalog.get(), 20) == 14,
         "torch emission");

  // Pin identity: reload replaces Active elsewhere; pinned shared_ptr stays.
  auto pinned = catalog;
  auto replaced = std::make_shared<cutum::BlockDefinitionCatalog>();
  Expect(pinned.get() != replaced.get(), "pin identity distinct after replace");
  Expect(cutum::CatalogIsTransparent(pinned.get(), 10),
         "pinned catalog still transparent after conceptual reload");

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "BlockCatalogQueriesTest: PASS\n";
  return 0;
}
