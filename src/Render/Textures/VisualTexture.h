#pragma once

#include "Render/Textures/GlTextureOwner.h"
#include <cstddef>

namespace cutum
{

struct VisualTextureStorage
{
  size_t inlineBytes{}, controlBytes{}, tableBytes{}, images{}, aliases{}, rgbaBytes{};
  size_t models{}, linearImages{}, linearAliases{};
  bool present{}, supported{};
};

struct VisualTexture
{
  UGlTextureOwner Name{0};
  size_t RgbaBytes{};
};

} // namespace cutum
