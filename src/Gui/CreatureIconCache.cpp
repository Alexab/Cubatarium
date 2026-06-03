#include "CreatureIconCache.h"
#include "CreatureCatalogTypes.h"
#include "CreatureDefinitionStorage.h"
#include <glm/glm.hpp>
#include "CreatureTextureStorage.h"
#include "SkinDefinitionStorage.h"

#include <GL/glew.h>
#include <iostream>

namespace cutum {

CreatureIconCache::CreatureIconCache(std::shared_ptr<CreatureDefinitionStorage> species,
                                     std::shared_ptr<SkinDefinitionStorage> skins,
                                     std::shared_ptr<CreatureTextureStorage> textures)
    : species_(std::move(species))
    , skins_(std::move(skins))
    , textures_(std::move(textures))
{
}

CreatureIconCache::~CreatureIconCache()
{
    Shutdown();
}

bool CreatureIconCache::Initialize()
{
    glGenFramebuffers(1, &fbo_);
    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconSize, kIconSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (species_) {
        warmupQueue_ = species_->ListSpawnable();
        for (const std::string& id : species_->ListAllIds()) {
            if (const CreatureDefinition* def = species_->Get(id)) {
                if (def->role == CreatureRole::ControlledDefault) {
                    warmupQueue_.push_back(id);
                }
            }
        }
    }
    if (skins_) {
        for (const std::string& id : skins_->ListEquippable()) {
            warmupQueue_.push_back("skin:" + id);
        }
    }
    warmupIndex_ = 0;
    return complete;
}

void CreatureIconCache::Shutdown()
{
    for (const auto& entry : speciesCache_) {
        if (entry.second != 0) {
            glDeleteTextures(1, &entry.second);
        }
    }
    for (const auto& entry : skinCache_) {
        if (entry.second != 0 && entry.second != colorTex_) {
            glDeleteTextures(1, &entry.second);
        }
    }
    speciesCache_.clear();
    skinCache_.clear();
    if (colorTex_ != 0) {
        glDeleteTextures(1, &colorTex_);
        colorTex_ = 0;
    }
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}

GLuint CreatureIconCache::RenderSolidColorIcon(float r, float g, float b, float a)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glViewport(0, 0, kIconSize, kIconSize);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint CreatureIconCache::GetOrCreateSpeciesIcon(const std::string& speciesId)
{
    const auto it = speciesCache_.find(speciesId);
    if (it != speciesCache_.end()) {
        return it->second;
    }
    glm::vec4 color{0.5f, 0.5f, 0.5f, 1.0f};
    if (species_) {
        if (const CreatureDefinition* def = species_->Get(speciesId)) {
            color = def->visual.wireframeColor;
        }
    }
    const GLuint tex = RenderSolidColorIcon(color.r, color.g, color.b, color.a);
    speciesCache_[speciesId] = tex;
    return tex;
}

GLuint CreatureIconCache::GetOrCreateSkinIcon(const std::string& skinId)
{
    const auto it = skinCache_.find(skinId);
    if (it != skinCache_.end()) {
        return it->second;
    }
    if (textures_) {
        const GLuint existing = textures_->GetTexture("skin/" + skinId + "/diffuse");
        if (existing != 0) {
            skinCache_[skinId] = existing;
            return existing;
        }
    }
    glm::vec4 color{0.7f, 0.7f, 0.7f, 1.0f};
    if (skins_) {
        if (const SkinDefinition* def = skins_->Get(skinId)) {
            color = def->iconFallbackColor;
        }
    }
    const GLuint tex = RenderSolidColorIcon(color.r, color.g, color.b, color.a);
    skinCache_[skinId] = tex;
    return tex;
}

GLuint CreatureIconCache::GetSpeciesIcon(const std::string& speciesId)
{
    if (speciesId.empty()) {
        return 0;
    }
    return GetOrCreateSpeciesIcon(speciesId);
}

GLuint CreatureIconCache::GetSkinIcon(const std::string& skinId)
{
    if (skinId.empty()) {
        return 0;
    }
    return GetOrCreateSkinIcon(skinId);
}

void CreatureIconCache::WarmupNext(size_t count)
{
    for (size_t i = 0; i < count && warmupIndex_ < warmupQueue_.size(); ++i, ++warmupIndex_) {
        const std::string& key = warmupQueue_[warmupIndex_];
        if (key.rfind("skin:", 0) == 0) {
            GetOrCreateSkinIcon(key.substr(5));
        } else {
            GetOrCreateSpeciesIcon(key);
        }
    }
}

} // namespace cutum
