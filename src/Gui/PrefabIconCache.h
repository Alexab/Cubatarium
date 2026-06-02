#ifndef PREFAB_ICON_CACHE_H
#define PREFAB_ICON_CACHE_H

#include "BlockTypes.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum {

class BlockDefinitionStorage;
class PrefabLibrary;
class ShaderManager;
class TextureCubeStorage;

class PrefabIconCache {
public:
    PrefabIconCache(std::shared_ptr<PrefabLibrary> prefabs,
                    std::shared_ptr<TextureCubeStorage> textures,
                    std::shared_ptr<BlockDefinitionStorage> blockDefs,
                    std::shared_ptr<ShaderManager> shaderManager);
    ~PrefabIconCache();

    bool Initialize();
    void Shutdown();

    GLuint GetIcon(const std::string& prefabName);
    GLuint GetIconIfCached(const std::string& prefabName) const;
    void WarmupNext(size_t count);

    GLuint GetBlockIconTexture(const std::string& blockName);

private:
    GLuint RenderPrefabIcon(const std::string& prefabName);
    GLuint RenderBlockIcon(BlockId blockId);
    GLuint GetBlockTexture(BlockId blockId) const;
    bool InitCubeMesh();

    std::shared_ptr<PrefabLibrary> prefabs_;
    std::shared_ptr<TextureCubeStorage> textures_;
    std::shared_ptr<BlockDefinitionStorage> blockDefs_;
    std::shared_ptr<ShaderManager> shaderManager_;
    std::shared_ptr<class ShaderProgram> shader_;

    std::unordered_map<std::string, GLuint> cache_;
    std::unordered_map<BlockId, GLuint> blockCache_;
    std::vector<std::string> warmupQueue_;
    size_t warmupIndex_{0};

    GLuint cubeVao_{0};
    GLuint cubeVbo_{0};
    GLuint cubeEbo_{0};
    GLuint fbo_{0};
    GLuint colorTex_{0};
    GLuint depthRbo_{0};
    static constexpr int kIconSize = 64;
};

} // namespace cutum

#endif
