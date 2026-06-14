#ifndef OBJECTSTORAGE_H
#define OBJECTSTORAGE_H

#include "Object.h"
#include "ObjectImplementation.h"

namespace cutum {

class UTextureCubeStorage;

class UObjectStorage
{
public:
 UObjectStorage(std::shared_ptr<UTextureCubeStorage> texture_cube);

 void Generate();
 void Load(const std::string& objects_path);
 void Save(const std::string& objects_path);

 bool AddPrototype(const UObjectPrototype& prototype);
 const UObjectPrototype& GetPrototype(const std::string& type_name) const;

 std::shared_ptr<UObject> TakeObject(const std::string& type_name);
 std::shared_ptr<UObject> TakeObject(uint64_t type_id);

 uint64_t GetObjectTypeId(const std::string& type_name) const;
 std::string GetObjectTypeName(uint64_t type_id) const;

 std::shared_ptr<UTextureCubeStorage> GetTextureCubeStorage() const { return TextureCubeInstance; }

private:
 bool LoadJson(const std::string& file_name, std::string &name, size_t &id, std::string &class_name, std::vector<std::string> &cube_textures);

private:
 std::map<std::string, uint64_t> PrototypeNames;
 std::map<uint64_t, UObjectPrototype> Prototypes;

 std::shared_ptr<UTextureCubeStorage> TextureCubeInstance;
};

}

#endif // OBJECTSTORAGE_H
