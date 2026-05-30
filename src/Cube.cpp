#include <iostream>
#include <sstream>
#include <cmath>
#include <limits>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Cube.h"

namespace cutum {

namespace {

constexpr float kRayEpsilon = 1e-6f;

glm::vec3 LocalFaceNormal(int axis, int sign, float halfExtent)
{
 switch (axis) {
 case 0: return sign < 0 ? glm::vec3(halfExtent, 0.0f, 0.0f) : glm::vec3(-halfExtent, 0.0f, 0.0f);
 case 1: return sign < 0 ? glm::vec3(0.0f, halfExtent, 0.0f) : glm::vec3(0.0f, -halfExtent, 0.0f);
 default: return sign < 0 ? glm::vec3(0.0f, 0.0f, halfExtent) : glm::vec3(0.0f, 0.0f, -halfExtent);
 }
}

int LocalFaceSide(int axis, int sign)
{
 switch (axis) {
 case 0: return sign < 0 ? CubeSide::CUBE_SIDE_LEFT : CubeSide::CUBE_SIDE_RIGHT;
 case 1: return sign < 0 ? CubeSide::CUBE_SIDE_TOP : CubeSide::CUBE_SIDE_BOTTOM;
 default: return sign < 0 ? CubeSide::CUBE_SIDE_FAR : CubeSide::CUBE_SIDE_NEAR;
 }
}

} // namespace

Cube::Cube()
{
}

Cube::Cube(const Cube &copy)
{
 InitialPose = copy.InitialPose;
 ObjectPose = copy.ObjectPose;
 Size = copy.Size;
}

Cube& Cube::operator = (const Cube &copy)
{
 InitialPose = copy.InitialPose;
 ObjectPose = copy.ObjectPose;
 Size = copy.Size;
 return *this;
}

const glm::mat4& Cube::GetObjectPose() const
{
 return ObjectPose;
}

const glm::mat4& Cube::GetInitialPose() const
{
 return InitialPose;
}

float Cube::GetSize() const
{
 return Size;
}

void Cube::SetSize(float size)
{
 Size = size;
}

void Cube::Init(const glm::mat4& initial_pose, float size)
{
 InitialPose = initial_pose;
 Size = size;
}

void Cube::SetObjectPose(const glm::mat4 &pose)
{
 ObjectPose = pose;
 UpdateVertices();
}

glm::vec3 Cube::GetCenterPosition() const
{
 glm::mat4 pose = ObjectPose * InitialPose;
 glm::vec3 a(pose[3][0], pose[3][1], pose[3][2]);
 return a;
}

bool Cube::CheckCollision(Cube &cube)
{
 glm::mat4 pose1 = ObjectPose * InitialPose;
 glm::mat4 pose2 = cube.GetObjectPose() * cube.GetInitialPose();
 glm::vec3 a(pose1[3][0], pose1[3][1], pose1[3][2]);
 glm::vec3 b(pose2[3][0], pose2[3][1], pose2[3][2]);

 //check the X axis
 if(fabs(a.x - b.x) < Size/2.0 + cube.Size/2.0)
 {
  //check the Y axis
  if(fabs(a.y - b.y) < Size/2.0 + cube.Size/2.0)
  {
   //check the Z axis
   if(fabs(a.z - b.z) < Size/2.0 + cube.Size/2.0)
   {
    std::stringstream s;
    s << "Cube collision: (" << a.x << "," << a.y << "," << a.z << ")" << " vs (" << b.x << "," << b.y << "," << b.z << ")" <<std::endl;
    std::cout << s.str() << std::endl;
    return true;
   }
  }
 }

 return false;
}

bool Cube::CheckCollision(const glm::vec3& position, float size)
{
 glm::mat4 pose1 = ObjectPose * InitialPose;
 glm::vec3 a(pose1[3][0], pose1[3][1], pose1[3][2]);
 glm::vec3 b(position);

 return Cube::CheckCollision(a, Size, b, size);
}

bool Cube::CheckCollision(const glm::vec3& position1, float size1, const glm::vec3& position2, float size2)
{
 glm::vec3 a(position1);
 glm::vec3 b(position2);

 //check the X axis
 if(fabs(a.x - b.x) < size1/2.0 + size2/2.0)
 {
  //check the Y axis
  if(fabs(a.y - b.y) < size1/2.0 + size2/2.0)
  {
   //check the Z axis
   if(fabs(a.z - b.z) < size1/2.0 + size2/2.0)
   {
    std::stringstream s;
    s << "Cube collision: (" << a.x << "," << a.y << "," << a.z << ")" << " vs (" << b.x << "," << b.y << "," << b.z << ")" <<std::endl;
    std::cout << s.str() << std::endl;
    return true;
   }
  }
 }

 return false;
}

bool Cube::IsIntersectionCube( const glm::vec3& originRay, const glm::vec3& dirRay, float sizeOfSide, std::map<float, std::pair<int,glm::vec3>> &intersected_sides) const
{
 intersected_sides.clear();

 const float halfExtent = sizeOfSide * 0.5f;
 const glm::vec3 boxMin(-halfExtent);
 const glm::vec3 boxMax(halfExtent);

 float tMin = -std::numeric_limits<float>::infinity();
 float tMax = std::numeric_limits<float>::infinity();
 int entryAxis = -1;
 int entrySign = 0;

 for (int axis = 0; axis < 3; ++axis) {
  if (std::abs(dirRay[axis]) < kRayEpsilon) {
   if (originRay[axis] < boxMin[axis] || originRay[axis] > boxMax[axis]) {
    return false;
   }
   continue;
  }

  const float invDir = 1.0f / dirRay[axis];
  float t1 = (boxMin[axis] - originRay[axis]) * invDir;
  float t2 = (boxMax[axis] - originRay[axis]) * invDir;
  int sign1 = -1;
  int sign2 = 1;
  if (t1 > t2) {
   std::swap(t1, t2);
   std::swap(sign1, sign2);
  }

  if (t1 > tMin) {
   tMin = t1;
   entryAxis = axis;
   entrySign = sign1;
  }
  tMax = std::min(tMax, t2);
  if (tMax < tMin) {
   return false;
  }
 }

 if (tMax < 0.0f) {
  return false;
 }

 const float tEntry = tMin >= 0.0f ? tMin : tMax;
 if (tEntry < 0.0f) {
  return false;
 }

 if (entryAxis < 0) {
  for (int axis = 0; axis < 3; ++axis) {
   if (std::abs(dirRay[axis]) < kRayEpsilon) {
    continue;
   }
   const float invDir = 1.0f / dirRay[axis];
   const float tNear = (boxMin[axis] - originRay[axis]) * invDir;
   const float tFar = (boxMax[axis] - originRay[axis]) * invDir;
   if (std::abs(tEntry - tNear) <= kRayEpsilon) {
    entryAxis = axis;
    entrySign = -1;
    break;
   }
   if (std::abs(tEntry - tFar) <= kRayEpsilon) {
    entryAxis = axis;
    entrySign = 1;
    break;
   }
  }
 }

 if (entryAxis < 0) {
  return false;
 }

 const int side = LocalFaceSide(entryAxis, entrySign);
 const glm::vec3 localNormal = LocalFaceNormal(entryAxis, entrySign, halfExtent);
 intersected_sides[tEntry] = std::pair<int, glm::vec3>(side, localNormal);
 return true;
}

bool Cube::IsIntersectionCube( const glm::vec3& originRay, const glm::vec3& dirRay, float sizeOfSide, int &side, glm::vec3& normal, float &distance) const
{
 std::map<float, std::pair<int,glm::vec3>> intersected_sides;
 bool result = IsIntersectionCube(originRay, dirRay, sizeOfSide, intersected_sides);

 if(!intersected_sides.empty())
 {
  distance = intersected_sides.begin()->first;
  side = intersected_sides.begin()->second.first;
  normal = intersected_sides.begin()->second.second;
 }

 return result;
}

// https://www.gamedev.ru/code/forum/?id=40346
bool Cube::CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3>> &intersection_results) const
{
 intersection_results.clear();

 std::map<float, std::pair<int,glm::vec3>> intersected_sides;
 glm::mat4 pose = ObjectPose * InitialPose;
 double size = Size;

 glm::mat4 invPose = glm::inverse(pose);
 glm::vec4 rel_position_vec4 = invPose * glm::vec4(position, 1.0f);
 glm::vec3 rel_position(rel_position_vec4.x, rel_position_vec4.y, rel_position_vec4.z);

 glm::vec3 localDir = glm::vec3(invPose * glm::vec4(front, 0.0f));
 const float localDirLen = glm::length(localDir);
 if (localDirLen < kRayEpsilon) {
  return false;
 }
 localDir /= localDirLen;

 bool is_intersected = IsIntersectionCube(rel_position, localDir, static_cast<float>(size), intersected_sides);

 if (is_intersected && !intersected_sides.empty()) {
  const float t = intersected_sides.begin()->first;
  const auto& hit = intersected_sides.begin()->second;
  const glm::vec3 hitLocal = rel_position + localDir * t;
  const glm::vec3 hitWorld = glm::vec3(pose * glm::vec4(hitLocal, 1.0f));
  const float worldDistance = glm::length(hitWorld - position);
  intersection_results[worldDistance] = std::tuple<int, glm::vec3, glm::vec3>(
      hit.first, hit.second, hitWorld);
 }

 return is_intersected && !intersection_results.empty();
}

size_t Cube::GetTypeId() const
{
 return TypeId;
}

void Cube::SetTypeId(size_t value)
{
 TypeId = value;
}

void Cube::Copy(const Cube &copy)
{
 SetTypeId(copy.GetTypeId());
 Init(copy.GetInitialPose(), copy.GetSize());
}

void Cube::Copy(std::shared_ptr<Cube> copy)
{
 Cube::Copy(*copy);
}

}

