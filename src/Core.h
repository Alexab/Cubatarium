#ifndef CORE_H
#define CORE_H

#include <map>
#include <memory>
#include <string>
#include <filesystem>

namespace cutum {

class World;
class TextureBaseStorage;
class TextureCubeStorage;
class ObjectStorage;
class World;
class GeometryEngine;
class ViewEngine;

class Core
{
public:
 Core(std::shared_ptr<TextureBaseStorage> texture_base_storage_,
      std::shared_ptr<TextureCubeStorage> texture_cube_storage_,
      std::shared_ptr<ObjectStorage> object_storage_,
      std::shared_ptr<World> world_,
      std::shared_ptr<GeometryEngine>
      geometries_,
      std::shared_ptr<ViewEngine> views_);

public:
 void LoadSystem(const std::string& config_file_name);
 void SaveSystem(const std::string& config_file_name);

 void CreateWorld(const std::string& world_name);
 void LoadWorld(const std::string& world_name);
 void SaveWorld(const std::string& world_name);

 void LoadWorldList(const std::string& world_path);

private:
 std::vector<std::string> WorldList;

 std::filesystem::path WorkDir;

 std::filesystem::path texture_base_storage_file_name;
 std::filesystem::path texture_cube_storage_file_name;
 std::filesystem::path object_storage_file_name;
 std::filesystem::path WorldPath;

 std::string default_world_name;
 std::string default_user_name;

 std::shared_ptr<TextureBaseStorage> TextureBaseStorageInstance;
 std::shared_ptr<TextureCubeStorage> TextureCubeStorageInstance;
 std::shared_ptr<ObjectStorage> ObjectStorageInstance;
 std::shared_ptr<GeometryEngine> GeometryEngineInstance;
 std::shared_ptr<ViewEngine> ViewEngineInstance;
 std::shared_ptr<World> WorldInstance;
};

}
#endif // CORE_H
