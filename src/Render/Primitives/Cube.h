#ifndef CUBE_H
#define CUBE_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#include <memory>
#include <tuple>

namespace cutum
{

enum CubeSide
{
  CUBE_SIDE_NEAR = 0,
  CUBE_SIDE_RIGHT,
  CUBE_SIDE_FAR,
  CUBE_SIDE_LEFT,
  CUBE_SIDE_TOP,
  CUBE_SIDE_BOTTOM
};

class UCube
{
public:
  UCube();
  UCube(const UCube &copy);
  UCube &operator=(const UCube &copy);
  virtual void Init(const glm::mat4 &initial_pose, float size = 1.0);
  virtual void SetObjectPose(const glm::mat4 &pose);
  virtual bool CheckCollision(UCube &cube);
  virtual bool CheckCollision(const glm::vec3 &position, float size = 1.0);

  static bool CheckCollision(const glm::vec3 &position1, float size1,
                             const glm::vec3 &position2, float size2);
  static bool CheckAabbCollision(const glm::vec3 &c1, const glm::vec3 &h1,
                                 const glm::vec3 &c2, const glm::vec3 &h2);

  bool IsIntersectionCube(
      const glm::vec3 &originRay, const glm::vec3 &dirRay, float sizeOfSide,
      std::map<float, std::pair<int, glm::vec3>> &intersected_sides) const;
  bool IsIntersectionCube(const glm::vec3 &originRay, const glm::vec3 &dirRay,
                          const float sizeOfSide, int &side, glm::vec3 &normal,
                          float &distance) const;

  virtual bool
  CheckRayIntersection(const glm::vec3 &position, const glm::vec3 &front,
                       std::map<float, std::tuple<int, glm::vec3, glm::vec3>>
                           &intersection_results) const;
  // virtual bool CheckRayIntersection(const glm::vec3& position, const
  // glm::vec3& front, glm::vec3& intersecion, float &distance, int &side)
  // const;

  const glm::mat4 &GetObjectPose() const;
  const glm::mat4 &GetInitialPose() const;
  glm::vec3 GetCenterPosition() const;
  float GetSize() const;
  void SetSize(float size);
  size_t GetTypeId() const;
  void SetTypeId(size_t value);

  void Copy(const UCube &copy);
  virtual void Copy(std::shared_ptr<UCube> copy);

protected:
  virtual void UpdateVertices() = 0;

protected:
  glm::mat4 ObjectPose;
  glm::mat4 InitialPose;
  float Size;

protected:
  size_t TypeId;
};

extern std::shared_ptr<UCube> NewCube();

} // namespace cutum
#endif // CUBE_H
