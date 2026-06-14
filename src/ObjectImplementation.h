#ifndef OBJECTIMPLEMENTATION_H
#define OBJECTIMPLEMENTATION_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Object.h"

namespace cutum {

class Terrain: public UObject
{
public:
 Terrain() = default;
 virtual void Generate()=0;
};

class UTerrainPlane: public Terrain
{
public:
 UTerrainPlane();
 UTerrainPlane(int width, int height);
 virtual void Generate();
 virtual void Generate(size_t type_id);

 virtual std::shared_ptr<UObject> New();

private:
 int Width, Height;
};


class UPerson: public UObject
{
public:
 virtual void Generate();

 virtual std::shared_ptr<UObject> New();
};

class URect: public UObject
{
public:
 URect(int width, int height, int length);
 virtual void Generate();

 virtual std::shared_ptr<UObject> New();
private:
 int Width, Height, Length;
};

class USingleCube: public UObject
{
public:
 USingleCube();
 USingleCube(uint64_t object_type);
 virtual void Generate();

 virtual std::shared_ptr<UObject> New();
};

}

#endif // OBJECTIMPLEMENTATION_H
