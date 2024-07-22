#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QFile>
#include <iostream>
#include "Core.h"
#include "World.h"
#include "TextureCube.h"
#include "TextureBase.h"
#include "ObjectStorage.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "User.h"

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

 QString val;
 QFile file;
 file.setFileName(config_path.string().c_str());
 file.open(QIODevice::ReadOnly | QIODevice::Text);
 val = file.readAll();
 file.close();
 qWarning() << val;
 QJsonDocument d = QJsonDocument::fromJson(val.toUtf8());
 QJsonObject sett2 = d.object();
 QJsonValue default_world_value = sett2.value(QString("default_world"));
 QJsonValue default_user_value = sett2.value(QString("default_user"));

 bool is_need_autocreate=false;
 if(default_world_value.isUndefined() || default_user_value.isUndefined() ||
    !default_world_value.isString() ||
    !default_user_value.isString())
 {
  is_need_autocreate = true;
 }

 auto parent_dir = WorkDir.parent_path();

 default_world_name = default_world_value.toString().toLocal8Bit();
 default_user_name = default_user_value.toString().toLocal8Bit();

 texture_base_storage_file_name = parent_dir;
 texture_base_storage_file_name.append("textures").append("blocks");
 texture_cube_storage_file_name = parent_dir;
 texture_cube_storage_file_name.append("models").append("blocks");
 object_storage_file_name = parent_dir;
 object_storage_file_name.append("models").append("objects");
 WorldPath = parent_dir;
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
}

void Core::SaveSystem(const std::string& config_file_name)
{
 QJsonObject system_data;

 system_data.insert("default_world", QJsonValue(WorldInstance->GetWorldName().c_str()));
 system_data.insert("default_user", QJsonValue(WorldInstance->GetCurrentUserName().c_str()));

 QJsonDocument jsonDoc;
 jsonDoc.setObject(system_data);
 QString val;
 QFile file;
 file.setFileName(config_file_name.c_str());
 file.open(QIODevice::WriteOnly | QIODevice::Text);
 file.resize(0);
 val = file.write(jsonDoc.toJson());
 file.close();

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
