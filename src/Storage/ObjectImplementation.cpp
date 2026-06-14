#include "Storage/ObjectImplementation.h"

namespace cutum
{

UTerrainPlane::UTerrainPlane()
{
  Width = 30;
  Height = 30;
  Generate();
}

UTerrainPlane::UTerrainPlane(int width, int height)
{
  Width = width;
  Height = height;
}

void UTerrainPlane::Generate() { Generate(2); }

void UTerrainPlane::Generate(size_t type_id)
{
  Cubes.resize(Width * Height);
  size_t k = 0;
  for (int i = -Width / 2; i < Width / 2; i++)
  {
    for (int j = -Height / 2; j < Height / 2; j++)
    {
      glm::mat4 pose = glm::mat4(1.0f);
      pose = glm::translate(pose, glm::vec3(i * 1.0f, 0.0f, j * 1.0f));
      Cubes[k] = NewCube();
      Cubes[k]->Init(pose);
      Cubes[k]->SetTypeId(type_id);
      ++k;
    }
  }

  glm::mat4 pose = glm::mat4(1.0f);
  SetPose(pose);
}

std::shared_ptr<UObject> UTerrainPlane::New()
{
  return std::make_shared<UTerrainPlane>();
}

void UPerson::Generate()
{
  Cubes.resize(2);
  for (int i = 0; i < 2; i++)
  {
    glm::mat4 pose = glm::mat4(1.0f);
    pose = glm::translate(pose, glm::vec3(0.0f, i * 1.0f, 0.0f));
    Cubes[i] = NewCube();
    Cubes[i]->SetTypeId(1);
    Cubes[i]->Init(pose);
  }

  glm::mat4 pose = glm::mat4(1.0f);
  pose = glm::translate(pose, glm::vec3(0.0f, 1.0f, 0.0f));
  SetPose(pose);
}

std::shared_ptr<UObject> UPerson::New() { return std::make_shared<UPerson>(); }

URect::URect(int width, int height, int length)
    : Width(width), Height(height), Length(length)
{
}

void URect::Generate()
{
  Cubes.resize(Width * Height * Length);
  size_t k = 0;
  for (int n = -Length / 2; n < Length / 2; n++)
  {
    for (int i = -Width / 2; i < Width / 2; i++)
    {
      for (int j = -Height / 2; j < Height / 2; j++)
      {
        glm::mat4 pose = glm::mat4(1.0f);
        pose = glm::translate(pose, glm::vec3(i * 1.0f, n * 1.0f, j * 1.0f));
        Cubes[k] = NewCube();
        Cubes[k]->Init(pose);
        Cubes[k]->SetTypeId(3);
        ++k;
      }
    }
  }

  glm::mat4 pose = glm::mat4(1.0f);
  pose = glm::translate(pose, glm::vec3(0.0f, 1.0f, 0.0f));
  SetPose(pose);
}

std::shared_ptr<UObject> URect::New()
{
  return std::make_shared<URect>(3, 3, 3);
}

USingleCube::USingleCube()
{
  Cubes.resize(1);
  glm::mat4 pose = glm::mat4(1.0f);
  Cubes[0] = NewCube();
  Cubes[0]->Init(pose);
  Cubes[0]->SetTypeId(3);
}

USingleCube::USingleCube(uint64_t object_type)
{
  Cubes.resize(1);
  glm::mat4 pose = glm::mat4(1.0f);
  Cubes[0] = NewCube();
  Cubes[0]->Init(pose);
  Cubes[0]->SetTypeId(object_type);
}

void USingleCube::Generate()
{
  Cubes.resize(1);
  glm::mat4 pose = glm::mat4(1.0f);
  Cubes[0] = NewCube();
  Cubes[0]->Init(pose);
  Cubes[0]->SetTypeId(3);
}

std::shared_ptr<UObject> USingleCube::New()
{
  return std::make_shared<USingleCube>();
}

} // namespace cutum
