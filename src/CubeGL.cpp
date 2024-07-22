#include "CubeGL.h"

namespace cutum {

VertexData::VertexData(QVector3D position, QVector2D texCoord)
 : position(position), texCoord(texCoord)
{
}

CubeGL::CubeGL()
: Cube(), indexBuf(QOpenGLBuffer::IndexBuffer)
{
 arrayBuf.create();
 indexBuf.create();
}

CubeGL::CubeGL(const CubeGL &copy)
 : Cube(copy), indexBuf(QOpenGLBuffer::IndexBuffer)
{
 arrayBuf.create();
 indexBuf.create();

 Vertices = copy.Vertices;
 Indices = copy.Indices;

 arrayBuf.bind();
 arrayBuf.allocate(Vertices.data(), int(Vertices.size() * sizeof(VertexData)));
 arrayBuf.release();

 // Transfer index data to VBO 1
 indexBuf.bind();
 indexBuf.allocate(Indices.data(), int(Indices.size() * sizeof(GLushort)));
 indexBuf.release();
}

CubeGL& CubeGL::operator = (const CubeGL &copy)
{
 Cube::operator = (copy);
 Vertices = copy.Vertices;
 Indices = copy.Indices;

 arrayBuf.bind();
 arrayBuf.allocate(Vertices.data(), int(Vertices.size() * sizeof(VertexData)));
 arrayBuf.release();

 indexBuf.bind();
 indexBuf.allocate(Indices.data(), int(Indices.size() * sizeof(GLushort)));
 indexBuf.release();
 return *this;
}

CubeGL::~CubeGL()
{
 arrayBuf.destroy();
 indexBuf.destroy();
}

void CubeGL::Init(const QMatrix4x4 &initial_pose, float size)
{
 Cube::Init(initial_pose, size);
 float size2 = Size / 2.0;

 float cube_shift = 1.0f/6.0f;

 // Vertex data for face 0 (CUBE_SIDE_NEAR)
 VerticesInitialPos.clear();
 VerticesInitialPos.emplace_back(QVector3D(-size2, -size2,  size2), QVector2D(0.0f, 0.0f)); // v0
 VerticesInitialPos.emplace_back(QVector3D( size2, -size2,  size2), QVector2D(cube_shift*1.0f, 0.0f)); // v1
 VerticesInitialPos.emplace_back(QVector3D(-size2,  size2,  size2), QVector2D(0.0f, 1.0f)); // v2
 VerticesInitialPos.emplace_back(QVector3D( size2,  size2,  size2), QVector2D(cube_shift*1.0f, 1.0f)); // v3

 // Vertex data for face 1 (CUBE_SIDE_RIGHT)
 VerticesInitialPos.emplace_back(QVector3D( size2, -size2,  size2), QVector2D(cube_shift*1.0f, 0.0f)); // v4
 VerticesInitialPos.emplace_back(QVector3D( size2, -size2, -size2), QVector2D(cube_shift*2.0f, 0.0f)); // v5
 VerticesInitialPos.emplace_back(QVector3D( size2,  size2,  size2), QVector2D(cube_shift*1.0f, 1.0f)); // v6
 VerticesInitialPos.emplace_back(QVector3D( size2,  size2, -size2), QVector2D(cube_shift*2.0f, 1.0f)); // v7

 // Vertex data for face 2 (CUBE_SIDE_FAR)
 VerticesInitialPos.emplace_back(QVector3D( size2, -size2, -size2), QVector2D(cube_shift*2.0f, 0.0f)); // v8
 VerticesInitialPos.emplace_back(QVector3D(-size2, -size2, -size2), QVector2D(cube_shift*3.0f, 0.0f)); // v9
 VerticesInitialPos.emplace_back(QVector3D( size2,  size2, -size2), QVector2D(cube_shift*2.0f, 1.0f)); // v10
 VerticesInitialPos.emplace_back(QVector3D(-size2,  size2, -size2), QVector2D(cube_shift*3.0f, 1.0f)); // v11

 // Vertex data for face 3 (CUBE_SIDE_LEFT)
 VerticesInitialPos.emplace_back(QVector3D(-size2, -size2, -size2), QVector2D(cube_shift*3.0f, 0.0f)); // v12
 VerticesInitialPos.emplace_back(QVector3D(-size2, -size2,  size2), QVector2D(cube_shift*4.0f, 0.0f)); // v13
 VerticesInitialPos.emplace_back(QVector3D(-size2,  size2, -size2), QVector2D(cube_shift*3.0f, 1.0f)); // v14
 VerticesInitialPos.emplace_back(QVector3D(-size2,  size2,  size2), QVector2D(cube_shift*4.0f, 1.0f)); // v15

 // Vertex data for face 4 (CUBE_SIDE_TOP)
 VerticesInitialPos.emplace_back(QVector3D(-size2,  size2,  size2), QVector2D(cube_shift*4.0f, 0.0f)); // v16
 VerticesInitialPos.emplace_back(QVector3D( size2,  size2,  size2), QVector2D(cube_shift*5.0f, 0.0f)); // v17
 VerticesInitialPos.emplace_back(QVector3D(-size2,  size2, -size2), QVector2D(cube_shift*4.0f, 1.0f)); // v18
 VerticesInitialPos.emplace_back(QVector3D( size2,  size2, -size2), QVector2D(cube_shift*5.0f, 1.0f)); // v19

 // Vertex data for face 5 (CUBE_SIDE_BOTTOM)
 VerticesInitialPos.emplace_back(QVector3D(-size2, -size2, -size2), QVector2D(cube_shift*5.0f, 0.0f)); // v16
 VerticesInitialPos.emplace_back(QVector3D( size2, -size2, -size2), QVector2D(1.0, 0.0f)); // v17
 VerticesInitialPos.emplace_back(QVector3D(-size2, -size2,  size2), QVector2D(cube_shift*5.0f, 1.0f)); // v18
 VerticesInitialPos.emplace_back(QVector3D( size2, -size2,  size2), QVector2D(1.0, 1.0f)); // v19

 for(auto & vert : VerticesInitialPos)
 {
  vert.position = initial_pose*vert.position;
 }

 Indices = {
  0,  1,  2,  3,  3,     // Face 0 - triangle strip ( v0,  v1,  v2,  v3)
  4,  4,  5,  6,  7,  7, // Face 1 - triangle strip ( v4,  v5,  v6,  v7)
  8,  8,  9, 10, 11, 11, // Face 2 - triangle strip ( v8,  v9, v10, v11)
 12, 12, 13, 14, 15, 15, // Face 3 - triangle strip (v12, v13, v14, v15)
 16, 16, 17, 18, 19, 19, // Face 4 - triangle strip (v16, v17, v18, v19)
 20, 20, 21, 22, 23      // Face 5 - triangle strip (v20, v21, v22, v23)
};

 // Transfer index data to VBO 1
 indexBuf.bind();
 indexBuf.allocate(Indices.data(), int(Indices.size() * sizeof(GLushort)));
 indexBuf.release();

 UpdateVertices();
}

void CubeGL::UpdateVertices()
{
 Vertices.resize(VerticesInitialPos.size());
 for(size_t i=0;i<Vertices.size();i++)
 {
  Vertices[i].position = ObjectPose*VerticesInitialPos[i].position;
  Vertices[i].texCoord = VerticesInitialPos[i].texCoord;
 }

 arrayBuf.bind();
 arrayBuf.allocate(Vertices.data(), int(Vertices.size() * sizeof(VertexData)));
 arrayBuf.release();
}

std::vector<VertexData> CubeGL::GetVertices() const
{
 return Vertices;
}

std::vector<VertexData> CubeGL::GetVerticesInitialPos() const
{
 return VerticesInitialPos;
}

std::vector<GLushort> CubeGL::GetIndices() const
{
 return Indices;
}

QOpenGLBuffer& CubeGL::GetArrayBuf()
{
 return arrayBuf;
}

QOpenGLBuffer& CubeGL::GetIndexBuf()
{
 return indexBuf;
}

std::shared_ptr<Cube> NewCube()
{
 return std::make_shared<CubeGL>();
}

}
