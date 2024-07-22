#ifndef TEXTUREBASE_H
#define TEXTUREBASE_H

#include <string>
#include <map>

namespace cutum {

class TextureBase
{
public:
 TextureBase();
 TextureBase(const std::string& name, const std::string& file_name);

 const std::string& GetName() const;
 const std::string& GetFileName() const;

private:
 std::string Name;
 std::string FileName;
};

class TextureBaseStorage
{
public:
 void Load(const std::string &textures_path);

 const std::map<std::string, TextureBase>& GetBaseTextures() const;

private:
 std::map<std::string, TextureBase> TextureBaseStorage;
};

}

#endif // TEXTUREBASE_H
