#include <iostream>
#include <sstream>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Cube.h"

namespace cutum {

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
    float d = sizeOfSide / 2;
    float x, z, y;

    intersected_sides.clear();

    // Верхняя грань куба
    x = originRay.x + dirRay.x * ( d - originRay.y ) / dirRay.y;
    z = originRay.z + dirRay.z * ( d - originRay.y ) / dirRay.y;
    if( ( x < d ) && ( x > -d ) && ( z < d ) && ( z > -d ) )
    {
     glm::vec3 normal_to_side_local = glm::vec3(0.0f, d, 0.0f);
     float length = glm::length(originRay + normal_to_side_local);
     intersected_sides[length] = std::pair<int, glm::vec3>(CubeSide::CUBE_SIDE_TOP, normal_to_side_local);
    }

    // Нижняя грань куба
    x = originRay.x + dirRay.x * ( -d - originRay.y ) / dirRay.y;
    z = originRay.z + dirRay.z * ( -d - originRay.y ) / dirRay.y;
    if( ( x < d ) && ( x > -d ) && ( z < d ) && ( z > -d ) )
    {
     glm::vec3 normal_to_side_local = glm::vec3(0.0f, -d, 0.0f);
     float length = glm::length(originRay + normal_to_side_local);
     intersected_sides[length] = std::pair<int, glm::vec3>(CubeSide::CUBE_SIDE_BOTTOM, normal_to_side_local);
    }

    // Правая грань куба
    z = originRay.z + dirRay.z * ( d - originRay.x ) / dirRay.x;
    y = originRay.y + dirRay.y * ( d - originRay.x ) / dirRay.x;
    if( ( z < d ) && ( z > -d ) && ( y < d ) && ( y > -d ) )
    {
     glm::vec3 normal_to_side_local = glm::vec3(-d, 0.0f, 0.0f);
     float length = glm::length(originRay + normal_to_side_local);
     intersected_sides[length] = std::pair<int, glm::vec3>(CubeSide::CUBE_SIDE_RIGHT, normal_to_side_local);
    }

    // Левая грань куба
    z = originRay.z + dirRay.z * ( -d - originRay.x ) / dirRay.x;
    y = originRay.y + dirRay.y * ( -d - originRay.x ) / dirRay.x;
    if( ( z < d ) && ( z > -d ) && ( y < d ) && ( y > -d ) )
    {
     glm::vec3 normal_to_side_local = glm::vec3(d, 0.0f, 0.0f);
     float length = glm::length(originRay + normal_to_side_local);
     intersected_sides[length] = std::pair<int, glm::vec3>(CubeSide::CUBE_SIDE_LEFT, normal_to_side_local);
    }

    // Дальняя грань
    x = originRay.x + dirRay.x * ( d - originRay.z ) / dirRay.z;
    y = originRay.y + dirRay.y * ( d - originRay.z ) / dirRay.z;
    if( ( x < d ) && ( x > -d ) && ( y < d ) && ( y > -d ) )
    {
     glm::vec3 normal_to_side_local = glm::vec3(0.0f, 0.0f, d);
     float length = glm::length(originRay + normal_to_side_local);
     intersected_sides[length] = std::pair<int, glm::vec3>(CubeSide::CUBE_SIDE_FAR, normal_to_side_local);
    }

    // Ближняя грань
    x = originRay.x + dirRay.x * ( -d - originRay.z ) / dirRay.z;
    y = originRay.y + dirRay.y * ( -d - originRay.z ) / dirRay.z;
    if( ( x < d ) && ( x > -d ) && ( y < d ) && ( y > -d ) )
    {
     glm::vec3 normal_to_side_local = glm::vec3(0.0f, 0.0f, -d);
     float length = glm::length(originRay + normal_to_side_local);
     intersected_sides[length] = std::pair<int, glm::vec3>(CubeSide::CUBE_SIDE_NEAR, normal_to_side_local);
    }

    if(intersected_sides.empty())
     return false;

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
 glm::vec3 center(pose[3][0], pose[3][1], pose[3][2]);
 double size = Size;

 glm::mat4 invPose = glm::inverse(pose);
 glm::vec4 rel_position_vec4 = invPose * glm::vec4(position, 1.0f);
 glm::vec3 rel_position(rel_position_vec4.x, rel_position_vec4.y, rel_position_vec4.z);
 
 bool is_intersected = IsIntersectionCube(rel_position, front, size, intersected_sides);

 for(auto I = intersected_sides.begin(); I != intersected_sides.end(); ++I)
 {
  intersection_results[I->first + glm::length(rel_position)] = std::tuple<int, glm::vec3, glm::vec3>(I->second.first, I->second.second, I->second.second + center);
 }

 return is_intersected;
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

