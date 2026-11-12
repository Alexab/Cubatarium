#ifndef WORLD_ITEM_SYSTEM_H
#define WORLD_ITEM_SYSTEM_H

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace cutum
{

class UWorld;
class UGeometryEngine;

struct WorldItem
{
  int id{0};
  std::string itemId;
  int count{1};
  bool isBlock{false};
  glm::vec3 position{0.f};
  float age{0.f};
  float ttl{180.f};
};

class WorldItemSystem
{
public:
  static WorldItemSystem &Instance();

  int Spawn(const glm::vec3 &position, const std::string &itemId, int count,
            bool isBlock);
  void Tick(UWorld &world, float dt);
  void Draw(UWorld &world, UGeometryEngine &engine, const glm::mat4 &viewProj);
  void Clear();
  size_t Count() const { return Items.size(); }

private:
  std::vector<WorldItem> Items;
  int NextId{1};
};

} // namespace cutum

#endif
