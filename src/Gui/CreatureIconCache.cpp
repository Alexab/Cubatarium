#include "CreatureIconCache.h"
#include "CreaturePartMeshData.h"
#include "CreatureAppearance.h"
#include "CreatureCatalogTypes.h"
#include "CreatureDefinitionStorage.h"
#include "CreatureTextureStorage.h"
#include "ShaderManager.h"
#include "SkinDefinitionStorage.h"

#include "render/GlStateScope.h"
#include "render/GlStateMask.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace cutum {

UCreatureIconCache::UCreatureIconCache(std::shared_ptr<UCreatureDefinitionStorage> species,
                                     std::shared_ptr<USkinDefinitionStorage> skins,
                                     std::shared_ptr<UCreatureTextureStorage> textures,
                                     std::shared_ptr<UShaderManager> shader_manager)
    : species_(std::move(species))
    , skins_(std::move(skins))
    , textures_(std::move(textures))
    , ShaderManager(std::move(shader_manager))
{
}

UCreatureIconCache::~UCreatureIconCache()
{
    Shutdown();
}

namespace {

void UploadIconCubeMesh(GLuint& vao, GLuint& vbo, GLuint& ebo, const float* texCoords)
{
    float vertices[24 * 5];
    for (int v = 0; v < 24; ++v) {
        vertices[v * 5 + 0] = kCreaturePartPositions[v * 3 + 0];
        vertices[v * 5 + 1] = kCreaturePartPositions[v * 3 + 1];
        vertices[v * 5 + 2] = kCreaturePartPositions[v * 3 + 2];
        vertices[v * 5 + 3] = texCoords[v * 2 + 0];
        vertices[v * 5 + 4] = texCoords[v * 2 + 1];
    }
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCreaturePartIndices), kCreaturePartIndices,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

} // namespace

bool UCreatureIconCache::InitCubeMesh()
{
    if (cubeVao_ != 0) {
        return true;
    }
    float boxUv[48];
    float headUv[48];
    float bodyUv[48];
    BuildCreatureBoxTexCoords(boxUv);
    BuildCreatureHeadTexCoords(headUv);
    BuildCreatureBodyTexCoords(bodyUv);
    UploadIconCubeMesh(cubeVao_, cubeVbo_, cubeEbo_, boxUv);
    UploadIconCubeMesh(headCubeVao_, headCubeVbo_, headCubeEbo_, headUv);
    UploadIconCubeMesh(bodyCubeVao_, bodyCubeVbo_, bodyCubeEbo_, bodyUv);
    return cubeVao_ != 0 && headCubeVao_ != 0 && bodyCubeVao_ != 0;
}

bool UCreatureIconCache::Initialize()
{
    if (!ShaderManager) {
        return false;
    }
    shader_ = ShaderManager->CreateShader("creature_icon", "shaders/vshader.glsl",
                                           "shaders/fshader.glsl");
    if (!shader_ || !shader_->IsValid() || !InitCubeMesh()) {
        return false;
    }

    glGenFramebuffers(1, &fbo_);
    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconSize, kIconSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

    glGenRenderbuffers(1, &depthRbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kIconSize, kIconSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);

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

void UCreatureIconCache::Shutdown()
{
    for (const auto& entry : speciesCache_) {
        if (entry.second != 0) {
            glDeleteTextures(1, &entry.second);
        }
    }
    for (const auto& entry : skinCache_) {
        if (entry.second == 0) {
            continue;
        }
        if (textures_) {
            const GLuint diffuse = textures_->GetTexture("skin/" + entry.first + "/diffuse");
            if (entry.second == diffuse) {
                continue;
            }
        }
        glDeleteTextures(1, &entry.second);
    }
    speciesCache_.clear();
    skinCache_.clear();
    shader_.reset();
    if (depthRbo_ != 0) {
        glDeleteRenderbuffers(1, &depthRbo_);
        depthRbo_ = 0;
    }
    if (colorTex_ != 0) {
        glDeleteTextures(1, &colorTex_);
        colorTex_ = 0;
    }
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (bodyCubeEbo_ != 0) {
        glDeleteBuffers(1, &bodyCubeEbo_);
        bodyCubeEbo_ = 0;
    }
    if (bodyCubeVbo_ != 0) {
        glDeleteBuffers(1, &bodyCubeVbo_);
        bodyCubeVbo_ = 0;
    }
    if (bodyCubeVao_ != 0) {
        glDeleteVertexArrays(1, &bodyCubeVao_);
        bodyCubeVao_ = 0;
    }
    if (headCubeEbo_ != 0) {
        glDeleteBuffers(1, &headCubeEbo_);
        headCubeEbo_ = 0;
    }
    if (headCubeVbo_ != 0) {
        glDeleteBuffers(1, &headCubeVbo_);
        headCubeVbo_ = 0;
    }
    if (headCubeVao_ != 0) {
        glDeleteVertexArrays(1, &headCubeVao_);
        headCubeVao_ = 0;
    }
    if (cubeEbo_ != 0) {
        glDeleteBuffers(1, &cubeEbo_);
        cubeEbo_ = 0;
    }
    if (cubeVbo_ != 0) {
        glDeleteBuffers(1, &cubeVbo_);
        cubeVbo_ = 0;
    }
    if (cubeVao_ != 0) {
        glDeleteVertexArrays(1, &cubeVao_);
        cubeVao_ = 0;
    }
}

GLuint UCreatureIconCache::RenderSolidColorIcon(float r, float g, float b, float a)
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

GLuint UCreatureIconCache::RenderSpeciesPartsIcon(const std::string& speciesId)
{
    if (!species_ || !skins_ || !textures_ || !shader_ || fbo_ == 0) {
        return 0;
    }
    const CreatureDefinition* def = species_->Get(speciesId);
    if (!def) {
        return 0;
    }

    const ResolvedCreatureAppearance appearance =
        ResolveCreatureAppearance(*species_, *skins_, speciesId, "");

    GLuint iconTex = 0;
    glGenTextures(1, &iconTex);
    glBindTexture(GL_TEXTURE_2D, iconTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconSize, kIconSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    UGlStateScope glState(kGlMaskIconFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, iconTex, 0);
    glViewport(0, 0, kIconSize, kIconSize);
    glEnable(GL_DEPTH_TEST);
    const glm::vec4 bg = def->visual.wireframeColor * 0.25f;
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float fitHeight = std::max(def->bounds.restSizeBlocks.y, 0.5f);
    const float fitScale = 1.6f / fitHeight;
    const glm::mat4 projection = glm::perspective(glm::radians(28.0f), 1.0f, 0.1f, 30.0f);
    const glm::mat4 view =
        glm::lookAt(glm::vec3(1.8f, 1.4f, 1.8f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    shader_->Use();
    shader_->SetInt("texture0", 0);
    shader_->SetInt("uAnimFrame", 0);
    shader_->SetInt("uAnimFrameCount", 1);
    for (const ResolvedCreaturePart& part : appearance.parts) {
        const GLuint tex = textures_->GetTexture(part.textureAssetKey);
        if (tex == 0) {
            continue;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        GLuint partVao = cubeVao_;
        if (part.partId == "head") {
            partVao = headCubeVao_;
        } else if (part.partId == "torso") {
            partVao = bodyCubeVao_;
        }
        glBindVertexArray(partVao);
        const glm::vec3 local = part.offsetBlocks * fitScale;
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), local) *
                                glm::scale(glm::mat4(1.0f), part.sizeBlocks * fitScale);
        shader_->SetMat4("mvp_matrix", projection * view * model);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    shader_->Unuse();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return iconTex;
}

GLuint UCreatureIconCache::GetOrCreateSpeciesIcon(const std::string& speciesId)
{
    const auto it = speciesCache_.find(speciesId);
    if (it != speciesCache_.end()) {
        return it->second;
    }
    GLuint tex = RenderSpeciesPartsIcon(speciesId);
    if (tex == 0) {
        glm::vec4 color{0.5f, 0.5f, 0.5f, 1.0f};
        if (species_) {
            if (const CreatureDefinition* def = species_->Get(speciesId)) {
                color = def->visual.wireframeColor;
            }
        }
        tex = RenderSolidColorIcon(color.r, color.g, color.b, color.a);
    }
    speciesCache_[speciesId] = tex;
    return tex;
}

GLuint UCreatureIconCache::GetOrCreateSkinIcon(const std::string& skinId)
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

GLuint UCreatureIconCache::GetSpeciesIcon(const std::string& speciesId)
{
    if (speciesId.empty()) {
        return 0;
    }
    return GetOrCreateSpeciesIcon(speciesId);
}

GLuint UCreatureIconCache::GetSkinIcon(const std::string& skinId)
{
    if (skinId.empty()) {
        return 0;
    }
    return GetOrCreateSkinIcon(skinId);
}

void UCreatureIconCache::WarmupNext(size_t count)
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
