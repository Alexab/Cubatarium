#ifndef OBJECTIMPLEMENTATION_H
#define OBJECTIMPLEMENTATION_H

#include "Object.h"

namespace cutum {

class Terrain: public Object
{
public:
 Terrain() = default;
 virtual void Generate()=0;
};

class TerrainPlane: public Terrain
{
public:
 TerrainPlane();
 TerrainPlane(int width, int height);
 virtual void Generate();
 virtual void Generate(size_t type_id);

 virtual std::shared_ptr<Object> New();

private:
 int Width, Height;
};


class Person: public Object
{
public:
 virtual void Generate();

 virtual std::shared_ptr<Object> New();
};

class Rect: public Object
{
public:
 Rect(int width, int height, int length);
 virtual void Generate();

 virtual std::shared_ptr<Object> New();
private:
 int Width, Height, Length;
};

class SingleCube: public Object
{
public:
 SingleCube();
 SingleCube(uint64_t object_type);
 virtual void Generate();

 virtual std::shared_ptr<Object> New();
};

}

#endif // OBJECTIMPLEMENTATION_H
