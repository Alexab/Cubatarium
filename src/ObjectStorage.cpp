//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonValue>
//#include <QJsonArray>
//#include <QFile>
#include <filesystem>
#include <iostream>
#include "ObjectStorage.h"
#include "ObjectImplementation.h"
#include "TextureCube.h"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace cutum {

namespace fs = std::filesystem;

const ObjectPrototype& UnknownPrototype()
{
 static ObjectPrototype prototype;
 return prototype;
}

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
#ifdef CUBATARIUM_DEBUG
 std::cout << "ObjectStorage::Load: Loading from " << objects_path << std::endl;
#endif
 
 try
 {
  int loaded_count = 0;
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
      loaded_count++;
#ifdef CUBATARIUM_DEBUG
      std::cout << "ObjectStorage::Load: Added SingleCube prototype '" << name << "'" << std::endl;
#endif
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
      loaded_count++;
#ifdef CUBATARIUM_DEBUG
      std::cout << "ObjectStorage::Load: Added TerrainPlane prototype '" << name << "'" << std::endl;
#endif
     }
    }
   }
  }

#ifdef CUBATARIUM_DEBUG
 std::cout << "ObjectStorage::Load: Total loaded prototypes: " << loaded_count << std::endl;
#endif
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
 if(type_id == 0) {
  std::cerr << "ObjectStorage::GetPrototype: unknown type '" << type_name << "'" << std::endl;
  return UnknownPrototype();
 }
 auto it = Prototypes.find(type_id);
 if (it == Prototypes.end()) {
  std::cerr << "ObjectStorage::GetPrototype: missing prototype id " << type_id << std::endl;
  return UnknownPrototype();
 }
 return it->second;
}


std::shared_ptr<Object> ObjectStorage::TakeObject(const std::string& type_name)
{
 auto I = PrototypeNames.find(type_name);
 if (I != PrototypeNames.end()) {
     return TakeObject(I->second);
 }
 return nullptr;
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
 std::string val;
 std::ifstream file(file_name);
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
     std::cout << val << std::endl;
 } else {
     std::cerr << "Failed to open file: " << file_name << std::endl;
     return false;
 }

 try {
     json d = json::parse(val);
     std::string name_value = d.value("name", "");
     int id_value = d.value("id", 0);
     std::string class_value = d.value("class", "");
     json cube_textures_value = d.value("cube_textures", json::array());

     if(name_value.empty() || id_value == 0 || cube_textures_value.empty() || class_value.empty())
      return false;

     name = name_value;
     id = static_cast<size_t>(id_value);
     class_name = class_value;

     cube_textures.clear();
     for(const auto& texture : cube_textures_value) {
         if(texture.is_string()) {
             cube_textures.push_back(texture.get<std::string>());
         }
     }

     return true;
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error: " << e.what() << std::endl;
     return false;
 }
}

}
