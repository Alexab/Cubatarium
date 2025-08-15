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
 std::cout << "Core::LoadSystem: Loading system from " << config_file_name << std::endl;
 
 WorkDir = std::filesystem::current_path();
 std::cout << "Core::LoadSystem: Working directory: " << WorkDir.string() << std::endl;

 auto config_path = WorkDir;
 config_path.append(config_file_name);
 std::cout << "Core::LoadSystem: Config path: " << config_path.string() << std::endl;

 std::string val;
 std::ifstream file(config_path.string());
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
     std::cout << val << std::endl;
 } else {
     std::cerr << "Failed to open config file: " << config_path.string() << std::endl;
     return;
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

     // Используем текущую директорию вместо родительской
     auto project_dir = WorkDir;

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

     std::cout << "Loading textures from: " << texture_base_storage_file_name.string() << std::endl;
     TextureBaseStorageInstance->Load(texture_base_storage_file_name.string());
     
     std::cout << "Loading cube textures from: " << texture_cube_storage_file_name.string() << std::endl;
     TextureCubeStorageInstance->Load(texture_cube_storage_file_name.string());
     
     std::cout << "Loading objects from: " << object_storage_file_name.string() << std::endl;
     ObjectStorageInstance->Load(object_storage_file_name.string());
     std::cout << "Core::LoadSystem: Objects loaded successfully" << std::endl;

     LoadWorldList(WorldPath.string());
     if(WorldList.empty() || std::find(WorldList.begin(), WorldList.end(), default_world_name) == WorldList.end())
      is_need_autocreate = true;

     if(is_need_autocreate)
     {
      std::cout << "Creating new world: World" << std::endl;
      CreateWorld("World");
      SaveSystem(config_file_name);
     }
     else
     {
      std::cout << "Loading existing world: " << default_world_name << std::endl;
      LoadWorld(default_world_name);
      WorldInstance->SetCurrentUserName(default_user_name);
      std::cout << "Core::LoadSystem: World loaded successfully" << std::endl;
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
 std::cout << "Core::LoadWorld: Loading world '" << world_name << "'" << std::endl;
 
 std::filesystem::path world_path=WorldPath;
 world_path.append(world_name);
 std::cout << "Core::LoadWorld: World path: " << world_path.string() << std::endl;

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
