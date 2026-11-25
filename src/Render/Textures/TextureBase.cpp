#include "Render/Textures/TextureBase.h"
#include <filesystem>
#include <iostream>
#include <type_traits>
#include <utility>

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

void UTextureBaseStorage::Swap(UTextureBaseStorage &other) noexcept
{
  static_assert(noexcept(BaseTextures.swap(other.BaseTextures)));
  BaseTextures.swap(other.BaseTextures);
}

void UTextureBaseStorage::MergePrepared(UTextureBaseStorage &&prepared) noexcept
{
  if(this==&prepared)return;
  static_assert(std::is_nothrow_move_assignable_v<UTextureBase>);
  // Both trees have the same default allocator and std::less<std::string>.
  // Merge transfers nodes without allocation; comparison only reads strings.
  BaseTextures.merge(prepared.BaseTextures);
  // Duplicated keys remain in prepared. Preserve the registration rule that
  // the prepared value wins; moving strings/optional pixels cannot allocate.
  for(auto &entry:prepared.BaseTextures)BaseTextures.find(entry.first)->second=std::move(entry.second);
}

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
