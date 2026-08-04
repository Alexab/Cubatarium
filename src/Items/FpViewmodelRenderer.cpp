#include "Items/FpViewmodelRenderer.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace cutum
{

namespace
{
constexpr float kFovDeg = 50.0f;

glm::vec3 ReadVec3(const nlohmann::json &arr, const glm::vec3 &fallback)
{
  if (!arr.is_array() || arr.size() < 3)
  {
    return fallback;
  }
  return glm::vec3(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>());
}
} // namespace

UFpViewmodelRenderer::UFpViewmodelRenderer(
    std::shared_ptr<UItemDefinitionStorage> items,
    std::shared_ptr<UShaderManager> shaderManager)
    : Items(std::move(items)), ShaderManager(std::move(shaderManager))
{
}

UFpViewmodelRenderer::~UFpViewmodelRenderer() { Shutdown(); }

bool UFpViewmodelRenderer::Initialize()
{
  if (!ShaderManager)
  {
    return false;
  }
  Shader = ShaderManager->CreateShader("fp_viewmodel", "shaders/vshader.glsl",
                                       "shaders/fshader.glsl");
  if (!Shader || !Shader->IsValid())
  {
    return false;
  }
  if (!InitCubeMesh())
  {
    return false;
  }
  SkinTex = SolidTex(210, 160, 120);
  SleeveTex = SolidTex(55, 70, 110);
  MetalTex = SolidTex(180, 185, 195);
  WoodTex = SolidTex(158, 107, 56);
  return EnsureFbo(256);
}

void UFpViewmodelRenderer::Shutdown()
{
  auto delTex = [](GLuint &t) {
    if (t != 0)
    {
      glDeleteTextures(1, &t);
      t = 0;
    }
  };
  if (DepthRbo != 0)
  {
    glDeleteRenderbuffers(1, &DepthRbo);
    DepthRbo = 0;
  }
  delTex(ColorTex);
  if (Fbo != 0)
  {
    glDeleteFramebuffers(1, &Fbo);
    Fbo = 0;
  }
  FboSize = 0;
  delTex(SkinTex);
  delTex(SleeveTex);
  delTex(MetalTex);
  delTex(WoodTex);
  if (CubeEbo != 0)
  {
    glDeleteBuffers(1, &CubeEbo);
    CubeEbo = 0;
  }
  if (CubeVbo != 0)
  {
    glDeleteBuffers(1, &CubeVbo);
    CubeVbo = 0;
  }
  if (CubeVao != 0)
  {
    glDeleteVertexArrays(1, &CubeVao);
    CubeVao = 0;
  }
  Shader.reset();
}

bool UFpViewmodelRenderer::InitCubeMesh()
{
  if (CubeVao != 0)
  {
    return true;
  }
  const float cubeShift = 1.0f / 6.0f;
  const float vertices[] = {
      -0.5f, -0.5f, 0.5f,  0.0f, 1.0f,  0.5f,  -0.5f, 0.5f,  cubeShift, 1.0f,
      -0.5f, 0.5f,  0.5f,  0.0f, 0.0f,  0.5f,  0.5f,  0.5f,  cubeShift, 0.0f,
      0.5f,  -0.5f, 0.5f,  cubeShift, 1.0f,  0.5f,  -0.5f, -0.5f, cubeShift * 2.0f,
      1.0f,  0.5f,  0.5f,  0.5f,  cubeShift, 0.0f,  0.5f,  0.5f,  -0.5f, cubeShift * 2.0f,
      0.0f,  0.5f,  -0.5f, -0.5f, cubeShift * 2.0f, 1.0f, -0.5f, -0.5f, -0.5f,
      cubeShift * 3.0f, 1.0f, 0.5f, 0.5f, -0.5f, cubeShift * 2.0f, 0.0f, 0.5f, 0.5f,
      -0.5f, cubeShift * 3.0f, 0.0f, -0.5f, -0.5f, -0.5f, cubeShift * 3.0f, 1.0f,
      -0.5f, -0.5f, 0.5f,  cubeShift * 4.0f, 1.0f, -0.5f, 0.5f, -0.5f, cubeShift * 3.0f,
      0.0f,  -0.5f, 0.5f,  0.5f, cubeShift * 4.0f, 0.0f, -0.5f, 0.5f,  0.5f,
      cubeShift * 4.0f, 0.0f, 0.5f,  0.5f, 0.5f, cubeShift * 5.0f, 0.0f, -0.5f, 0.5f,
      -0.5f, cubeShift * 4.0f, 1.0f, 0.5f, 0.5f, -0.5f, cubeShift * 5.0f, 1.0f,
      -0.5f, -0.5f, -0.5f, cubeShift * 5.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f, 0.0f,
      -0.5f, -0.5f, 0.5f,  cubeShift * 5.0f, 1.0f, 0.5f, -0.5f, 0.5f, 1.0f, 1.0f,
  };
  const unsigned int indices[] = {
      0,  1,  2,  2,  1,  3,  4,  5,  6,  6,  5,  7,  8,  9,  10, 10, 9,  11,
      12, 13, 14, 14, 13, 15, 16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23,
  };
  glGenVertexArrays(1, &CubeVao);
  glGenBuffers(1, &CubeVbo);
  glGenBuffers(1, &CubeEbo);
  glBindVertexArray(CubeVao);
  glBindBuffer(GL_ARRAY_BUFFER, CubeVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, CubeEbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  return true;
}

bool UFpViewmodelRenderer::EnsureFbo(int size)
{
  const int clamped = std::max(128, std::min(size, 512));
  if (Fbo != 0 && FboSize == clamped)
  {
    return true;
  }
  if (DepthRbo != 0)
  {
    glDeleteRenderbuffers(1, &DepthRbo);
    DepthRbo = 0;
  }
  if (ColorTex != 0)
  {
    glDeleteTextures(1, &ColorTex);
    ColorTex = 0;
  }
  if (Fbo != 0)
  {
    glDeleteFramebuffers(1, &Fbo);
    Fbo = 0;
  }
  glGenFramebuffers(1, &Fbo);
  glGenTextures(1, &ColorTex);
  glBindTexture(GL_TEXTURE_2D, ColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, clamped, clamped, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ColorTex, 0);
  glGenRenderbuffers(1, &DepthRbo);
  glBindRenderbuffer(GL_RENDERBUFFER, DepthRbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, clamped, clamped);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            DepthRbo);
  const bool ok =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  FboSize = ok ? clamped : 0;
  return ok;
}

GLuint UFpViewmodelRenderer::SolidTex(unsigned char r, unsigned char g,
                                      unsigned char b)
{
  const unsigned char rgba[4] = {r, g, b, 255};
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

std::vector<UFpViewmodelRenderer::Part> UFpViewmodelRenderer::ArmParts() const
{
  // Right-arm viewmodel (camera looks toward -Z); boxes in front of camera.
  return {
      Part{0.28f, -0.55f, -0.55f, 0.2f, 0.55f, 0.2f, 55, 70, 110},   // sleeve
      Part{0.34f, -0.22f, -0.38f, 0.16f, 0.16f, 0.16f, 210, 160, 120}, // hand
  };
}

std::vector<UFpViewmodelRenderer::Part>
UFpViewmodelRenderer::ToolParts(const std::string &itemId) const
{
  std::vector<Part> parts;
  if (!Items || itemId.empty())
  {
    return parts;
  }
  const auto *def = Items->Get(itemId);
  if (def && !def->ModelPath.empty())
  {
    IUPlatformPaths *paths = IUPlatformPaths::TryGet();
    std::string jsonText;
    if (paths && paths->ReadAssetText(def->ModelPath, jsonText))
    {
      try
      {
        const nlohmann::json data = nlohmann::json::parse(jsonText);
        if (data.contains("parts") && data["parts"].is_array())
        {
          for (const auto &partJson : data["parts"])
          {
            const glm::vec3 off =
                ReadVec3(partJson.value("offset", nlohmann::json::array()),
                         glm::vec3(0.f));
            const glm::vec3 sz =
                ReadVec3(partJson.value("size", nlohmann::json::array()),
                         glm::vec3(0.2f));
            // Place near hand, scale down for viewmodel.
            Part p;
            p.ox = 0.42f + off.x * 0.55f;
            p.oy = -0.05f + off.y * 0.55f;
            p.oz = -0.28f + off.z * 0.55f;
            p.sx = std::max(0.04f, sz.x * 0.55f);
            p.sy = std::max(0.04f, sz.y * 0.55f);
            p.sz = std::max(0.04f, sz.z * 0.55f);
            p.r = 185;
            p.g = 190;
            p.b = 200;
            if (itemId.find("wood") != std::string::npos)
            {
              p.r = 158;
              p.g = 107;
              p.b = 56;
            }
            else if (itemId.find("stone") != std::string::npos)
            {
              p.r = 140;
              p.g = 140;
              p.b = 132;
            }
            parts.push_back(p);
          }
        }
      }
      catch (...)
      {
      }
    }
  }
  if (parts.empty())
  {
    parts.push_back(
        Part{0.45f, 0.0f, -0.3f, 0.08f, 0.35f, 0.08f, 185, 190, 200});
  }
  return parts;
}

void UFpViewmodelRenderer::DrawParts(const std::vector<Part> &parts,
                                     const glm::mat4 &mvpBase)
{
  glBindVertexArray(CubeVao);
  for (const Part &p : parts)
  {
    GLuint tex = MetalTex;
    if (p.g > 140 && p.b < 140)
    {
      tex = SkinTex;
    }
    else if (p.b > 90 && p.r < 90)
    {
      tex = SleeveTex;
    }
    else if (p.r > 140 && p.g < 120)
    {
      tex = WoodTex;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex != 0 ? tex : MetalTex);
    const glm::mat4 model =
        glm::translate(glm::mat4(1.f), glm::vec3(p.ox, p.oy, p.oz)) *
        glm::scale(glm::mat4(1.f), glm::vec3(p.sx, p.sy, p.sz));
    Shader->SetMat4("mvp_matrix", mvpBase * model);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
}

GLuint UFpViewmodelRenderer::RenderFrame(const InventoryEntryRef *active,
                                         int size)
{
  if (!Shader || !EnsureFbo(size) || FboSize <= 0)
  {
    return 0;
  }
  UGlStateScope glState(kGlMaskIconFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glViewport(0, 0, FboSize, FboSize);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0.f, 0.f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const glm::mat4 projection =
      glm::perspective(glm::radians(kFovDeg), 1.0f, 0.05f, 20.0f);
  const glm::mat4 view =
      glm::lookAt(glm::vec3(0.05f, 0.05f, 0.15f), glm::vec3(0.3f, -0.25f, -0.45f),
                  glm::vec3(0.f, 1.f, 0.f));
  const glm::mat4 mvpBase = projection * view;

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);

  DrawParts(ArmParts(), mvpBase);
  if (active && !active->empty && active->kind == InventoryEntryKind::Item &&
      !active->Id.empty() && !active->broken)
  {
    DrawParts(ToolParts(active->Id), mvpBase);
  }

  Shader->Unuse();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return ColorTex;
}

void UFpViewmodelRenderer::DrawOverlay(UGuiRenderer &renderer,
                                       const GuiTheme &theme,
                                       const InventoryEntryRef *active,
                                       int framebuffer_w, int framebuffer_h)
{
  const int size = std::max(160, theme.HotbarSlotSize * 4);
  const GLuint tex = RenderFrame(active, size);
  if (tex == 0)
  {
    return;
  }
  const int drawSize = std::max(140, theme.HotbarSlotSize * 3);
  const int x = framebuffer_w - drawSize - theme.Padding;
  const int y = framebuffer_h - drawSize - theme.Padding * 2;
  renderer.DrawTexturedRect({x, y, drawSize, drawSize}, tex);
}

} // namespace cutum
