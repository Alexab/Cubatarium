#ifndef OBJECT_H
#define OBJECT_H

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
 virtual bool CheckCollision(const QVector3D& position, float size = 1.0);
 virtual bool CheckRayIntersection(const QVector3D& position, const QVector3D& front, std::map<float, std::tuple<int, QVector3D, QVector3D, size_t>> &distance_map) const;

 QMatrix4x4 GetPose() const;
 void SetPose(const QMatrix4x4 &value);
 void SetPoseFromTranslation(const QVector3D &translation);

protected:
 void UpdatePose();
 void SetObjectTypeId(uint64_t value);
protected:
 std::vector<std::shared_ptr<Cube>> Cubes;
 QMatrix4x4 Pose;
 uint64_t ObjectId;
 uint64_t ObjectTypeId;

 static std::atomic_uint64_t LastObjectId;
};

}

#endif // OBJECT_H
