#include "PrefabIconCache.h"

#include "BlockDefinitionStorage.h"
#include "BlockTypes.h"
#include "Prefab.h"
#include "ShaderManager.h"
#include "TextureCube.h"

#include "render/GlStateMask.h"
#include "render/GlStateScope.h"

#include <GL/glew.h>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace cutum {

UPrefabIconCache::UPrefabIconCache(std::shared_ptr<UPrefabLibrary> prefabs,
                                 std::shared_ptr<UTextureCubeStorage> textures,
                                 std::shared_ptr<UBlockDefinitionStorage> blockDefs,
                                 std::shared_ptr<UShaderManager> shader_manager)
    : prefabs_(std::move(prefabs))
    , textures_(std::move(textures))
    , blockDefs_(std::move(blockDefs))
    , ShaderManager(std::move(shader_manager))
{
}

UPrefabIconCache::~UPrefabIconCache()
{
    Shutdown();
}

bool UPrefabIconCache::Initialize()
{
    if (!ShaderManager || !prefabs_) {
        return false;
    }
    shader_ = ShaderManager->CreateShader("gui_prefab_icon", "shaders/vshader.glsl",
                                           "shaders/fshader.glsl");
    if (!shader_ || !shader_->IsValid()) {
        return false;
    }
    if (!InitCubeMesh()) {
        return false;
    }

    glGenFramebuffers(1, &fbo_);
    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconSize, kIconSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

    glGenRenderbuffers(1, &depthRbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kIconSize, kIconSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    warmupQueue_ = prefabs_->ListNames();
    warmupIndex_ = 0;
    return complete;
}

void UPrefabIconCache::Shutdown()
{
    for (const auto& entry : cache_) {
        if (entry.second != 0 && entry.second != colorTex_) {
            glDeleteTextures(1, &entry.second);
        }
    }
    for (const auto& entry : blockCache_) {
        if (entry.second != 0 && entry.second != colorTex_) {
            glDeleteTextures(1, &entry.second);
        }
    }
    cache_.clear();
    blockCache_.clear();

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
    shader_.reset();
}

bool UPrefabIconCache::InitCubeMesh()
{
    if (cubeVao_ != 0) {
        return true;
    }
    const float cubeShift = 1.0f / 6.0f;
    const float vertices[] = {
        -0.5f, -0.5f, 0.5f,  0.0f,            1.0f, 0.5f, -0.5f,  0.5f, cubeShift, 1.0f,
        -0.5f,  0.5f, 0.5f,  0.0f,            0.0f, 0.5f,  0.5f,  0.5f, cubeShift, 0.0f,
        0.5f, -0.5f,  0.5f,  cubeShift,       1.0f, 0.5f, -0.5f, -0.5f, cubeShift * 2.0f, 1.0f,
        0.5f,  0.5f,  0.5f,  cubeShift,       0.0f, 0.5f,  0.5f, -0.5f, cubeShift * 2.0f, 0.0f,
        0.5f, -0.5f, -0.5f, cubeShift * 2.0f, 1.0f,-0.5f, -0.5f, -0.5f, cubeShift * 3.0f, 1.0f,
        0.5f,  0.5f, -0.5f, cubeShift * 2.0f, 0.0f, 0.5f,  0.5f, -0.5f, cubeShift * 3.0f, 0.0f,
        -0.5f,-0.5f, -0.5f, cubeShift * 3.0f, 1.0f,-0.5f, -0.5f,  0.5f, cubeShift * 4.0f, 1.0f,
        -0.5f, 0.5f, -0.5f, cubeShift * 3.0f, 0.0f,-0.5f,  0.5f,  0.5f, cubeShift * 4.0f, 0.0f,
        -0.5f, 0.5f,  0.5f, cubeShift * 4.0f, 0.0f, 0.5f,  0.5f,  0.5f, cubeShift * 5.0f, 0.0f,
        -0.5f, 0.5f, -0.5f, cubeShift * 4.0f, 1.0f, 0.5f,  0.5f, -0.5f, cubeShift * 5.0f, 1.0f,
        -0.5f,-0.5f, -0.5f, cubeShift * 5.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f,              0.0f,
        -0.5f,-0.5f,  0.5f, cubeShift * 5.0f, 1.0f, 0.5f, -0.5f,  0.5f, 1.0f,              1.0f,
    };
    const unsigned int indices[] = {
        0,  1,  2,  2,  1,  3,  4,  5,  6,  6,  5,  7,  8,  9, 10, 10,  9, 11,
        12, 13, 14, 14, 13, 15, 16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23,
    };

    glGenVertexArrays(1, &cubeVao_);
    glGenBuffers(1, &cubeVbo_);
    glGenBuffers(1, &cubeEbo_);
    glBindVertexArray(cubeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return true;
}

GLuint UPrefabIconCache::GetBlockTexture(BlockId blockId) const
{
    if (!blockDefs_ || !textures_ || blockId == BLOCK_AIR) {
        return 0;
    }
    const BlockDefinition* def = blockDefs_->GetById(blockId);
    if (!def) {
        return 0;
    }
    const auto& texMap = textures_->GetTextures();
    for (const auto& kv : texMap) {
        if (kv.second.GetName() == def->name) {
            return kv.second.GetTexture();
        }
    }
    return 0;
}

GLuint UPrefabIconCache::GetBlockIconTexture(const std::string& blockName)
{
    if (!blockDefs_ || !textures_ || !shader_ || blockName.empty()) {
        return 0;
    }
    const BlockDefinition* def = blockDefs_->GetByName(blockName);
    if (!def || def->id == BLOCK_AIR) {
        return 0;
    }
    const BlockId id = def->id;
    if (auto it = blockCache_.find(id); it != blockCache_.end()) {
        return it->second;
    }

    const GLuint tex = RenderBlockIcon(id);
    if (tex != 0) {
        blockCache_[id] = tex;
    }
    return tex;
}

GLuint UPrefabIconCache::RenderPrefabIcon(const std::string& prefabName)
{
    const Prefab* prefab = prefabs_ ? prefabs_->Get(prefabName) : nullptr;
    if (!prefab || prefab->voxels.empty() || !shader_ || fbo_ == 0) {
        return 0;
    }

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
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::vec3 minB(1e6f);
    glm::vec3 maxB(-1e6f);
    for (const PrefabVoxel& voxel : prefab->voxels) {
        if (voxel.id == BLOCK_AIR) {
            continue;
        }
        const glm::vec3 p(voxel.offset);
        minB = glm::min(minB, p);
        maxB = glm::max(maxB, p);
    }
    const glm::vec3 center = (minB + maxB) * 0.5f;
    const glm::vec3 size = maxB - minB;
    const float extent = std::max({size.x, size.y, size.z});
    const float fitScale = extent > 0.01f ? 1.8f / extent : 1.0f;

    const glm::mat4 projection =
        glm::perspective(glm::radians(35.0f), 1.0f, 0.1f, 50.0f);
    const glm::mat4 view =
        glm::lookAt(glm::vec3(2.2f, 2.0f, 2.2f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    shader_->Use();
    shader_->SetInt("texture0", 0);
    shader_->SetInt("uAnimFrame", 0);
    shader_->SetInt("uAnimFrameCount", 1);
    glBindVertexArray(cubeVao_);

    for (const PrefabVoxel& voxel : prefab->voxels) {
        if (voxel.id == BLOCK_AIR) {
            continue;
        }
        const GLuint tex = GetBlockTexture(voxel.id);
        if (tex == 0) {
            continue;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);

        const glm::vec3 local = (glm::vec3(voxel.offset) - center) * fitScale;
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), local) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(0.88f));
        const glm::mat4 mvp = projection * view * model;
        shader_->SetMat4("mvp_matrix", mvp);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    shader_->Unuse();

    return iconTex;
}

GLuint UPrefabIconCache::RenderBlockIcon(BlockId blockId)
{
    const GLuint tex = GetBlockTexture(blockId);
    if (tex == 0 || !shader_ || fbo_ == 0) {
        return 0;
    }

    GLuint iconTex = 0;
    glGenTextures(1, &iconTex);
    glBindTexture(GL_TEXTURE_2D, iconTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconSize, kIconSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    UGlStateScope glState(kGlMaskIconFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, iconTex,
                           0);
    glViewport(0, 0, kIconSize, kIconSize);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 projection =
        glm::perspective(glm::radians(35.0f), 1.0f, 0.1f, 50.0f);
    glm::mat4 view =
        glm::lookAt(glm::vec3(2.2f, 2.0f, 2.2f), glm::vec3(0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f));

    shader_->Use();
    shader_->SetInt("texture0", 0);
    shader_->SetInt("uAnimFrame", 0);
    shader_->SetInt("uAnimFrameCount", 1);
    glBindVertexArray(cubeVao_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.88f));
    const glm::mat4 mvp = projection * view * model;
    shader_->SetMat4("mvp_matrix", mvp);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    shader_->Unuse();
    return iconTex;
}

GLuint UPrefabIconCache::GetIconIfCached(const std::string& prefabName) const
{
    const auto it = cache_.find(prefabName);
    return it != cache_.end() ? it->second : 0;
}

GLuint UPrefabIconCache::GetIcon(const std::string& prefabName)
{
    if (prefabName.empty()) {
        return 0;
    }
    if (const GLuint cached = GetIconIfCached(prefabName)) {
        return cached;
    }
    const GLuint tex = RenderPrefabIcon(prefabName);
    cache_[prefabName] = tex;
    return tex;
}

void UPrefabIconCache::WarmupNext(size_t count)
{
    for (size_t n = 0; n < count && warmupIndex_ < warmupQueue_.size(); ++n, ++warmupIndex_) {
        GetIcon(warmupQueue_[warmupIndex_]);
    }
}

} // namespace cutum
