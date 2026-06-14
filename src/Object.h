#ifndef OBJECT_H
#define OBJECT_H

#include "Cube.h"
#include <atomic>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace cutum
{

class UObject;

class UObjectPrototype
{
public:
  UObjectPrototype() = default;
  UObjectPrototype(const UObjectPrototype &) = default;
  UObjectPrototype(const std::string &type_name, uint64_t type_id,
                   std::shared_ptr<UObject> sample);

  const std::string &GetTypeName() const;
  uint64_t GetTypeId() const;
  std::shared_ptr<UObject> GetSample() const;

  std::shared_ptr<UObject> New();

private:
  std::string TypeName;
  uint64_t TypeId;

  std::shared_ptr<UObject> Sample;
};

class UObject
{
  friend class UObjectPrototype;

public:
  UObject();
  UObject(const UObject &copy) = delete;
  UObject &operator=(const UObject &copy) = delete;

  virtual std::shared_ptr<UObject> New();
  virtual void Copy(std::shared_ptr<UObject> copy);

  uint64_t GetObjectId() const;
  uint64_t GetObjectTypeId() const;

  virtual std::vector<std::shared_ptr<UCube>> &GetCubes();
  virtual bool CheckCollision(UObject &object);
  virtual bool CheckCollision(const glm::vec3 &position,
                              float size = 1.0); // QVector3D -> glm::vec3
  virtual bool CheckRayIntersection(
      const glm::vec3 &position, const glm::vec3 &front,
      std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t>>
          &distance_map) const; // QVector3D -> glm::vec3

  glm::mat4 GetPose() const;            // QMatrix4x4 -> glm::mat4
  void SetPose(const glm::mat4 &value); // QMatrix4x4 -> glm::mat4
  void SetPoseFromTranslation(
      const glm::vec3 &translation); // QVector3D -> glm::vec3

protected:
  void UpdatePose();
  void SetObjectTypeId(uint64_t value);

protected:
  std::vector<std::shared_ptr<UCube>> Cubes;
  glm::mat4 Pose; // QMatrix4x4 -> glm::mat4
  uint64_t ObjectId;
  uint64_t ObjectTypeId;

  static std::atomic_uint64_t LastObjectId;
};

} // namespace cutum

#endif // OBJECT_H
