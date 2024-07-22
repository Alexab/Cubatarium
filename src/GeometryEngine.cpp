
#include <QVector2D>
#include <QVector3D>
#include <QPainter>
#include <QCoreApplication>
#include "GeometryEngine.h"
#include "Core.h"
#include "ObjectImplementation.h"
#include "User.h"
#include "ObjectStorage.h"
#include "Camera.h"

namespace cutum {

GeometryEngine::GeometryEngine(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<World> world, std::shared_ptr<TextureBaseStorage> texture_base_storage, std::shared_ptr<TextureCubeStorage> texture_cube_storage)
 : ObjectStorageInstance(object_storage)
 , WorldInstance(world)
 , TextureBaseStorageInstance(texture_base_storage)
 , TextureCubeStorageInstance(texture_cube_storage)
{
}

GeometryEngine::~GeometryEngine()
{

}

bool GeometryEngine::InitEngine()
{
 initializeOpenGLFunctions();
 if(!InitShaders())
  return false;
 //this->setSceneRadius(10000.0);
 //this->camera()->setZNearCoefficient(0.00001);
 //this->camera()->setZClippingCoefficient(1000.0);

 //TextureCubeStorageInstance->GenerateCubeTextures();

 return true;
}

bool GeometryEngine::InitShaders()
{
 // Compile vertex shader
 if (!program.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/vshader.glsl"))
  return false;

 // Compile fragment shader
 if (!program.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/fshader.glsl"))
  return false;

 //if (!program.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/fshader_mix_textures.glsl"))
 // return false;

 // Link shader pipeline
 if (!program.link())
  return false;

 //if(!program.bind())
 // return false;

 return true;
}

void GeometryEngine::SetPainter(std::shared_ptr<QPainter> painter)
{
 Painter = painter;
}

void GeometryEngine::Paint(int width_size, int height_size)
{
 Painter->beginNativePainting();
 DrawCubeGeometry();
 Painter->endNativePainting();

 Painter->setRenderHint(QPainter::Antialiasing);
 QPen pen;
 pen.setColor(Qt::yellow);
 pen.setWidth(2);
 Painter->setPen(pen);
 QFont textFont;
 textFont.setPixelSize(20);
 Painter->setFont(textFont);
 Painter->setBrush(QBrush(Qt::yellow));
 QString title = QString("Cubatarium version ")+ QCoreApplication::applicationVersion();
 Painter->drawText(QRect(30, 0, width_size, 30), Qt::AlignLeft, title);

 std::string legend = std::string("Press [0-9] for object selection. Left click for put, long left click (or Del) for remove object. F12 - reset world");
 Painter->drawText(QRect(30, height_size-30, width_size, height_size), Qt::AlignHCenter, legend.c_str());

 Painter->drawLine(width_size/2-10, height_size/2, width_size/2+10, height_size/2);
 Painter->drawLine(width_size/2, height_size/2-10, width_size/2, height_size/2+10);
}

void GeometryEngine::DrawCubeGeometry()
{
 DrawCubeGeometry(WorldInstance->GetObjects(), WorldInstance->GetCurrentUserCamera()->GetMvpMatrix(),
                              WorldInstance->GetIsIntersectionExists(),
                              WorldInstance->GetIntersectionObjectIndex(),
                              WorldInstance->GetIntersectionCubeIndex());
}

void GeometryEngine::DrawCube(std::shared_ptr<Cube> icube, std::shared_ptr<QOpenGLTexture> & texture)
{
 auto cube = std::dynamic_pointer_cast<CubeGL>(icube);
 if(!cube)
  return;

 texture->bind();
 program.setUniformValue("texture0", 0);


 // Tell OpenGL which VBOs to use
 cube->GetArrayBuf().bind();
 cube->GetIndexBuf().bind();

 // Offset for position
 quintptr offset = 0;

 // Tell OpenGL programmable pipeline how to locate vertex position data
 int vertexLocation = program.attributeLocation("a_position");
 program.enableAttributeArray(vertexLocation);
 program.setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 3, sizeof(VertexData));

 // Offset for texture coordinate
 offset += sizeof(QVector3D);

 // Tell OpenGL programmable pipeline how to locate vertex texture coordinate data
 int texcoordLocation = program.attributeLocation("a_texcoord");
 program.enableAttributeArray(texcoordLocation);
 program.setAttributeBuffer(texcoordLocation, GL_FLOAT, offset, 2, sizeof(VertexData));

 // Draw cube geometry using indices from VBO 1
 glDrawElements(GL_TRIANGLE_STRIP,int(std::dynamic_pointer_cast<CubeGL>(cube)->GetIndices().size()), GL_UNSIGNED_SHORT, nullptr);
 cube->GetArrayBuf().release();
 cube->GetIndexBuf().release();

 texture->release();
}

void GeometryEngine::DrawObject(std::shared_ptr<Object> object, size_t object_index, const std::map<size_t, TextureCube>& textures, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index)
{
 for(size_t i=0; i<object->GetCubes().size(); i++)
 {
  auto & cube = object->GetCubes()[i];
  std::shared_ptr<QOpenGLTexture> texture;
  if(is_intersection_exists && intersecion_object_index == object_index && intersecion_cube_index == i)
  {
   texture = textures.at(TextureCubeStorageInstance->GetTypeIdByName("selection")).GetTexture();
  }
  else
  {
   texture = textures.at(cube->GetTypeId()).GetTexture();
  }
  DrawCube(cube, texture);
 }

}

void GeometryEngine::DrawCubeGeometry(const std::vector<std::shared_ptr<Object>>& objects, const QMatrix4x4& mvp_matrix, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index)
{
 // Enable depth buffer
 glEnable(GL_DEPTH_TEST);

 // Enable back face culling
 glEnable(GL_CULL_FACE);

 // Bind shader pipeline for use
 program.bind();
 // Clear color and depth buffer
 glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

 // Set modelview-projection matrix
 program.setUniformValue("mvp_matrix", mvp_matrix);

 //glUniform1f(alphaUniformLocation, alpha);

 //Get locations of fragment shader uniforms to be tied to UI
 //alphaUniformLocation = glGetUniformLocation(program.programId(), "alpha");

 auto textures = TextureCubeStorageInstance->GetTextures();

 for(size_t j=0; j<objects.size(); j++)
 {
  auto &object = objects[j];
  DrawObject(object, j, textures, is_intersection_exists, intersecion_object_index, intersecion_cube_index);
 }

 auto user = WorldInstance->GetCurrentUser();
 if(user)
 {
  auto active_object_type_name = user->GetActiveObjectTypeName();
  auto active_object = ObjectStorageInstance->TakeObject(active_object_type_name);//GetPrototype(active_object_type_name).GetSample();
  if(active_object)
  {
   // Set modelview-projection matrix for user plane
   QMatrix4x4 pose;
   pose.setToIdentity();
   active_object->GetCubes()[0]->Init(pose, 0.2f);

   pose(0,3) = 0.8f;
   pose(1,3) = 0.8f;
   pose(2,3) = 0.0f;
   active_object->SetPose(pose);

   QMatrix4x4 position;
   position.setToIdentity();
   program.setUniformValue("mvp_matrix", position);

   DrawObject(active_object, 0, textures, false, 0, 0);

 //  std::shared_ptr<QOpenGLTexture> texture = textures[active_cube->GetTypeId()].GetTexture();
 //  DrawCube(active_cube, texture);
  }
 }
 program.release();

 glDisable(GL_CULL_FACE);
 glDisable(GL_DEPTH_TEST);
}

}
