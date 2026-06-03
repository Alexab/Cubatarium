#ifndef CREATURE_ICON_CACHE_H
#define CREATURE_ICON_CACHE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum {

class CreatureDefinitionStorage;
class CreatureTextureStorage;
class SkinDefinitionStorage;

class CreatureIconCache {
public:
    CreatureIconCache(std::shared_ptr<CreatureDefinitionStorage> species,
                      std::shared_ptr<SkinDefinitionStorage> skins,
                      std::shared_ptr<CreatureTextureStorage> textures);
    ~CreatureIconCache();

    bool Initialize();
    void Shutdown();

    GLuint GetSpeciesIcon(const std::string& speciesId);
    GLuint GetSkinIcon(const std::string& skinId);
    void WarmupNext(size_t count);

private:
    GLuint RenderSolidColorIcon(float r, float g, float b, float a);
    GLuint GetOrCreateSpeciesIcon(const std::string& speciesId);
    GLuint GetOrCreateSkinIcon(const std::string& skinId);

    std::shared_ptr<CreatureDefinitionStorage> species_;
    std::shared_ptr<SkinDefinitionStorage> skins_;
    std::shared_ptr<CreatureTextureStorage> textures_;

    std::unordered_map<std::string, GLuint> speciesCache_;
    std::unordered_map<std::string, GLuint> skinCache_;
    std::vector<std::string> warmupQueue_;
    size_t warmupIndex_{0};

    GLuint fbo_{0};
    GLuint colorTex_{0};
    static constexpr int kIconSize = 64;
};

} // namespace cutum

#endif
