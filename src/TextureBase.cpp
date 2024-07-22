#include "TextureBase.h"
#include <filesystem>
#include <iostream>

namespace cutum {

namespace fs = std::filesystem;

TextureBase::TextureBase()
{

}

TextureBase::TextureBase(const std::string& name, const std::string& file_name)
 : Name(name)
 , FileName(file_name)
{

}

const std::string& TextureBase::GetName() const
{
 return Name;
}

const std::string& TextureBase::GetFileName() const
{
 return FileName;
}

void TextureBaseStorage::Load(const std::string &textures_path)
{
 try
 {
  for (const auto & entry : fs::directory_iterator(textures_path))
  {
   auto stem = entry.path().stem();
   auto ext = entry.path().extension();
   if(ext.string() == ".png")
   {
    TextureBase descr(stem.string(), entry.path().string());
    TextureBaseStorage[stem.string()] = descr;
   }
  }
 }
 catch(std::filesystem::filesystem_error &ex)
 {
  std::cerr << ex.what();
 }
}

const std::map<std::string, TextureBase>& TextureBaseStorage::GetBaseTextures() const
{
 return TextureBaseStorage;
}

}
