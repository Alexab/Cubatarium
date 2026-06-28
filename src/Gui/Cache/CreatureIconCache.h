#ifndef CREATURE_ICON_CACHE_H
#define CREATURE_ICON_CACHE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UCreaturePreviewRenderer;

class UCreatureIconCache
{
public:
  explicit UCreatureIconCache(
      std::shared_ptr<UCreaturePreviewRenderer> preview);
  ~UCreatureIconCache();

  bool Initialize();
  void Shutdown();
  void ClearRenderedIcons();

  GLuint GetSpeciesIcon(const std::string &speciesId);
  GLuint GetSkinIcon(const std::string &skinId);
  void WarmupNext(size_t count);

private:
  GLuint GetOrCreateSpeciesIcon(const std::string &speciesId);
  GLuint GetOrCreateSkinIcon(const std::string &skinId);

  std::shared_ptr<UCreaturePreviewRenderer> Preview;

  std::unordered_map<std::string, GLuint> SpeciesCache;
  std::unordered_map<std::string, GLuint> SkinCache;
  std::vector<std::string> WarmupQueue;
  size_t WarmupIndex{0};

  static constexpr int kIconSize = 64;
  static constexpr float kIconYaw = 45.0f;
  static constexpr float kIconPitch = 32.0f;
};

} // namespace cutum

#endif
