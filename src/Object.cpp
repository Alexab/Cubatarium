#include <iostream>
#include <sstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Object.h"

namespace cutum {

UObjectPrototype::UObjectPrototype(const std::string& type_name, uint64_t type_id, std::shared_ptr<UObject> sample)
 : TypeName(type_name)
 , TypeId(type_id)
 , Sample(sample)
{
 Sample->SetObjectTypeId(type_id);
}

const std::string& UObjectPrototype::GetTypeName() const
{
 return TypeName;
}

uint64_t UObjectPrototype::GetTypeId() const
{
 return TypeId;
}

std::shared_ptr<UObject> UObjectPrototype::GetSample() const
{
 return Sample;
}


std::shared_ptr<UObject> UObjectPrototype::New()
{
 auto result=Sample->New();
 result->SetObjectTypeId(TypeId);
 return result;
}

std::atomic_uint64_t UObject::LastObjectId=0;

UObject::UObject()
 : ObjectId(++LastObjectId)
 , ObjectTypeId(0)
{

}
/*
UObject::UObject(const UObject &copy)
 : ObjectId(++LastObjectId)
 : Cubes;
glm::mat4 Pose;
{

}

UObject& UObject::operator = (const UObject &copy)
{

}*/

std::shared_ptr<UObject> UObject::New()
{
 return std::make_shared<UObject>();
}

void UObject::Copy(std::shared_ptr<UObject> copy)
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



uint64_t UObject::GetObjectId() const
{
 return ObjectId;
}

uint64_t UObject::GetObjectTypeId() const
{
 return ObjectTypeId;
}

std::vector<std::shared_ptr<UCube>>& UObject::GetCubes()
{
 return Cubes;
}

bool UObject::CheckCollision(UObject &object)
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

bool UObject::CheckCollision(const glm::vec3& position, float size)
{
 for(auto & cube : Cubes)
 {
  if(cube->CheckCollision(position, size))
   return true;
 }
 return false;
}

bool UObject::CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t>> &distance_map) const
{
 distance_map.clear();


 for(size_t i=0; i<Cubes.size(); i++)
 {
  std::map<float, std::tuple<int, glm::vec3, glm::vec3>> cube_intersection_results;
  if(Cubes[i]->CheckRayIntersection(position, front, cube_intersection_results))
  {
   for(auto I = cube_intersection_results.begin();I != cube_intersection_results.end(); ++I)
   {
    float dist = I->first;
    distance_map[dist] = std::tuple<int, glm::vec3, glm::vec3, size_t>(std::get<0>(I->second),
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

glm::mat4 UObject::GetPose() const
{
 return Pose;
}

void UObject::SetPose(const glm::mat4 &value)
{
 Pose = value;
 UpdatePose();
}

void UObject::SetPoseFromTranslation(const glm::vec3 &translation)
{
 glm::mat4 pose = glm::mat4(1.0f);
 pose = glm::translate(pose, translation);
 SetPose(pose);
}

void UObject::UpdatePose()
{
 for(auto & cube : Cubes)
 {
  cube->SetObjectPose(Pose);
 }
}

void UObject::SetObjectTypeId(uint64_t value)
{
 ObjectTypeId = value;
}

}

