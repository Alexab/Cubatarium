#ifndef PHYSICSTESTWORLDFIXTURE_H
#define PHYSICSTESTWORLDFIXTURE_H

#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include <memory>

namespace cutum
{

inline std::shared_ptr<UTextureCubeStorage> MakePhysicsTestTextureStorage()
{
  auto base = std::make_shared<UTextureBaseStorage>();
  return std::make_shared<UTextureCubeStorage>(base);
}

} // namespace cutum

#endif // PHYSICSTESTWORLDFIXTURE_H
