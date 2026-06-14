#include "TextureBase.h"
#include <filesystem>
#include <iostream>

namespace cutum
{

namespace fs = std::filesystem;

UTextureBase::UTextureBase() {}

UTextureBase::UTextureBase(const std::string &name,
                           const std::string &file_name)
    : Name(name), FileName(file_name)
{
}

const std::string &UTextureBase::GetName() const { return Name; }

const std::string &UTextureBase::GetFileName() const { return FileName; }

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

const std::map<std::string, UTextureBase> &
UTextureBaseStorage::GetBaseTextures() const
{
  return BaseTextures;
}

} // namespace cutum
