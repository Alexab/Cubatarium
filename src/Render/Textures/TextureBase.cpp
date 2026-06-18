#include "Render/Textures/TextureBase.h"
#include <filesystem>
#include <iostream>

namespace cutum
{

namespace fs = std::filesystem;

UTextureBase::UTextureBase() {}

UTextureBase::UTextureBase(const std::string &Name, const std::string &file_name)
    : Name(Name), FileName(file_name)
{
}

UTextureBase::UTextureBase(const std::string &Name, TexturePixelData pixels)
    : Name(Name), Pixels(std::move(pixels))
{
}

const std::string &UTextureBase::GetName() const { return Name; }

const std::string &UTextureBase::GetFileName() const { return FileName; }

bool UTextureBase::HasPixelData() const { return Pixels.has_value(); }

const TexturePixelData *UTextureBase::GetPixelData() const
{
  return Pixels ? &*Pixels : nullptr;
}

void UTextureBaseStorage::Load(const std::string &textures_path)
{
  try
  {
    for (const auto &entry : fs::directory_iterator(textures_path))
    {
      auto stem = entry.path().stem();
      auto ext = entry.path().extension();
      if (ext.string() == ".png")
      {
        UTextureBase descr(stem.string(), entry.path().string());
        BaseTextures[stem.string()] = descr;
      }
    }
  }
  catch (std::filesystem::filesystem_error &ex)
  {
    std::cerr << ex.what();
  }
}

void UTextureBaseStorage::Clear() { BaseTextures.clear(); }

void UTextureBaseStorage::Register(const std::string &stem,
                                   const std::string &file_path)
{
  BaseTextures[stem] = UTextureBase(stem, file_path);
}

void UTextureBaseStorage::RegisterPixels(const std::string &stem,
                                         TexturePixelData pixels)
{
  BaseTextures[stem] = UTextureBase(stem, std::move(pixels));
}

const std::map<std::string, UTextureBase> &
UTextureBaseStorage::GetBaseTextures() const
{
  return BaseTextures;
}

} // namespace cutum
