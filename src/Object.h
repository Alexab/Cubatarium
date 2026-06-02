#ifndef OBJECT_H
#define OBJECT_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <map>
#include <tuple>
#include <memory>
#include <atomic>
#include <string>
#include "Cube.h"

namespace cutum {

class Object;

class ObjectPrototype
{
public:
 ObjectPrototype() = default;
 ObjectPrototype(const ObjectPrototype&) = default;
 ObjectPrototype(const std::string& type_name, uint64_t type_id, std::shared_ptr<Object> sample);

 const std::string& GetTypeName() const;
 uint64_t GetTypeId() const;
 std::shared_ptr<Object> GetSample() const;

 std::shared_ptr<Object> New();

private:
 std::string TypeName;
 uint64_t TypeId;

 std::shared_ptr<Object> Sample;
};

class Object
{
friend class ObjectPrototype;
public:
 Object();
 Object(const Object &copy) = delete;
 Object& operator = (const Object &copy) = delete;

 virtual std::shared_ptr<Object> New();
 virtual void Copy(std::shared_ptr<Object> copy);

 uint64_t GetObjectId() const;
 uint64_t GetObjectTypeId() const;

 virtual std::vector<std::shared_ptr<Cube>>& GetCubes();
 virtual bool CheckCollision(Object &object);
 virtual bool CheckCollision(const glm::vec3& position, float size = 1.0); // QVector3D -> glm::vec3
 virtual bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t>> &distance_map) const; // QVector3D -> glm::vec3

 glm::mat4 GetPose() const; // QMatrix4x4 -> glm::mat4
 void SetPose(const glm::mat4 &value); // QMatrix4x4 -> glm::mat4
 void SetPoseFromTranslation(const glm::vec3 &translation); // QVector3D -> glm::vec3

protected:
 void UpdatePose();
 void SetObjectTypeId(uint64_t value);
protected:
 std::vector<std::shared_ptr<Cube>> Cubes;
 glm::mat4 Pose; // QMatrix4x4 -> glm::mat4
 uint64_t ObjectId;
 uint64_t ObjectTypeId;

 static std::atomic_uint64_t LastObjectId;
};

}

#endif // OBJECT_H
