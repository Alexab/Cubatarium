//#include <QPainter>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonValue>
//#include <QJsonArray>
//#include <QFile>
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "Core.h"
#include "World.h"
#include "TextureCube.h"
#include "TextureBase.h"
#include "ObjectStorage.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "User.h"

using json = nlohmann::json;

namespace cutum {

Core::Core(std::shared_ptr<TextureBaseStorage> texture_base_storage_,
           std::shared_ptr<TextureCubeStorage> texture_cube_storage_,
           std::shared_ptr<ObjectStorage> object_storage_,
           std::shared_ptr<World> world_,
           std::shared_ptr<GeometryEngine> geometries_,
           std::shared_ptr<ViewEngine> views_)
 : TextureBaseStorageInstance(texture_base_storage_)
 , TextureCubeStorageInstance(texture_cube_storage_)
 , ObjectStorageInstance(object_storage_)
 , WorldInstance(world_)
 , GeometryEngineInstance(geometries_)
 , ViewEngineInstance(views_)
{
}

void Core::LoadSystem(const std::string& config_file_name)
{
 
 WorkDir = std::filesystem::current_path();
 
 auto config_path = WorkDir;
 config_path.append(config_file_name);
 
 std::string val;
 std::ifstream file(config_path.string());
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
     
 } else {
     // Try parent directory for config.json
     auto parent_config = WorkDir;
     if (parent_config.has_parent_path()) {
         parent_config = parent_config.parent_path();
         parent_config.append(config_file_name);
         
         std::ifstream pfile(parent_config.string());
         if (pfile.is_open()) {
             std::stringstream buffer;
             buffer << pfile.rdbuf();
             val = buffer.str();
             pfile.close();
             
         } else {
             std::cerr << "Failed to open config file: " << config_path.string() << " and parent: " << parent_config.string() << std::endl;
             return;
         }
     } else {
         std::cerr << "Failed to open config file: " << config_path.string() << std::endl;
         return;
     }
 }

 try {
     json d = json::parse(val);
     std::string default_world_value = d.value("default_world", "");
     std::string default_user_value = d.value("default_user", "");

     bool is_need_autocreate = false;
     if(default_world_value.empty() || default_user_value.empty())
     {
      is_need_autocreate = true;
     }

     // Use current directory; if resources are not here, try parent directory
     auto project_dir = WorkDir;
     auto textures_test = project_dir; textures_test.append("textures").append("blocks");
     if (!std::filesystem::exists(textures_test)) {
         if (project_dir.has_parent_path()) {
             auto parent_dir = project_dir.parent_path();
             auto parent_textures = parent_dir; parent_textures.append("textures").append("blocks");
             if (std::filesystem::exists(parent_textures)) {
                 
                 project_dir = parent_dir;
             } else {
                 
             }
         }
     }

     default_world_name = default_world_value;
     default_user_name = default_user_value;

     texture_base_storage_file_name = project_dir;
     texture_base_storage_file_name.append("textures").append("blocks");
     texture_cube_storage_file_name = project_dir;
     texture_cube_storage_file_name.append("models").append("blocks");
     object_storage_file_name = project_dir;
     object_storage_file_name.append("models").append("objects");
     WorldPath = project_dir;
     WorldPath.append("worlds");

      TextureBaseStorageInstance->Load(texture_base_storage_file_name.string());
      
      TextureCubeStorageInstance->Load(texture_cube_storage_file_name.string());
      
      ObjectStorageInstance->Load(object_storage_file_name.string());
     

     LoadWorldList(WorldPath.string());
     if(WorldList.empty() || std::find(WorldList.begin(), WorldList.end(), default_world_name) == WorldList.end())
      is_need_autocreate = true;

     if(is_need_autocreate)
     {
       CreateWorld("World");
       SaveSystem(config_file_name);
     }
     else
     {
      LoadWorld(default_world_name);
      WorldInstance->SetCurrentUserName(default_user_name);
      
     }
     if(WorldInstance->GetCurrentUser()->GetActiveObject() == nullptr)
      WorldInstance->GetCurrentUser()->SetActiveObjectTypeName("grass");
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error: " << e.what() << std::endl;
     CreateWorld("World");
     SaveSystem(config_file_name);
 }
}

void Core::SaveSystem(const std::string& config_file_name)
{
 json system_data;

 system_data["default_world"] = WorldInstance->GetWorldName();
 system_data["default_user"] = WorldInstance->GetCurrentUserName();

 std::ofstream file(config_file_name);
 if (file.is_open()) {
     file << system_data.dump(4);
     file.close();
 }

 SaveWorld(WorldInstance->GetWorldName());
}

void Core::CreateWorld(const std::string& world_name)
{
 WorldInstance->Create(world_name);
 if(WorldInstance->GetCurrentUser() == nullptr)
 {
  WorldInstance->GenerateUsers();
 }
 SaveWorld(world_name);
}

void Core::LoadWorld(const std::string& world_name)
{
  std::filesystem::path world_path=WorldPath;
  world_path.append(world_name);
 
 WorldInstance->Load(world_path.string());
 if(WorldInstance->GetCurrentUser() == nullptr)
 {
  WorldInstance->GenerateUsers();
 }
}

void Core::SaveWorld(const std::string& world_name)
{
 std::filesystem::path world_path=WorldPath;
 world_path.append(world_name);

 WorldInstance->Save(world_path.string());
}

void Core::LoadWorldList(const std::string& world_path)
{
 try
 {
  for (const auto & entry : std::filesystem::directory_iterator(world_path))
  {
   auto world_dir_name = entry.path().filename();
   WorldList.push_back(world_dir_name.string());
  }
 }
 catch(std::filesystem::filesystem_error &ex)
 {
  std::cerr << ex.what();
 }
}

}
