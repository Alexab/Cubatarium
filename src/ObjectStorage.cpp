#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QFile>
#include <filesystem>
#include <iostream>
#include "ObjectStorage.h"
#include "ObjectImplementation.h"
#include "TextureCube.h"

namespace cutum {

namespace fs = std::filesystem;

ObjectStorage::ObjectStorage(std::shared_ptr<TextureCubeStorage> texture_cube)
 : TextureCubeInstance(texture_cube)
{

}

void ObjectStorage::Generate()
{
// ObjectPrototype wood("wood", 1, std::make_shared<SingleCube>(1));
// AddPrototype(wood);
// ObjectPrototype grass("grass", 2, std::make_shared<SingleCube>(2));
// AddPrototype(grass);
// ObjectPrototype stone("stone", 3, std::make_shared<SingleCube>(3));
// AddPrototype(stone);
// ObjectPrototype tree_birch("tree_birch", 4, std::make_shared<SingleCube>(4));
// AddPrototype(tree_birch);
}

void ObjectStorage::Load(const std::string& objects_path)
{
 try
 {
  for (const auto & entry : fs::directory_iterator(objects_path))
  {
   auto ext = entry.path().extension();
   if(ext.string() == ".json")
   {
    std::string name;
    size_t id;
    std::string class_name;
    std::vector<std::string> cube_textures;
    if(LoadJson(entry.path().string(), name, id, class_name, cube_textures))
    {
     if(class_name == "SingleCube" && cube_textures.size()>0)
     {
      std::string texture_name = cube_textures[0];
      auto texture_cube_id = TextureCubeInstance->GetTypeIdByName(texture_name);
      if(texture_cube_id == 0)
      {
        std::cerr << "Texture id for name "<< texture_name << "not found";
        continue;
      }
      ObjectPrototype object_description(name, id, std::make_shared<SingleCube>(texture_cube_id));
      AddPrototype(object_description);
     }
     else
     if(class_name == "TerrainPlane" && cube_textures.size()>0)
     {
      std::string texture_name = cube_textures[0];
      auto texture_cube_id = TextureCubeInstance->GetTypeIdByName(texture_name);
      if(texture_cube_id == 0)
      {
        std::cerr << "Texture id for name "<< texture_name << "not found";
        continue;
      }
      ObjectPrototype object_description(name, id, std::make_shared<TerrainPlane>(30, 30));
      std::dynamic_pointer_cast<TerrainPlane>(object_description.GetSample())->Generate(texture_cube_id);
      AddPrototype(object_description);
     }
    }
   }
  }
 }
 catch(std::filesystem::filesystem_error &ex)
 {
  std::cerr << ex.what();
 }
}

void ObjectStorage::Save(const std::string& objects_path)
{

}

bool ObjectStorage::AddPrototype(const ObjectPrototype& prototype)
{
 if(PrototypeNames.find(prototype.GetTypeName()) != PrototypeNames.end())
  return false;

 if(prototype.GetTypeId() == 0)
  return false;

 PrototypeNames[prototype.GetTypeName()] = prototype.GetTypeId();
 Prototypes[prototype.GetTypeId()] = prototype;
 return true;
}

const ObjectPrototype& ObjectStorage::GetPrototype(const std::string& type_name) const
{
 auto type_id = GetObjectTypeId(type_name);
 if(type_id == 0)
  throw 1; // TODO
 return Prototypes.at(type_id);
}


std::shared_ptr<Object> ObjectStorage::TakeObject(const std::string& type_name)
{
 auto I = PrototypeNames.find(type_name);
 return (I != PrototypeNames.end())?TakeObject(I->second):nullptr;
}

std::shared_ptr<Object> ObjectStorage::TakeObject(uint64_t type_id)
{
 auto I = Prototypes.find(type_id);
 auto result = (I != Prototypes.end())?I->second.New():nullptr;
 if(result)
  result->Copy(I->second.GetSample());
 //return (I != Prototypes.end())?I->second.New():nullptr;
 return result;
}

uint64_t ObjectStorage::GetObjectTypeId(const std::string& type_name) const
{
 auto I = PrototypeNames.find(type_name);
 return (I != PrototypeNames.end())?I->second:0;
}

std::string ObjectStorage::GetObjectTypeName(uint64_t type_id) const
{
 for(auto I = PrototypeNames.begin(); I != PrototypeNames.end(); ++I)
 {
  if(I->second == type_id)
   return I->first;
 }
 return std::string("");
}

bool ObjectStorage::LoadJson(const std::string& file_name, std::string &name, size_t &id, std::string &class_name, std::vector<std::string> &cube_textures)
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
 QJsonValue class_value = sett2.value(QString("class"));
 QJsonValue cube_textures_value = sett2.value(QString("cube_textures"));

 if(name_value.isUndefined() || id_value.isUndefined() || cube_textures_value.isUndefined() ||
    class_value.isUndefined())
  return false;

 if(!name_value.isString())
  return false;
 name = name_value.toString().toLocal8Bit();

 id = id_value.toInt();

 if(!class_value.isString())
  return false;
 class_name = class_value.toString().toLocal8Bit();

 if(!cube_textures_value.isArray())
  return false;

 auto texture_array = cube_textures_value.toArray();
 cube_textures.resize(texture_array.size());
 for(int i=0; i<texture_array.size(); i++)
  cube_textures[i] = texture_array[i].toString().toLocal8Bit();

 return true;
}

}
