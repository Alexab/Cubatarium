#include <iostream>
#include <sstream>
#include "Object.h"
#include <QDebug>

namespace cutum {

ObjectPrototype::ObjectPrototype(const std::string& type_name, uint64_t type_id, std::shared_ptr<Object> sample)
 : TypeName(type_name)
 , TypeId(type_id)
 , Sample(sample)
{
 Sample->SetObjectTypeId(type_id);
}

const std::string& ObjectPrototype::GetTypeName() const
{
 return TypeName;
}

uint64_t ObjectPrototype::GetTypeId() const
{
 return TypeId;
}

std::shared_ptr<Object> ObjectPrototype::GetSample() const
{
 return Sample;
}


std::shared_ptr<Object> ObjectPrototype::New()
{
 auto result=Sample->New();
 result->SetObjectTypeId(TypeId);
 return result;
}

std::atomic_uint64_t Object::LastObjectId=0;

Object::Object()
 : ObjectId(++LastObjectId)
 , ObjectTypeId(0)
{

}
/*
Object::Object(const Object &copy)
 : ObjectId(++LastObjectId)
 : Cubes;
QMatrix4x4 Pose;
{

}

Object& Object::operator = (const Object &copy)
{

}*/

std::shared_ptr<Object> Object::New()
{
 return std::make_shared<Object>();
}

void Object::Copy(std::shared_ptr<Object> copy)
{
 SetObjectTypeId(copy->GetObjectTypeId());
 if(Cubes.size() == copy->GetCubes().size())
 {
  for(size_t i=0;i<Cubes.size();i++)
  {
   Cubes[i]->Copy(copy->GetCubes()[i]);
  }
 }
}



uint64_t Object::GetObjectId() const
{
 return ObjectId;
}

uint64_t Object::GetObjectTypeId() const
{
 return ObjectTypeId;
}

std::vector<std::shared_ptr<Cube>>& Object::GetCubes()
{
 return Cubes;
}

bool Object::CheckCollision(Object &object)
{
 auto & object_cubes = object.GetCubes();
 for(auto & cube : Cubes)
 {
  for(auto & object_cube : object_cubes)
  {
   if(cube->CheckCollision(*object_cube))
    return true;
  }
 }
 return false;
}

bool Object::CheckCollision(const QVector3D& position, float size)
{
 for(auto & cube : Cubes)
 {
  if(cube->CheckCollision(position, size))
   return true;
 }
 return false;
}

bool Object::CheckRayIntersection(const QVector3D& position, const QVector3D& front, std::map<float, std::tuple<int, QVector3D, QVector3D, size_t>> &distance_map) const
{
 distance_map.clear();


 for(size_t i=0; i<Cubes.size(); i++)
 {
  std::map<float, std::tuple<int, QVector3D, QVector3D>> cube_intersection_results;
  if(Cubes[i]->CheckRayIntersection(position, front, cube_intersection_results))
  {
   for(auto I = cube_intersection_results.begin();I != cube_intersection_results.end(); ++I)
   {
    float dist = I->first;
    distance_map[dist] = std::tuple<int, QVector3D, QVector3D, size_t>(std::get<0>(I->second),
                                                                            std::get<1>(I->second),
                                                                            std::get<2>(I->second),
                                                                            i);
   }
  }
 }

 if(distance_map.empty())
  return false;

 return true;
}

QMatrix4x4 Object::GetPose() const
{
 return Pose;
}

void Object::SetPose(const QMatrix4x4 &value)
{
 Pose = value;
 UpdatePose();
}

void Object::SetPoseFromTranslation(const QVector3D &translation)
{
 QMatrix4x4 pose;
 pose.setToIdentity();
 pose.translate(translation);
 SetPose(pose);
}

void Object::UpdatePose()
{
 for(auto & cube : Cubes)
 {
  cube->SetObjectPose(Pose);
 }
}

void Object::SetObjectTypeId(uint64_t value)
{
 ObjectTypeId = value;
}

}

