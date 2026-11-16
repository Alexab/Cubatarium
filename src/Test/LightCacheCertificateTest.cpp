#include "World/Lighting/LightCacheCertificate.h"
#include <cstdlib>
#include <iostream>

static void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "light_cache_certificate_test: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

int main()
{
  using namespace cutum;
  BlockDefinitionCatalog catalog;
  BlockDefinition stone, water;
  stone.Name = "stone";
  water.Name = "water";
  water.Physics.IsLiquid = true;
  water.Render.Style = BlockRenderStyle::Fluid;
  catalog.ById.emplace(8, stone);
  catalog.ById.emplace(9, water);
  const WorldVerticalProfile profile;
  const auto signature = MakeLightCacheSignature(profile, catalog, 1);
  BlockDefinitionCatalog reordered;
  reordered.ById.emplace(9, water);
  reordered.ById.emplace(8, stone);
  Expect(MakeLightCacheSignature(profile, reordered, 1) == signature,
         "catalog iteration order cannot change a persisted signature");
  const auto changed = [&](const BlockDefinitionCatalog &other)
  { return MakeLightCacheSignature(profile, other, 1) != signature; };
  auto modified = catalog;
  modified.ById.at(8).Lighting.Emission = 14;
  Expect(changed(modified), "emission changes invalidate cached completion");
  modified = catalog;
  modified.ById.at(9).Physics.IsLiquid = false;
  Expect(changed(modified), "liquid attenuation changes invalidate completion");
  modified = catalog;
  modified.ById.at(8).Physics.Movement.Occupancy = 0.0f;
  Expect(changed(modified), "light transmission changes invalidate completion");
  modified = catalog;
  modified.ById.at(8).Render.Style = BlockRenderStyle::Cross;
  Expect(changed(modified), "decor transmission changes invalidate completion");
  modified = catalog;
  modified.ById.at(8).DisplayName = "renamed display label";
  Expect(!changed(modified), "display-only edits preserve compatible light");
  Expect(MakeLightCacheSignature(WorldVerticalProfile(-128, 511), catalog, 1) !=
             signature,
         "height expansion cannot reuse an older sky solution");
  Expect(MakeLightCacheSignature(profile, catalog, 0) != signature,
         "flat and full lighting are distinct cache domains");
  Expect(
      MakeLightCacheSignature(WorldVerticalProfile(1, 0), catalog, 1).empty(),
      "invalid profile has no reusable cache signature");

  const auto encoded =
      EncodeColumnLightCache({{1, -2}, {-3, 4}, {1, -2}}, signature);
  const auto previous_signature =
      "light-v3" + signature.substr(signature.find(':'));
  Expect(!DecodeColumnLightCache(
             EncodeColumnLightCache({{1, -2}}, previous_signature), signature),
         "fields computed with the old section-seed bypass require relighting");
  const auto decoded = DecodeColumnLightCache(encoded, signature);
  Expect(
      decoded && decoded->size() == 2 && decoded->front() == glm::ivec2(-3, 4),
      "completion metadata round-trips signed coordinates deterministically");
  Expect(!DecodeColumnLightCache(encoded, "different_solver_signature"),
         "incompatible solver/material signature is rejected");
  Expect(!DecodeColumnLightCache(encoded, ""),
         "unknown runtime signature never grants completion");
  auto damaged = encoded;
  damaged["format_version"] = 1;
  Expect(!DecodeColumnLightCache(damaged, signature),
         "legacy unsigned completion flags require a relight");
  damaged["format_version"] = 2.0;
  Expect(!DecodeColumnLightCache(damaged, signature),
         "schema version must be an integer");
  damaged = encoded;
  damaged["complete"].push_back({uint64_t{1} << 50, 0});
  Expect(!DecodeColumnLightCache(damaged, signature),
         "invalid coordinates cannot narrow or partially load");
  damaged = encoded;
  damaged["complete"].push_back({true, 0});
  Expect(!DecodeColumnLightCache(damaged, signature),
         "boolean coordinates are invalid");
  damaged = encoded;
  damaged.erase("light_signature");
  Expect(!DecodeColumnLightCache(damaged, signature),
         "missing compatibility proof is not completion");
  return EXIT_SUCCESS;
}
