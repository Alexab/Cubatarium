#ifndef OBJECTSTORAGE_H
#define OBJECTSTORAGE_H

#include "Object.h"
#include "ObjectImplementation.h"

namespace cutum {

class TextureCubeStorage;

class ObjectStorage
{
public:
 ObjectStorage(std::shared_ptr<TextureCubeStorage> texture_cube);

 void Generate();
 void Load(const std::string& objects_path);
 void Save(const std::string& objects_path);

 bool AddPrototype(const ObjectPrototype& prototype);
 const ObjectPrototype& GetPrototype(const std::string& type_name) const;

 std::shared_ptr<Object> TakeObject(const std::string& type_name);
 std::shared_ptr<Object> TakeObject(uint64_t type_id);

 uint64_t GetObjectTypeId(const std::string& type_name) const;
 std::string GetObjectTypeName(uint64_t type_id) const;

private:
 bool LoadJson(const std::string& file_name, std::string &name, size_t &id, std::string &class_name, std::vector<std::string> &cube_textures);

private:
 std::map<std::string, uint64_t> PrototypeNames;
 std::map<uint64_t, ObjectPrototype> Prototypes;

 std::shared_ptr<TextureCubeStorage> TextureCubeInstance;
};

}

#endif // OBJECTSTORAGE_H
