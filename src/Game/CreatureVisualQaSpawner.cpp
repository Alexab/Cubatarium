#include "Game/CreatureVisualQaSpawner.h"
#include "Game/CreatureVisualQaPlacement.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraBasisLogic.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <filesystem>

namespace cutum
{

UCreatureVisualQaSpawner::UCreatureVisualQaSpawner(UWorld &world) : World(world)
{
  RefreshQueue();
}

void UCreatureVisualQaSpawner::RefreshQueue()
{
  Queue.clear();
  const auto storage = World.GetCreatureDefinitionStorage();
  if (storage)
  {
    const char *selected = std::getenv("CUBA_FLIGHT_QA_SPECIES");
    if (selected != nullptr && selected[0] != '\0')
    {
      std::istringstream stream(selected);
      std::string id;
      while (std::getline(stream, id, ','))
      {
        if (storage->Get(id) == nullptr)
        {
          throw std::runtime_error("Unknown QA creature: " + id);
        }
        Queue.push_back(id);
      }
      return;
    }
    const char *allIds = std::getenv("CUBA_FLIGHT_QA_ALL");
    if (allIds != nullptr && allIds[0] != '\0' && allIds[0] != '0')
    {
      Queue = storage->ListAllIds();
    }
    else
    {
      Queue = storage->ListSpawnable();
    }
  }
  if (Index >= Queue.size())
  {
    Index = 0;
  }
}

glm::vec3 UCreatureVisualQaSpawner::GridOriginForIndex(size_t index,
                                                       size_t total) const
{
  constexpr int kCols = 10;
  constexpr float kRowSpacing = 3.0f;
  constexpr float kColSpacing = 2.0f;
  const int row = static_cast<int>(index / kCols);
  const int col = static_cast<int>(index % kCols);
  const int colCenter = kCols / 2;

  glm::vec3 forward(0.0f, 0.0f, -1.0f);
  glm::vec3 right(1.0f, 0.0f, 0.0f);
  glm::vec3 base(0.0f, 0.0f, 0.0f);
  if (auto camera = World.GetCurrentUserCamera())
  {
    base = camera->GetPosition();
    glm::vec3 up;
    ComputeFpsCameraBasis(camera->GetYaw(), camera->GetPitch(), forward, right,
                          up);
    forward.y = 0.0f;
    right.y = 0.0f;
    const float forwardLen = glm::length(forward);
    const float rightLen = glm::length(right);
    if (forwardLen > 1e-5f)
    {
      forward /= forwardLen;
    }
    if (rightLen > 1e-5f)
    {
      right /= rightLen;
    }
  }

  const float depth = 4.0f + static_cast<float>(row) * kRowSpacing;
  const float lateral = static_cast<float>(col - colCenter) * kColSpacing;
  (void)total;
  return base + forward * depth + right * lateral;
}

CreatureVisualQaSpawnResult UCreatureVisualQaSpawner::SpawnNextSpecies()
{
  CreatureVisualQaSpawnResult result;
  RefreshQueue();
  if (Queue.empty())
  {
    result.Message = "QA spawn: no spawnable species";
    return result;
  }

  if (LastSpawnedId != 0)
  {
    World.RemoveCreature(LastSpawnedId);
    LastSpawnedId = 0;
  }

  const std::string &species = Queue[Index];
  const size_t displayIndex = Index + 1;
  Index = (Index + 1) % Queue.size();

  CreatureId maxId = 0;
  World.ForEachCreature([&](const UCreature &creature)
                        { maxId = std::max(maxId, creature.GetId()); });

  if (World.SpawnCreatureByView(species))
  {
    World.ForEachCreature(
        [&](const UCreature &creature)
        {
          if (creature.GetId() > maxId)
          {
            LastSpawnedId = creature.GetId();
          }
        });
    result.Ok = true;
    std::ostringstream oss;
    oss << "[" << displayIndex << "/" << Queue.size() << "] " << species;
    result.Message = oss.str();
    return result;
  }

  std::ostringstream oss;
  oss << "[" << displayIndex << "/" << Queue.size() << "] " << species
      << " skip: " << World.GetCreatureSpawnBlockedHint(species);
  result.Message = oss.str();
  return result;
}

CreatureVisualQaSpawnResult UCreatureVisualQaSpawner::SpawnAllInGrid()
{
  CreatureVisualQaSpawnResult result;
  RefreshQueue();
  if (Queue.empty())
  {
    result.Message = "QA batch: no spawnable species";
    return result;
  }

  if (const char *reset = std::getenv("CUBA_FLIGHT_QA_RESET");
      reset != nullptr && reset[0] == '1')
  {
    std::vector<CreatureId> oldQa;
    World.ForEachCreature([&](const UCreature &creature)
    {
      if (creature.GetSpawnSource() == CreatureSpawnSource::Qa)
      {
        oldQa.push_back(creature.GetId());
      }
    });
    for (CreatureId id : oldQa) { World.RemoveCreature(id); }
  }

  int spawned = 0;
  int skipped = 0;
  std::vector<glm::vec3> waterSlots;
  for (size_t i = 0; i < Queue.size(); ++i)
  {
    const std::string &species = Queue[i];
    glm::vec3 origin = GridOriginForIndex(i, Queue.size());
    const auto *definition = World.GetCreatureDefinitionStorage()->Get(species);
    if (std::getenv("CUBA_FLIGHT_QA_LAVA") != nullptr && definition &&
        definition->habitat == CreatureHabitat::Lava)
    {
      // A contained habitat fixture belongs only in an explicitly disposable
      // QA world. Never install it in the user's ordinary loaded world.
      if (World.GetWorldName().rfind("CreatureQaLava_",0) != 0 || Queue.size()!=1)
        throw std::runtime_error("Lava fixture requires one species in CreatureQaLava_* world");
      const auto lava=World.GetBlockRegistry().GetIdByTypeName("lava");
      const auto stone=World.GetBlockRegistry().GetIdByTypeName("stone");
      if (lava==BLOCK_AIR || stone==BLOCK_AIR)
        throw std::runtime_error("Lava QA fixture blocks are unavailable");
      const int cx=static_cast<int>(std::round(origin.x));
      const int cz=static_cast<int>(std::round(origin.z));
      int floor=static_cast<int>(std::floor(origin.y));
      for (int z=-6;z<=6;++z) for (int x=-6;x<=6;++x)
        if (const auto top=World.FindHighestSolidY(cx+x,cz+z)) floor=std::max(floor,*top+1);
      auto &blocks=World.GetBlockWorld();
      std::vector<glm::ivec3> changed;
      for (int z=-6;z<=6;++z) for (int x=-6;x<=6;++x)
      for (int y=0;y<=7;++y)
      {
        const glm::ivec3 pos(cx+x,floor+y,cz+z);
        const bool rim=std::abs(x)==6 || std::abs(z)==6;
        blocks.SetBlock(pos,y==0 || (rim && y==1) ? stone : y==1 ? lava : BLOCK_AIR);
        if (y==1 && !rim)
        {
          // A source-fed shallow flow at the centre exposes the creature's
          // face while retaining real lava physics and habitat sampling.
          const int level=std::clamp(3-std::max(std::abs(x),std::abs(z)),0,3);
          blocks.SetFluidState(pos,(level==0 ? FluidCellState::Source() :
              FluidCellState::Flowing(static_cast<uint8_t>(level))).WithKind(FluidKind::Lava));
        }
        changed.push_back(pos);
      }
      std::unordered_set<glm::ivec3,IVec3Hash> chunks;
      World.GetMeshService().MarkBlocksChunkDirtyBatchFromEdit(
          blocks,&World.GetBlockRegistry(),changed,chunks,true,false);
      origin=AdjustSpawnBodyOrigin(World,*definition,glm::vec3(cx,floor+1,cz));
      if (!CanSpawnCreatureAt(World,*definition,origin))
        throw std::runtime_error("Lava fixture failed ordinary habitat/collision validation");
    }
    if (std::getenv("CUBA_FLIGHT_QA_WATER") != nullptr &&
        definition && definition->habitat == CreatureHabitat::Aquatic)
    {
      const auto water = FindCreatureQaWaterPosition(World, origin, waterSlots, definition->bounds.restSizeBlocks, definition);
      if (!water) { throw std::runtime_error("Aquatic QA: no suitable loaded water"); }
      origin = *water;
      waterSlots.push_back(origin);
    }
    else if (definition && definition->habitat == CreatureHabitat::Terrestrial)
    {
      const auto land = FindCreatureQaLandPosition(World,origin,*definition);
      if (!land) { throw std::runtime_error("Terrestrial QA: no collision-free loaded land"); }
      origin = *land;
    }
    const char *qaSkin = std::getenv("CUBA_FLIGHT_QA_SKIN");
    const CreatureId spawnedId=World.SpawnCreature(species, origin, qaSkin ? qaSkin : "", CreatureSpawnSource::Qa);
    if (spawnedId != 0)
    {
      ++spawned;
      if(const char *gear=std::getenv("CUBA_FLIGHT_QA_WORLD_GEAR");gear && std::string(gear)=="1")
      {
        if(Queue.size()!=1 || !definition || definition->visual.boneSkeleton.animationProfile!="humanoid")
          throw std::runtime_error("World gear QA requires one humanoid B actor");
        auto *actor=World.GetCreature(spawnedId);auto *items=World.GetItemDefinitionStorage();
        if(!actor || !items)throw std::runtime_error("World gear QA inventory unavailable");
        const char *selected=std::getenv("CUBA_FLIGHT_QA_EQUIPMENT_MATERIAL");
        const std::string material=selected?selected:"copper";
        if(material!="copper" && material!="iron" && material!="leather")throw std::runtime_error("Unknown world gear material");
        auto item=[](const std::string &id){InventoryEntryRef e;e.kind=InventoryEntryKind::Item;e.Id=id;e.count=1;e.empty=false;return e;};
        const char *slots[]={"head","chest","legs","feet"};
        auto &inventory=actor->GetInventory();
        for(size_t slot=0;slot<6;++slot)
          if(!inventory.EquipArmor(slot,item(material+"_"+slots[slot]),*items))throw std::runtime_error("World QA armor rejected");
        if(!inventory.AssignToHotbar(0,0,item("wood_sword")) || !inventory.SetActiveSlot(0,0) ||
            !inventory.EquipOffhand(item("wood_torch"),*items))throw std::runtime_error("World QA held items rejected");
        if(const char *directory=std::getenv("CUBA_FLIGHT_CAPTURE_DIR"))
        {
          const std::filesystem::path folder(directory);std::filesystem::create_directories(folder);
          nlohmann::json state;inventory.SerializeToJson(state);
          std::ofstream stream(folder/"qa_equipment_inventory.json");
          stream<<nlohmann::json({{"species",species},{"skin",actor->GetSkinId()},{"material",material},
              {"spawn_source","Qa"},{"inventory",state}}).dump(2)<<'\n';
          if(!stream)throw std::runtime_error("World QA equipment proof failed");
        }
      }
    }
    else
    {
      ++skipped;
    }
  }

  LastSpawnedId = 0;
  Index = 0;
  result.Ok = spawned > 0;
  std::ostringstream oss;
  oss << "batch: " << spawned << "/" << Queue.size() << " ok";
  if (skipped > 0)
  {
    oss << ", " << skipped << " skip";
  }
  result.Message = oss.str();
  return result;
}

} // namespace cutum
