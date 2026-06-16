#ifndef TEXTUREBASE_H
#define TEXTUREBASE_H

#include <map>
#include <string>

namespace cutum
{

class UTextureBase
{
public:
  UTextureBase();
  UTextureBase(const std::string &Name, const std::string &file_name);

  const std::string &GetName() const;
  const std::string &GetFileName() const;

private:
  std::string Name;
  std::string FileName;
};

class UTextureBaseStorage
{
public:
  void Load(const std::string &textures_path);

  const std::map<std::string, UTextureBase> &GetBaseTextures() const;

private:
  std::map<std::string, UTextureBase> BaseTextures;
};

} // namespace cutum

#endif // TEXTUREBASE_H
