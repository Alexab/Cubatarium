#include <iostream>
#include <sstream>
#include <QDebug>
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

const QMatrix4x4& Cube::GetObjectPose() const
{
 return ObjectPose;
}

const QMatrix4x4& Cube::GetInitialPose() const
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

void Cube::Init(const QMatrix4x4&initial_pose, float size)
{
 InitialPose = initial_pose;
 Size = size;
}

void Cube::SetObjectPose(const QMatrix4x4 &pose)
{
 ObjectPose = pose;
 UpdateVertices();
}

QVector3D Cube::GetCenterPosition() const
{
 QMatrix4x4 pose = ObjectPose*InitialPose;
 QVector3D a(pose(0,3), pose(1,3), pose(2,3));
 return a;
}

bool Cube::CheckCollision(Cube &cube)
{
 QMatrix4x4 pose1 = ObjectPose*InitialPose;
 QMatrix4x4 pose2 = cube.GetObjectPose()*cube.GetInitialPose();
 QVector3D a(pose1(0,3), pose1(1,3), pose1(2,3));
 QVector3D b(pose2(0,3), pose2(1,3), pose2(2,3));

 //check the X axis
 if(fabs(a.x() - b.x()) < Size/2.0 + cube.Size/2.0)
 {
  //check the Y axis
  if(fabs(a.y() - b.y()) < Size/2.0 + cube.Size/2.0)
  {
   //check the Z axis
   if(fabs(a.z() - b.z()) < Size/2.0 + cube.Size/2.0)
   {
    std::stringstream s;
    s << "Cube collision: (" << a.x() << "," << a.y() << "," << a.z() << ")" << " vs (" << b.x() << "," << b.y() << "," << b.z() << ")" <<std::endl;
    qDebug() << s.str().c_str();
    return true;
   }
  }
 }

 return false;
}

bool Cube::CheckCollision(const QVector3D& position, float size)
{
 QMatrix4x4 pose1 = ObjectPose*InitialPose;
 QVector3D a(pose1(0,3), pose1(1,3), pose1(2,3));
 QVector3D b(position);

 return Cube::CheckCollision(a, Size, b, size);
}

bool Cube::CheckCollision(const QVector3D& position1, float size1, const QVector3D& position2, float size2)
{
 QVector3D a(position1);
 QVector3D b(position2);

 //check the X axis
 if(fabs(a.x() - b.x()) < size1/2.0 + size2/2.0)
 {
  //check the Y axis
  if(fabs(a.y() - b.y()) < size1/2.0 + size2/2.0)
  {
   //check the Z axis
   if(fabs(a.z() - b.z()) < size1/2.0 + size2/2.0)
   {
    std::stringstream s;
    s << "Cube collision: (" << a.x() << "," << a.y() << "," << a.z() << ")" << " vs (" << b.x() << "," << b.y() << "," << b.z() << ")" <<std::endl;
    qDebug() << s.str().c_str();
    return true;
   }
  }
 }

 return false;
}

bool Cube::IsIntersectionCube( const QVector3D& originRay, const QVector3D& dirRay, float sizeOfSide, std::map<float, std::pair<int,QVector3D>> &intersected_sides) const
{
    float d = sizeOfSide / 2;
    float x, z, y;

    intersected_sides.clear();

    // Верхняя сторона
    x = originRay.x() + dirRay.x() * ( d - originRay.y() ) / dirRay.y();
    z = originRay.z() + dirRay.z() * ( d - originRay.y() ) / dirRay.y();
    if( ( x < d ) && ( x > -d ) && ( z < d ) && ( z > -d ) )
    {
     QVector3D normal_to_side_local=QVector3D(0.0f, d, 0.0f);
     float length = (originRay+normal_to_side_local).length();
     intersected_sides[length] = std::pair<int, QVector3D>(CubeSide::CUBE_SIDE_TOP,normal_to_side_local);
    }

    // Нижняя сторона
    x = originRay.x() + dirRay.x() * ( -d - originRay.y() ) / dirRay.y();
    z = originRay.z() + dirRay.z() * ( -d - originRay.y() ) / dirRay.y();
    if( ( x < d ) && ( x > -d ) && ( z < d ) && ( z > -d ) )
    {
     QVector3D normal_to_side_local=QVector3D(0.0f, -d, 0.0f);
     float length = (originRay+normal_to_side_local).length();
     intersected_sides[length] = std::pair<int, QVector3D>(CubeSide::CUBE_SIDE_BOTTOM,normal_to_side_local);
    }

    // Левая боковая сторона
    z = originRay.z() + dirRay.z() * ( d - originRay.x() ) / dirRay.x();
    y = originRay.y() + dirRay.y() * ( d - originRay.x() ) / dirRay.x();
    if( ( z < d ) && ( z > -d ) && ( y < d ) && ( y > -d ) )
    {
     QVector3D normal_to_side_local=QVector3D(-d, 0.0f, 0.0f);
     float length = (originRay+normal_to_side_local).length();
     intersected_sides[length] = std::pair<int, QVector3D>(CubeSide::CUBE_SIDE_RIGHT,normal_to_side_local);
    }

    // Правая боковая сторона
    z = originRay.z() + dirRay.z() * ( -d - originRay.x() ) / dirRay.x();
    y = originRay.y() + dirRay.y() * ( -d - originRay.x() ) / dirRay.x();
    if( ( z < d ) && ( z > -d ) && ( y < d ) && ( y > -d ) )
    {
     QVector3D normal_to_side_local=QVector3D(d, 0.0f, 0.0f);
     float length = (originRay+normal_to_side_local).length();
     intersected_sides[length] = std::pair<int, QVector3D>(CubeSide::CUBE_SIDE_LEFT,normal_to_side_local);
    }

    // Передняя сторона
    x = originRay.x() + dirRay.x() * ( d - originRay.z() ) / dirRay.z();
    y = originRay.y() + dirRay.y() * ( d - originRay.z() ) / dirRay.z();
    if( ( x < d ) && ( x > -d ) && ( y < d ) && ( y > -d ) )
    {
     QVector3D normal_to_side_local=QVector3D(0.0f, 0.0f, d);
     float length = (originRay+normal_to_side_local).length();
     intersected_sides[length] = std::pair<int, QVector3D>(CubeSide::CUBE_SIDE_FAR,normal_to_side_local);
    }

    // Задняя сторона
    x = originRay.x() + dirRay.x() * ( -d - originRay.z() ) / dirRay.z();
    y = originRay.y() + dirRay.y() * ( -d - originRay.z() ) / dirRay.z();
    if( ( x < d ) && ( x > -d ) && ( y < d ) && ( y > -d ) )
    {
     QVector3D normal_to_side_local=QVector3D(0.0f, 0.0f, -d);
     float length = (originRay+normal_to_side_local).length();
     intersected_sides[length] = std::pair<int, QVector3D>(CubeSide::CUBE_SIDE_NEAR,normal_to_side_local);
    }

    if(intersected_sides.empty())
     return false;

    return true;
}

bool Cube::IsIntersectionCube( const QVector3D& originRay, const QVector3D& dirRay, float sizeOfSide, int &side, QVector3D& normal, float &distance) const
{
 std::map<float, std::pair<int,QVector3D>> intersected_sides;
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
bool Cube::CheckRayIntersection(const QVector3D& position, const QVector3D& front, std::map<float, std::tuple<int, QVector3D, QVector3D>> &intersection_results) const
{
 intersection_results.clear();

 std::map<float, std::pair<int,QVector3D>> intersected_sides;
 QMatrix4x4 pose = ObjectPose*InitialPose;
 QVector3D center(pose(0,3), pose(1,3), pose(2,3));
 double size = Size;

 QVector3D rel_position = pose.inverted()*position;
 bool is_intersected=IsIntersectionCube(rel_position, front, size, intersected_sides);

 for(auto I=intersected_sides.begin();I != intersected_sides.end();++I)
 {
  intersection_results[I->first+rel_position.length()] = std::tuple<int, QVector3D, QVector3D>(I->second.first, I->second.second, I->second.second+center);
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

