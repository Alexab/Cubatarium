#include "GuiIconSource.h"
#include "TextureCube.h"

namespace cutum {

GuiIconSource::GuiIconSource(std::shared_ptr<TextureCubeStorage> textures,
                             std::unique_ptr<PrefabIconCache> prefabCache,
                             std::unique_ptr<CreatureIconCache> creatureCache)
    : textures_(std::move(textures))
    , prefabCache_(std::move(prefabCache))
    , creatureCache_(std::move(creatureCache))
{
}

GLuint GuiIconSource::GetBlockIconTexture(const std::string& blockName)
{
    if (!prefabCache_) {
        if (!textures_ || blockName.empty()) {
            return 0;
        }
        const auto& texMap = textures_->GetTextures();
        for (const auto& kv : texMap) {
            if (kv.second.GetName() == blockName) {
                return kv.second.GetTexture();
            }
        }
        return 0;
    }
    return prefabCache_->GetBlockIconTexture(blockName);
}

GLuint GuiIconSource::GetPrefabIconTexture(const std::string& prefabName)
{
    if (!prefabCache_ || prefabName.empty()) {
        return 0;
    }
    return prefabCache_->GetIcon(prefabName);
}

GLuint GuiIconSource::GetPrefabIconTextureIfCached(const std::string& prefabName) const
{
    if (!prefabCache_ || prefabName.empty()) {
        return 0;
    }
    return prefabCache_->GetIconIfCached(prefabName);
}

GLuint GuiIconSource::GetCreatureIconTexture(const std::string& speciesId)
{
    if (!creatureCache_ || speciesId.empty()) {
        return 0;
    }
    return creatureCache_->GetSpeciesIcon(speciesId);
}

GLuint GuiIconSource::GetSkinIconTexture(const std::string& skinId)
{
    if (!creatureCache_ || skinId.empty()) {
        return 0;
    }
    return creatureCache_->GetSkinIcon(skinId);
}

void GuiIconSource::WarmupCreatureIcons(size_t maxPerFrame)
{
    if (creatureCache_) {
        creatureCache_->WarmupNext(maxPerFrame);
    }
}

void GuiIconSource::WarmupPrefabIcons(size_t maxPerFrame)
{
    if (prefabCache_) {
        prefabCache_->WarmupNext(maxPerFrame);
    }
}

} // namespace cutum
