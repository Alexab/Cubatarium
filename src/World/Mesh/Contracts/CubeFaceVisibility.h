#pragma once
#include "Blocks/BlockCatalogQueries.h"
#include "Blocks/BlockRegistry.h"
#include <cstdint>
#include <string>

namespace cutum
{
// Full material semantics for non-fluid cube/card-face extraction. Geometry
// availability is independent of AIR and material transparency.
static_assert(sizeof(BlockId) <= sizeof(uint16_t),
              "visibility words carry16bit block IDs");
inline constexpr int kFaceGeometryHalo = 1;
inline constexpr uint32_t kFaceId = 0xffffu;
inline constexpr uint32_t kFaceKnown = 1u << 16;
inline constexpr uint32_t kFaceTransparent = 1u << 17;
inline constexpr uint32_t kFaceSolid = 1u << 18;
inline constexpr uint32_t kFaceCross = 1u << 19;
inline constexpr uint32_t kFaceCutout = 1u << 20;
inline constexpr uint32_t kFaceFluid = 1u << 21;
inline constexpr uint32_t kFaceEligible = 1u << 22;

enum class CubeFaceDecision : uint32_t
{
  Hidden,
  Visible
};
inline CubeFaceDecision DecideCubeFace(uint32_t self, uint32_t neighbor)
{
  if (!(neighbor & kFaceKnown))
    return CubeFaceDecision::Hidden;
  if ((neighbor & kFaceCross) || !(neighbor & kFaceId))
    return CubeFaceDecision::Visible;
  if ((self & kFaceCutout) && (neighbor & kFaceCutout))
    return CubeFaceDecision::Visible;
  if ((self & kFaceId) == (neighbor & kFaceId) && !(self & kFaceSolid))
    return CubeFaceDecision::Hidden;
  if ((self & kFaceTransparent) && (neighbor & kFaceTransparent))
    return CubeFaceDecision::Hidden;
  // A transparent neighbor cannot occlude an opaque surface, regardless of
  // collision solidity or geometry beyond that cell.
  if (!(self & kFaceTransparent) &&
      (neighbor & (kFaceTransparent | kFaceFluid)))
    return CubeFaceDecision::Visible;
  return neighbor & kFaceSolid ? CubeFaceDecision::Hidden
                               : CubeFaceDecision::Visible;
}
inline uint32_t PackCubeFaceMaterialWord(BlockId id, BlockRenderStyle style,
                                         bool solid, bool liquid,
                                         bool transparent)
{
  uint32_t word = uint32_t(id) | kFaceKnown;
  if (id == BLOCK_AIR)
    return word;
  if (transparent)
    word |= kFaceTransparent;
  if (solid)
    word |= kFaceSolid;
  if (style == BlockRenderStyle::Cross)
    word |= kFaceCross;
  if (style == BlockRenderStyle::Cutout)
    word |= kFaceCutout;
  if (style == BlockRenderStyle::Fluid)
    word |= kFaceFluid;
  if (solid && !liquid && style != BlockRenderStyle::Cross &&
      style != BlockRenderStyle::Fluid)
    word |= kFaceEligible;
  return word;
}
inline uint32_t CubeFaceMaterialWord(BlockId id,
                                     const BlockDefinitionCatalog *catalog)
{
  if (id == BLOCK_AIR)
    return kFaceKnown;
  if (const auto *definition = FindCatalogDefinition(catalog, id))
    return PackCubeFaceMaterialWord(
        id, definition->Render.Style,
        definition->Physics.Movement.Occupancy >= 1.0f,
        definition->Physics.IsLiquid, definition->Render.Transparent);
  return PackCubeFaceMaterialWord(id, BlockRenderStyle::UCube, true, false,
                                  false);
}
inline uint32_t CubeFaceMaterialWord(BlockId id, UBlockRegistry &registry,
                                     const BlockDefinitionCatalog *catalog)
{
  if (catalog)
    return CubeFaceMaterialWord(id, catalog);
  return PackCubeFaceMaterialWord(
      id, registry.GetRenderStyle(id), registry.BlocksMovement(id),
      registry.IsLiquid(id), registry.IsTransparent(id));
}

// Flags come from the same numeric contract. Control flow is separately
// executed by the actual compute driver and checked against frozen CPU rules.
inline std::string CubeFaceVisibilityGlsl()
{
  std::string code;
  const auto define = [&](const char *name, uint32_t value)
  {
    code += std::string("const uint ") + name + " = " + std::to_string(value) +
            "u;\n";
  };
  define("FACE_HALO", kFaceGeometryHalo);
  define("FACE_ID", kFaceId);
  define("FACE_KNOWN", kFaceKnown);
  define("FACE_TRANSPARENT", kFaceTransparent);
  define("FACE_SOLID", kFaceSolid);
  define("FACE_CROSS", kFaceCross);
  define("FACE_CUTOUT", kFaceCutout);
  define("FACE_FLUID", kFaceFluid);
  define("FACE_ELIGIBLE", kFaceEligible);
  code += R"(
uint cubeFaceDecision(uint self, uint neighbor) {
  if ((neighbor & FACE_KNOWN) == 0u) return 0u;
  if ((neighbor & FACE_CROSS) != 0u || (neighbor & FACE_ID) == 0u) return 1u;
  if ((self & FACE_CUTOUT) != 0u && (neighbor & FACE_CUTOUT) != 0u) return 1u;
  if ((self & FACE_ID) == (neighbor & FACE_ID) && (self & FACE_SOLID) == 0u) return 0u;
  if ((self & FACE_TRANSPARENT) != 0u && (neighbor & FACE_TRANSPARENT) != 0u) return 0u;
  if ((self & FACE_TRANSPARENT) == 0u && (neighbor & (FACE_TRANSPARENT | FACE_FLUID)) != 0u) return 1u;
  return (neighbor & FACE_SOLID) != 0u ? 0u : 1u;
}
)";
  return code;
}
} // namespace cutum
