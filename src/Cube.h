#ifndef CUBE_H
#define CUBE_H

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector2D>

namespace cutum {

enum CubeSide {
 CUBE_SIDE_NEAR = 0,
 CUBE_SIDE_RIGHT,
 CUBE_SIDE_FAR,
 CUBE_SIDE_LEFT,
 CUBE_SIDE_TOP,
 CUBE_SIDE_BOTTOM
};

class Cube
{
public:
 Cube();
 Cube(const Cube &copy);
 Cube& operator = (const Cube &copy);
 virtual void Init(const QMatrix4x4& initial_pose, float size=1.0);
 virtual void SetObjectPose(const QMatrix4x4 &pose);
 virtual bool CheckCollision(Cube &cube);
 virtual bool CheckCollision(const QVector3D& position, float size=1.0);

 static bool CheckCollision(const QVector3D& position1, float size1, const QVector3D& position2, float size2);

 bool IsIntersectionCube( const QVector3D& originRay, const QVector3D& dirRay, float sizeOfSide, std::map<float, std::pair<int,QVector3D>> &intersected_sides) const;
 bool IsIntersectionCube( const QVector3D& originRay, const QVector3D& dirRay, const float sizeOfSide, int &side, QVector3D& normal, float &distance) const;

 virtual bool CheckRayIntersection(const QVector3D& position, const QVector3D& front, std::map<float, std::tuple<int, QVector3D, QVector3D>> &intersection_results) const;
// virtual bool CheckRayIntersection(const QVector3D& position, const QVector3D& front, QVector3D& intersecion, float &distance, int &side) const;

 const QMatrix4x4& GetObjectPose() const;
 const QMatrix4x4& GetInitialPose() const;
 QVector3D GetCenterPosition() const;
 float GetSize() const;
 void SetSize(float size);
 size_t GetTypeId() const;
 void SetTypeId(size_t value);

 void Copy(const Cube &copy);
 virtual void Copy(std::shared_ptr<Cube> copy);

protected:
 virtual void UpdateVertices()=0;

protected:
 QMatrix4x4 ObjectPose;
 QMatrix4x4 InitialPose;
 float Size;

protected:
 size_t TypeId;
};

extern std::shared_ptr<Cube> NewCube();

}
#endif // CUBE_H
