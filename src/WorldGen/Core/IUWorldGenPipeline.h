#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include <functional>
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

using WorldGenColumnProgressFn = std::function<void(int done, int total)>;

class IUWorldGenPipeline
{
public:
  virtual ~IUWorldGenPipeline() = default;

  virtual void GenerateColumn(int worldX, int worldZ) = 0;
  virtual int SurfaceYAt(int worldX, int worldZ) const = 0;

  virtual glm::vec3 DefaultSpawnPosition(int worldX, int worldZ,
                                         float eyeHeight = 1.62f) const;
  glm::vec3 ResolvePlayerSpawnPosition(const UBlockWorld &world,
                                       UBlockRegistry &registry,
                                       int centerX = 0, int centerZ = 0,
                                       float eyeHeight = 1.62f) const;

  /// Cooperative spawn resolution (UX plan U2/S1): the historical one-shot
  /// scanned ~2.3k SurfaceYAt candidates in a single tick (multi-second
  /// main-thread stall). Call TickSpawnResolve once per frame; it returns
  /// true when the spawn point is decided (SpawnPointOut valid or fallback
  /// used). Candidates are probed nearest-first, budgeted per call.
  bool TickSpawnResolve(const UBlockWorld &world, UBlockRegistry &registry,
                        int centerX, int centerZ, int probesPerTick,
                        glm::vec3 &SpawnPointOut, float eyeHeight = 1.62f);
  /// True once a TickSpawnResolve session has concluded (result or
  /// fallback); safe to call before the first tick.
  bool SpawnResolveFinished() const { return SpawnResolveDone; }

  const ProceduralSettings &GetProceduralSettings() const { return Ctx.Settings; }
  virtual void GenerateSpawnPatch(int centerX, int centerZ, int radiusBlocks,
                                  WorldGenColumnProgressFn onProgress = nullptr);
  virtual void GenerateFullPatch(int centerX, int centerZ, int halfExtent,
                                 WorldGenColumnProgressFn onProgress = nullptr);

protected:
  explicit IUWorldGenPipeline(WorldGenContext ctx);
  WorldGenContext Ctx;

  // TickSpawnResolve session state (UX U2/S1).
  struct SpawnResolveCandidateEntry
  {
    int x;
    int z;
  };
  bool SpawnResolveStarted{false};
  bool SpawnResolveDone{false};
  std::vector<SpawnResolveCandidateEntry> SpawnResolveCandidates;
  size_t SpawnResolveCandidateIndex{0};
  float SpawnResolveEyeHeight{1.62f};
  const ProceduralSettings *SpawnResolveSettings{nullptr};
};

class UProceduralWorldGenFactory
{
public:
  static std::unique_ptr<IUWorldGenPipeline> Create(WorldGenContext ctx);
};

} // namespace cutum
