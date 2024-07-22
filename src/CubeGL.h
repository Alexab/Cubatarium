#ifndef CUBEGL_H
#define CUBEGL_H

#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include <QVector3D>
#include <QVector2D>
#include "Cube.h"


namespace cutum {

struct VertexData
{
 QVector3D position;
 QVector2D texCoord;

 VertexData() = default;
 VertexData(const VertexData &) = default;
 VertexData& operator = (const VertexData &) = default;
 VertexData(QVector3D position, QVector2D texCoord);
};

class CubeGL: public Cube
{
public:
 CubeGL();
 CubeGL(const CubeGL &copy);
 CubeGL& operator = (const CubeGL &copy);

 ~CubeGL();

 void Init(const QMatrix4x4 &initial_pose, float size=1.0);
 void UpdateVertices();

 std::vector<VertexData> GetVertices() const;
 std::vector<VertexData> GetVerticesInitialPos() const;
 std::vector<GLushort> GetIndices() const;
 QOpenGLBuffer& GetArrayBuf();
 QOpenGLBuffer& GetIndexBuf();

private:
 std::vector<VertexData> VerticesInitialPos;
 std::vector<VertexData> Vertices;
 std::vector<GLushort> Indices;
 QOpenGLBuffer arrayBuf;
 QOpenGLBuffer indexBuf;
};

extern std::shared_ptr<Cube> NewCube();

}

#endif // CUBEGL_H
