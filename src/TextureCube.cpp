#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QFile>
#include <filesystem>
#include <iostream>
#include "TextureCube.h"

namespace cutum {

namespace fs = std::filesystem;

TextureCube::TextureCube()
 : TypeId(0)
 , NumTextureFrames(0)
{

}

TextureCube::TextureCube(const std::string& name, size_t type_id, const std::vector<std::string>& texture_names)
 : Name(name)
 , TypeId(type_id)
 , TextureNames(texture_names)
 , NumTextureFrames((TextureNames.size()/6)<1?1:TextureNames.size()/6)
{

}

const std::string& TextureCube::GetName() const
{
 return Name;
}

size_t TextureCube::GetTypeId() const
{
 return TypeId;
}

GLuint TextureCube::GetTextureId() const
{
 return TextureId;
}

const std::vector<std::string>& TextureCube::GetTextureNames() const
{
 return TextureNames;
}

size_t TextureCube::GetNumTextureFrames() const
{
 return NumTextureFrames;
}

std::shared_ptr<QOpenGLTexture> TextureCube::GetTexture() const
{
 return Texture;
}

void TextureCube::SetTexture(std::shared_ptr<QOpenGLTexture> value)
{
 Texture = value;
 TextureId = Texture->textureId();
}

TextureCubeStorage::TextureCubeStorage(std::shared_ptr<TextureBaseStorage> base_textures)
 : TextureBaseStorageInstance(base_textures)
{

}

void TextureCubeStorage::GenerateCubeTextures()
{
}

void TextureCubeStorage::Load(const std::string &textures_path)
{
 try
 {
  for (const auto & entry : fs::directory_iterator(textures_path))
  {
   auto ext = entry.path().extension();
   if(ext.string() == ".json")
   {
    std::string name;
    size_t id;
    std::vector<std::string> textures;
    if(LoadJson(entry.path().string(), name, id, textures))
    {
     TextureCube descr=CreateCubeTexture(name, id, textures);
     Textures[descr.GetTypeId()] = descr;
    }
   }
  }
 }
 catch(std::filesystem::filesystem_error &ex)
 {
  std::cerr << ex.what();
 }
}

const std::map<size_t, TextureCube>& TextureCubeStorage::GetTextures() const
{
 return Textures;
}

size_t TextureCubeStorage::GetTypeIdByName(const std::string& name) const
{
 return TexturesNames.at(name);
}


TextureCube TextureCubeStorage::CreateCubeTexture(const std::string &cube_type_name, size_t cube_type_id, const std::vector<std::string>& texture_names)
{
 const auto & base_texture_descriptions = TextureBaseStorageInstance->GetBaseTextures();
 TextureCube result(cube_type_name, cube_type_id, texture_names);
 int num_texture_frames = int(result.GetNumTextureFrames());
 std::vector<QImage> texture_images(texture_names.size());
 for(size_t i=0;i<texture_names.size();i++)
 {
  texture_images[i] = QImage(base_texture_descriptions.at(texture_names[i]).GetFileName().c_str());
 }
 int width = texture_images[0].width();
 int hegiht = texture_images[0].height();
 QImage result_textures(width*6, hegiht*num_texture_frames, texture_images[0].format());
 QPainter p( &result_textures );
 size_t k=0;
 for(size_t j=0;j<num_texture_frames;j++)
 {
  for(size_t i=0;i<6;i++)
  {
   p.drawImage( int(i)*width, int(j)*hegiht, texture_images[k]);
   ++k;
  }
 }
 p.end();
 result.SetTexture(LoadTexture(result_textures));
 TexturesNames[cube_type_name] = cube_type_id;
 return result;
}

std::shared_ptr<QOpenGLTexture> TextureCubeStorage::LoadTexture(const QImage &image)
{
 auto texture = std::make_shared<QOpenGLTexture>(image.mirrored());
 texture->setMinificationFilter(QOpenGLTexture::Nearest);
 texture->setMagnificationFilter(QOpenGLTexture::Nearest);
 texture->setWrapMode(QOpenGLTexture::Repeat);

 return texture;
}

std::shared_ptr<QOpenGLTexture> TextureCubeStorage::LoadTexture(const std::string &texture_res_name)
{
 return LoadTexture(QImage(texture_res_name.c_str()));
}

bool TextureCubeStorage::LoadJson(const std::string& file_name, std::string &name, size_t &id, std::vector<std::string> &textures)
{
 QString val;
 QFile file;
 file.setFileName(file_name.c_str());
 file.open(QIODevice::ReadOnly | QIODevice::Text);
 val = file.readAll();
 file.close();
 qWarning() << val;
 QJsonDocument d = QJsonDocument::fromJson(val.toUtf8());
 QJsonObject sett2 = d.object();
 QJsonValue name_value = sett2.value(QString("name"));
 QJsonValue id_value = sett2.value(QString("id"));
 QJsonValue textures_value = sett2.value(QString("textures"));

 if(name_value.isUndefined() || id_value.isUndefined() || textures_value.isUndefined())
  return false;

 if(!name_value.isString())
  return false;
 name = name_value.toString().toLocal8Bit();

 id = id_value.toInt();

 if(!textures_value.isArray())
  return false;

 auto texture_array = textures_value.toArray();
 textures.resize(texture_array.size());
 for(int i=0; i<texture_array.size(); i++)
  textures[i] = texture_array[i].toString().toLocal8Bit();

 return true;
}

}
