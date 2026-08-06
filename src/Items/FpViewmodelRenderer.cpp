#include "Items/FpViewmodelRenderer.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemVisualDefaults.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Textures/TextureCube.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

namespace
{
constexpr float kFovDeg = 72.0f;
constexpr float kNear = 0.05f;
constexpr float kFar = 10.0f;
constexpr float kInertiaBaseX = 0.12f;
constexpr float kInertiaBaseY = -0.08f;
constexpr float kInertiaAmpX = 0.035f;
constexpr float kInertiaAmpY = 0.05f;
constexpr float kSwingSpeed = 3.5f;
constexpr float kBobSpeedScale = 2.0f;
constexpr float kPi = 3.14159265358979323846f;

const glm::vec3 kEye{0.f, 0.0f, 0.0f};
const glm::vec3 kTarget{0.05f, -0.15f, -1.0f};
// After idle Rx(+88°): local -Y = forward (-Z view); local -Z = up (+Y view).
// Previous +Z pushed the held item down under the wrist in frame.
const glm::vec3 kHandSocketR{0.04f, -0.62f, -0.14f};
const glm::vec3 kHandSocketL{-0.04f, -0.62f, -0.14f};

// Classic Steve rightArm UV origin on player_skin_atlas (see human geometry).
constexpr int kArmUvU = 40;
constexpr int kArmUvV = 16;
constexpr int kArmUvW = 4;
constexpr int kArmUvH = 12;
constexpr int kArmUvD = 4;

glm::vec3 ReadVec3(const nlohmann::json &arr, const glm::vec3 &fallback)
{
  if (!arr.is_array() || arr.size() < 3)
  {
    return fallback;
  }
  return glm::vec3(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>());
}

// Tool JSON is Y-up (handle along +Y). Arm local hangs along -Y; after root
// Rx(+88°) that is camera-forward. Mapping JSON +Y onto arm -Y made tools
// collinear with the forearm. Wield space: JSON +Y → mostly arm -Z (up in
// FP frame / above palm), with a small lean toward -Y (into the scene).
constexpr float kToolWieldScale = 0.85f;
constexpr float kToolWieldLeanDeg = 28.f;
/// Target longest axis in arm-local wield space (~matches parts_v1 rod length).
constexpr float kFpWieldTargetExtent = 0.95f;

glm::vec3 ToolJsonOffsetToArm(const glm::vec3 &off)
{
  // Base: (x, y, z)_json → (x, z, -y)_arm  (Y-up → -Z up from palm)
  const glm::vec3 base(off.x, off.z, -off.y);
  const float lean = glm::radians(kToolWieldLeanDeg);
  const float c = std::cos(lean);
  const float sn = std::sin(lean);
  return {base.x, base.y * c - base.z * sn, base.y * sn + base.z * c};
}

glm::vec3 ToolJsonSizeToArm(const glm::vec3 &sz)
{
  // Same axis remap, then AABB extents after Rx(lean).
  const glm::vec3 base(sz.x, sz.z, sz.y);
  const float lean = glm::radians(kToolWieldLeanDeg);
  const float c = std::abs(std::cos(lean));
  const float sn = std::abs(std::sin(lean));
  return {base.x, base.y * c + base.z * sn, base.y * sn + base.z * c};
}

float UnwrapDeltaDeg(float from, float to)
{
  float d = to - from;
  while (d > 180.f)
  {
    d -= 360.f;
  }
  while (d < -180.f)
  {
    d += 360.f;
  }
  return d;
}

struct FaceUvPixels
{
  int u0, v0, u1, v1;
};

enum class UvCorner : uint8_t
{
  Bl,
  Br,
  Tl,
  Tr,
};

constexpr UvCorner kFaceUvCorners[6][4] = {
    {UvCorner::Bl, UvCorner::Br, UvCorner::Tl, UvCorner::Tr},
    {UvCorner::Bl, UvCorner::Br, UvCorner::Tl, UvCorner::Tr},
    {UvCorner::Br, UvCorner::Bl, UvCorner::Tr, UvCorner::Tl},
    {UvCorner::Tr, UvCorner::Br, UvCorner::Tl, UvCorner::Bl},
    {UvCorner::Bl, UvCorner::Br, UvCorner::Tl, UvCorner::Tr},
    {UvCorner::Bl, UvCorner::Br, UvCorner::Tr, UvCorner::Tl},
};

FaceUvPixels ArmFacePixels(int faceIndex)
{
  const int u = kArmUvU;
  const int v = kArmUvV;
  const int w = kArmUvW;
  const int h = kArmUvH;
  const int d = kArmUvD;
  switch (faceIndex)
  {
  case 0:
    return {u + d + w + d, v + d, u + d + w + d + w, v + d + h};
  case 1:
    return {u + d + w, v + d, u + d + w + d, v + d + h};
  case 2:
    return {u + d, v + d, u + d + w, v + d + h};
  case 3:
    return {u, v + d, u + d, v + d + h};
  case 4:
    return {u + d, v, u + d + w, v + d};
  case 5:
    return {u + d + w, v, u + d + w + w, v + d};
  default:
    return {u, v, u + 1, v + 1};
  }
}

void GlUvForCorner(UvCorner corner, float u0, float v0, float u1, float v1,
                   float &outU, float &outV)
{
  switch (corner)
  {
  case UvCorner::Bl:
    outU = u0;
    outV = v1;
    break;
  case UvCorner::Br:
    outU = u1;
    outV = v1;
    break;
  case UvCorner::Tl:
    outU = u0;
    outV = v0;
    break;
  case UvCorner::Tr:
    outU = u1;
    outV = v0;
    break;
  }
}
} // namespace

UFpViewmodelRenderer::UFpViewmodelRenderer(
    std::shared_ptr<UItemDefinitionStorage> items,
    std::shared_ptr<UBlockDefinitionStorage> blocks,
    std::shared_ptr<UTextureCubeStorage> textures,
    std::shared_ptr<UCreatureTextureStorage> creatureTextures,
    std::shared_ptr<UShaderManager> shaderManager)
    : Items(std::move(items)), Blocks(std::move(blocks)),
      Textures(std::move(textures)),
      CreatureTextures(std::move(creatureTextures)),
      ShaderManager(std::move(shaderManager))
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
  Motion.WieldOffsetX = kInertiaBaseX;
  Motion.WieldOffsetY = kInertiaBaseY;
  return true;
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
  delTex(SkinTex);
  delTex(SleeveTex);
  delTex(MetalTex);
  delTex(WoodTex);
  auto delBuf = [](GLuint &b) {
    if (b != 0)
    {
      glDeleteBuffers(1, &b);
      b = 0;
    }
  };
  auto delVao = [](GLuint &v) {
    if (v != 0)
    {
      glDeleteVertexArrays(1, &v);
      v = 0;
    }
  };
  delBuf(CubeEbo);
  delBuf(CubeVbo);
  delVao(CubeVao);
  delBuf(ArmSkinEbo);
  delBuf(ArmSkinVbo);
  delVao(ArmSkinVao);
  ArmSkinTexW = 0;
  ArmSkinTexH = 0;
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
      cubeShift * 3.0f, 1.0f, 0.5f, 0.5f, -0.5f, cubeShift * 2.0f, 0.0f, -0.5f, 0.5f,
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

bool UFpViewmodelRenderer::EnsureArmSkinMesh(int texW, int texH)
{
  if (texW <= 0 || texH <= 0)
  {
    return false;
  }
  if (ArmSkinVao != 0 && ArmSkinTexW == texW && ArmSkinTexH == texH)
  {
    return true;
  }
  if (ArmSkinEbo != 0)
  {
    glDeleteBuffers(1, &ArmSkinEbo);
    ArmSkinEbo = 0;
  }
  if (ArmSkinVbo != 0)
  {
    glDeleteBuffers(1, &ArmSkinVbo);
    ArmSkinVbo = 0;
  }
  if (ArmSkinVao != 0)
  {
    glDeleteVertexArrays(1, &ArmSkinVao);
    ArmSkinVao = 0;
  }

  float posUv[24 * 5];
  int idx = 0;
  const float tw = static_cast<float>(texW);
  const float th = static_cast<float>(texH);
  constexpr float kUvInsetPx = 0.5f;
  for (int face = 0; face < 6; ++face)
  {
    const FaceUvPixels px = ArmFacePixels(face);
    const float u0 = (static_cast<float>(px.u0) + kUvInsetPx) / tw;
    const float v0 = (static_cast<float>(px.v0) + kUvInsetPx) / th;
    const float u1 = (static_cast<float>(px.u1) - kUvInsetPx) / tw;
    const float v1 = (static_cast<float>(px.v1) - kUvInsetPx) / th;
    const int base = face * 4;
    for (int i = 0; i < 4; ++i)
    {
      posUv[idx++] = kCreaturePartPositions[(base + i) * 3 + 0];
      posUv[idx++] = kCreaturePartPositions[(base + i) * 3 + 1];
      posUv[idx++] = kCreaturePartPositions[(base + i) * 3 + 2];
      float tu = 0.f;
      float tv = 0.f;
      GlUvForCorner(kFaceUvCorners[face][i], u0, v0, u1, v1, tu, tv);
      posUv[idx++] = tu;
      posUv[idx++] = tv;
    }
  }
  const unsigned int indices[] = {
      0,  1,  2,  2,  1,  3,  4,  5,  6,  6,  5,  7,  8,  9,  10, 10, 9,  11,
      12, 13, 14, 14, 13, 15, 16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23,
  };

  glGenVertexArrays(1, &ArmSkinVao);
  glGenBuffers(1, &ArmSkinVbo);
  glGenBuffers(1, &ArmSkinEbo);
  glBindVertexArray(ArmSkinVao);
  glBindBuffer(GL_ARRAY_BUFFER, ArmSkinVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(posUv), posUv, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ArmSkinEbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  ArmSkinTexW = texW;
  ArmSkinTexH = texH;
  return ArmSkinVao != 0;
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

std::vector<UFpViewmodelRenderer::Part>
UFpViewmodelRenderer::ArmPartsRight() const
{
  // Local -Y hang: sleeve then hand; skin atlas on both when available.
  Part sleeve{0.0f, -0.26f, 0.0f, 0.18f, 0.52f, 0.18f, 55, 70, 110};
  sleeve.useSkinAtlas = true;
  Part hand{0.0f, -0.55f, 0.02f, 0.16f, 0.16f, 0.16f, 210, 160, 120};
  hand.useSkinAtlas = true;
  return {sleeve, hand};
}

std::vector<UFpViewmodelRenderer::Part>
UFpViewmodelRenderer::ArmPartsLeft() const
{
  auto parts = ArmPartsRight();
  for (Part &p : parts)
  {
    p.ox = -p.ox;
  }
  return parts;
}

void UFpViewmodelRenderer::Update(float dt, float cameraYawDeg,
                                  float cameraPitchDeg, float moveSpeedHint)
{
  if (dt < 0.f)
  {
    dt = 0.f;
  }
  Motion.BobPhase += dt * (1.5f + moveSpeedHint * kBobSpeedScale);
  BobX = 0.008f * std::sin(Motion.BobPhase);
  BobY = 0.006f * std::sin(Motion.BobPhase * 2.f);

  if (!Motion.HaveLastAngles)
  {
    Motion.LastYaw = cameraYawDeg;
    Motion.LastPitch = cameraPitchDeg;
    Motion.HaveLastAngles = true;
    Motion.WieldOffsetX = kInertiaBaseX;
    Motion.WieldOffsetY = kInertiaBaseY;
  }
  else
  {
    const float dYaw = UnwrapDeltaDeg(Motion.LastYaw, cameraYawDeg);
    const float dPitch = cameraPitchDeg - Motion.LastPitch;
    const float velX = std::fabs(dYaw) / std::max(dt, 0.001f) * 0.01f;
    const float velY = std::fabs(dPitch) / std::max(dt, 0.001f);
    const float gapX = std::fabs(kInertiaBaseX - Motion.WieldOffsetX);
    const float gapY = std::fabs(kInertiaBaseY - Motion.WieldOffsetY);
    if (velX > 1.f || velY > 1.f)
    {
      if (velX > 1.f)
      {
        const float acc = 0.12f * (velX - gapX * 0.1f) * 0.001f;
        Motion.WieldOffsetX += (dYaw < 0.f) ? acc : -acc;
        Motion.WieldOffsetX = std::clamp(
            Motion.WieldOffsetX, kInertiaBaseX - kInertiaAmpX * 0.5f,
            kInertiaBaseX + kInertiaAmpX * 0.5f);
      }
      if (velY > 1.f)
      {
        const float acc = 0.12f * (velY - gapY * 0.1f) * 0.001f;
        Motion.WieldOffsetY += (dPitch > 0.f) ? acc : -acc;
        Motion.WieldOffsetY = std::clamp(
            Motion.WieldOffsetY, kInertiaBaseY - kInertiaAmpY * 0.5f,
            kInertiaBaseY + kInertiaAmpY * 0.5f);
      }
    }
    else
    {
      const float decX = 0.35f * dt * 0.05f;
      const float decY = 0.25f * dt * 0.05f;
      if (Motion.WieldOffsetX > kInertiaBaseX)
      {
        Motion.WieldOffsetX =
            std::max(kInertiaBaseX, Motion.WieldOffsetX - decX);
      }
      else
      {
        Motion.WieldOffsetX =
            std::min(kInertiaBaseX, Motion.WieldOffsetX + decX);
      }
      if (Motion.WieldOffsetY > kInertiaBaseY)
      {
        Motion.WieldOffsetY =
            std::max(kInertiaBaseY, Motion.WieldOffsetY - decY);
      }
      else
      {
        Motion.WieldOffsetY =
            std::min(kInertiaBaseY, Motion.WieldOffsetY + decY);
      }
    }
    Motion.LastYaw = cameraYawDeg;
    Motion.LastPitch = cameraPitchDeg;
  }

  if (Motion.Anim.T >= 0.f && !Motion.Anim.Holding)
  {
    const float dur = std::max(0.05f, Motion.Anim.Duration);
    Motion.Anim.T += dt / dur;
    if (Motion.Anim.T >= 1.f)
    {
      if (Motion.Anim.HoldAtEnd)
      {
        Motion.Anim.T = 1.f;
        Motion.Anim.Holding = true;
      }
      else
      {
        Motion.Anim.T = -1.f;
        Motion.Anim.HaveSnapshot = false;
        Motion.DigButton = -1;
      }
    }
  }
  else if (Motion.DigAnim >= 0.f && Motion.Anim.T < 0.f)
  {
    Motion.DigAnim += dt * kSwingSpeed;
    if (Motion.DigAnim >= 1.f)
    {
      Motion.DigAnim = -1.f;
      Motion.DigButton = -1;
    }
  }
}

void UFpViewmodelRenderer::BeginPresetAnim(const std::string &presetId,
                                           bool holdAtEnd)
{
  Motion.Anim.PresetId = presetId;
  Motion.Anim.T = 0.f;
  Motion.Anim.Holding = false;
  Motion.Anim.HoldAtEnd = holdAtEnd;
  Motion.Anim.HaveSnapshot = false;
  Motion.Anim.Duration = 0.28f;
  if (Items)
  {
    if (const ItemVisualPreset *p = Items->VisualPresets().Get(presetId))
    {
      Motion.Anim.Snapshot = *p;
      Motion.Anim.HaveSnapshot = true;
      Motion.Anim.Duration = std::max(0.05f, p->Duration);
      Motion.Anim.HoldAtEnd = holdAtEnd || p->HoldAtEnd;
    }
  }
}

void UFpViewmodelRenderer::NotifySwing(FpSwingKind kind)
{
  // Place always restarts; Dig/Melee ignored while a one-shot swing plays.
  if (kind != FpSwingKind::Place && Motion.Anim.T >= 0.f &&
      !Motion.Anim.Holding)
  {
    return;
  }
  Motion.DigButton = (kind == FpSwingKind::Place) ? 1 : 0;
  Motion.DigAnim = 0.f;
  std::string preset = "dig_tool";
  if (Items && !Motion.ActiveItemHint.empty())
  {
    if (const ItemDefinition *def = Items->Get(Motion.ActiveItemHint))
    {
      preset = DefaultSwingPreset(*def, kind);
    }
  }
  else if (kind == FpSwingKind::Place)
  {
    preset = "place_block";
  }
  else if (kind == FpSwingKind::Melee)
  {
    preset = "slash_weapon";
  }
  BeginPresetAnim(preset, false);
}

void UFpViewmodelRenderer::NotifyUseVisual(const std::string &presetId,
                                           bool holdAtEnd)
{
  if (presetId.empty())
  {
    return;
  }
  BeginPresetAnim(presetId, holdAtEnd);
}

void UFpViewmodelRenderer::ClearHeldVisual()
{
  Motion.Anim.T = -1.f;
  Motion.Anim.Holding = false;
  Motion.Anim.HaveSnapshot = false;
  Motion.DigAnim = -1.f;
  Motion.DigButton = -1;
}

void UFpViewmodelRenderer::SetActiveItemHint(const std::string &itemId)
{
  Motion.ActiveItemHint = itemId;
}

float UFpViewmodelRenderer::SampleEasing(float t, ItemVisualEasing easing) const
{
  t = std::clamp(t, 0.f, 1.f);
  switch (easing)
  {
  case ItemVisualEasing::Linear:
    return t;
  case ItemVisualEasing::Smoothstep:
    return t * t * (3.f - 2.f * t);
  case ItemVisualEasing::SinPi:
  default:
    return std::sin(t * kPi);
  }
}

void UFpViewmodelRenderer::ApplyPresetToOffsets(float eased, float &ox,
                                                float &oy, float &oz,
                                                glm::mat4 &swing) const
{
  if (!Motion.Anim.HaveSnapshot)
  {
    return;
  }
  const ItemVisualArmKeys &arm = Motion.Anim.Snapshot.Arm;
  auto lerp2 = [eased](const float v[2], bool has) {
    return has ? (v[0] + (v[1] - v[0]) * eased) : 0.f;
  };
  if (arm.HasTx)
  {
    ox += lerp2(arm.Tx, true);
  }
  if (arm.HasTy)
  {
    oy += lerp2(arm.Ty, true);
  }
  if (arm.HasTz)
  {
    oz += lerp2(arm.Tz, true);
  }
  const float rx = glm::radians(lerp2(arm.RxDeg, arm.HasRx));
  const float ry = glm::radians(lerp2(arm.RyDeg, arm.HasRy));
  const float rz = glm::radians(lerp2(arm.RzDeg, arm.HasRz));
  glm::quat punch = glm::quat(1.f, 0.f, 0.f, 0.f);
  if (arm.HasRx)
  {
    punch = glm::angleAxis(rx, glm::vec3(1.f, 0.f, 0.f)) * punch;
  }
  if (arm.HasRy)
  {
    punch = glm::angleAxis(ry, glm::vec3(0.f, 1.f, 0.f)) * punch;
  }
  if (arm.HasRz)
  {
    punch = glm::angleAxis(rz, glm::vec3(0.f, 0.f, 1.f)) * punch;
  }
  swing = glm::mat4_cast(punch);
}

glm::mat4 UFpViewmodelRenderer::BuildRootMatrix(bool mirrorX) const
{
  float ox = (mirrorX ? -Motion.WieldOffsetX : Motion.WieldOffsetX) +
             (mirrorX ? -BobX : BobX);
  float oy = Motion.WieldOffsetY + BobY;
  float oz = 0.f;
  glm::mat4 swing(1.f);
  if (Motion.Anim.T >= 0.f && !mirrorX && Motion.Anim.HaveSnapshot)
  {
    const float eased =
        SampleEasing(Motion.Anim.T, Motion.Anim.Snapshot.Easing);
    ApplyPresetToOffsets(eased, ox, oy, oz, swing);
  }
  else if (Motion.DigAnim >= 0.f && !mirrorX)
  {
    const float f = Motion.DigAnim;
    ox += -0.10f * std::sin(std::pow(f, 0.8f) * kPi);
    oy += 0.05f * std::sin(f * 1.8f * kPi);
    oz += 0.025f;
    const float t = std::sin(f * kPi);
    const glm::quat idle = glm::quat(1.f, 0.f, 0.f, 0.f);
    const glm::quat punch = glm::angleAxis(glm::radians(-35.f * t),
                                           glm::vec3(1.f, 0.f, 0.f)) *
                            glm::angleAxis(glm::radians(20.f * t),
                                           glm::vec3(0.f, 1.f, 0.f));
    swing = glm::mat4_cast(glm::slerp(idle, punch, t));
  }

  // Idle: local -Y → camera forward (-Z). Shoulder sits below the frame so
  // the arm enters from the bottom edge (MC/Luanti FP framing). Small yaw so
  // left/right hands share nearly the same depth.
  // Held socket uses local -Z for "above palm" after this orient.
  const glm::quat orient =
      glm::angleAxis(glm::radians(88.f), glm::vec3(1.f, 0.f, 0.f)) *
      glm::angleAxis(glm::radians(mirrorX ? -6.f : 6.f),
                     glm::vec3(0.f, 1.f, 0.f));
  const float basex = (mirrorX ? -0.40f : 0.40f) + ox;
  const float basey = -0.78f + oy;
  const float basez = -0.30f + oz;
  glm::mat4 root = glm::translate(glm::mat4(1.f), glm::vec3(basex, basey, basez)) *
                   glm::mat4_cast(orient) * swing;
  if (mirrorX)
  {
    root = root * glm::scale(glm::mat4(1.f), glm::vec3(-1.f, 1.f, 1.f));
  }
  return root;
}

void UFpViewmodelRenderer::DrawParts(const std::vector<Part> &parts,
                                     const glm::mat4 &mvpBase, GLuint skinAtlas)
{
  GLint skinW = 0;
  GLint skinH = 0;
  const bool haveSkin = skinAtlas != 0;
  if (haveSkin)
  {
    glBindTexture(GL_TEXTURE_2D, skinAtlas);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &skinW);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &skinH);
    EnsureArmSkinMesh(skinW, skinH);
  }

  for (const Part &p : parts)
  {
    const bool skinPart = p.useSkinAtlas && haveSkin && ArmSkinVao != 0;
    GLuint tex = MetalTex;
    GLuint vao = CubeVao;
    if (skinPart)
    {
      tex = skinAtlas;
      vao = ArmSkinVao;
    }
    else if (p.g > 140 && p.b < 140)
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
    glBindVertexArray(vao);
    const glm::mat4 model =
        glm::translate(glm::mat4(1.f), glm::vec3(p.ox, p.oy, p.oz)) *
        glm::scale(glm::mat4(1.f), glm::vec3(p.sx, p.sy, p.sz));
    Shader->SetMat4("mvp_matrix", mvpBase * model);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
}

std::vector<UFpViewmodelRenderer::Part>
UFpViewmodelRenderer::ToolParts(const std::string &itemId,
                                const glm::vec3 &socket) const
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
            Part p;
            const glm::vec3 armOff = ToolJsonOffsetToArm(off) * kToolWieldScale;
            const glm::vec3 armSz = ToolJsonSizeToArm(sz) * kToolWieldScale;
            p.ox = socket.x + armOff.x;
            p.oy = socket.y + armOff.y;
            p.oz = socket.z + armOff.z;
            p.sx = std::max(0.04f, armSz.x);
            p.sy = std::max(0.04f, armSz.y);
            p.sz = std::max(0.04f, armSz.z);
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
    // Fallback rod: long axis along arm -Z (up from palm), not along the arm.
    const glm::vec3 armOff =
        ToolJsonOffsetToArm(glm::vec3(0.f, 0.18f, 0.f)) * kToolWieldScale;
    const glm::vec3 armSz =
        ToolJsonSizeToArm(glm::vec3(0.07f, 0.38f, 0.07f)) * kToolWieldScale;
    parts.push_back(Part{socket.x + armOff.x, socket.y + armOff.y,
                         socket.z + armOff.z, std::max(0.04f, armSz.x),
                         std::max(0.04f, armSz.y), std::max(0.04f, armSz.z), 185,
                         190, 200});
  }
  return parts;
}

void UFpViewmodelRenderer::DrawHeld(const InventoryEntryRef *entry,
                                    const glm::vec3 &socket,
                                    const glm::mat4 &mvpBase)
{
  if (!entry || entry->empty || entry->Id.empty())
  {
    return;
  }
  if (entry->kind == InventoryEntryKind::Item && !entry->broken)
  {
    if (!TryDrawGltfHeld(entry->Id, socket, mvpBase))
    {
      DrawParts(ToolParts(entry->Id, socket), mvpBase, 0);
    }
    return;
  }
  if (entry->kind == InventoryEntryKind::Block)
  {
    DrawBlockCube(entry->Id, socket, mvpBase);
  }
}

bool UFpViewmodelRenderer::TryDrawGltfHeld(const std::string &itemId,
                                           const glm::vec3 &socket,
                                           const glm::mat4 &mvpBase)
{
  if (!Items || !Shader || itemId.empty())
  {
    return false;
  }
  const auto *def = Items->Get(itemId);
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths || !def)
  {
    return false;
  }

  std::string gltfRel = def->ModelPath;
  auto endsWithIgnoreCase = [](const std::string &s, const char *suf)
  {
    const size_t n = std::char_traits<char>::length(suf);
    if (s.size() < n)
    {
      return false;
    }
    for (size_t i = 0; i < n; ++i)
    {
      const char a = static_cast<char>(
          std::tolower(static_cast<unsigned char>(s[s.size() - n + i])));
      const char b =
          static_cast<char>(std::tolower(static_cast<unsigned char>(suf[i])));
      if (a != b)
      {
        return false;
      }
    }
    return true;
  };
  const bool isGltf = endsWithIgnoreCase(gltfRel, ".gltf") ||
                      endsWithIgnoreCase(gltfRel, ".glb");
  if (!isGltf)
  {
    const std::filesystem::path sibling =
        std::filesystem::path("models/items") / itemId / "model.gltf";
    if (paths->AssetExists(sibling.generic_string()))
    {
      gltfRel = sibling.generic_string();
    }
    else
    {
      return false;
    }
  }
  else if (!paths->AssetExists(gltfRel))
  {
    return false;
  }

  const std::filesystem::path absGltf = paths->AssetRoot() / gltfRel;

  static std::mutex cacheMu;
  static std::unordered_map<std::string, std::shared_ptr<CreatureGltfMeshAsset>>
      cache;
  std::shared_ptr<CreatureGltfMeshAsset> asset;
  {
    std::lock_guard<std::mutex> lock(cacheMu);
    const auto it = cache.find(itemId);
    if (it != cache.end())
    {
      asset = it->second;
    }
    else
    {
      const auto abs = absGltf.string();
      asset = CreatureGltfLoader::LoadFromFile(abs);
      if (asset)
      {
        cache[itemId] = asset;
      }
    }
  }
  if (!asset || asset->primitives.empty())
  {
    return false;
  }

  glm::vec3 minV(1e9f), maxV(-1e9f);
  for (const GltfPrimitiveCpu &prim : asset->primitives)
  {
    const auto &posUv = prim.mesh.interleavedPosUv;
    for (size_t i = 0; i + 4 < posUv.size(); i += 5)
    {
      const glm::vec3 p(posUv[i], posUv[i + 1], posUv[i + 2]);
      minV = glm::min(minV, p);
      maxV = glm::max(maxV, p);
    }
  }
  if (minV.x > maxV.x)
  {
    return false;
  }
  const glm::vec3 center = (minV + maxV) * 0.5f;
  const glm::vec3 ext = maxV - minV;
  float fitExtent = std::max(ext.x, std::max(ext.y, ext.z));
  if (def->Visual.FitAxis == "x")
  {
    fitExtent = ext.x;
  }
  else if (def->Visual.FitAxis == "y")
  {
    fitExtent = ext.y;
  }
  else if (def->Visual.FitAxis == "z")
  {
    fitExtent = ext.z;
  }
  fitExtent = std::max(1e-5f, fitExtent);
  const float fitScale = kFpWieldTargetExtent / fitExtent;
  const float wieldScale = DefaultWieldScale(*def);
  const glm::vec3 visualOff(def->Visual.WieldOffset[0],
                            def->Visual.WieldOffset[1],
                            def->Visual.WieldOffset[2]);
  const glm::mat4 visualRot =
      glm::rotate(glm::mat4(1.f), glm::radians(def->Visual.WieldEulerDeg[0]),
                  glm::vec3(1.f, 0.f, 0.f)) *
      glm::rotate(glm::mat4(1.f), glm::radians(def->Visual.WieldEulerDeg[1]),
                  glm::vec3(0.f, 1.f, 0.f)) *
      glm::rotate(glm::mat4(1.f), glm::radians(def->Visual.WieldEulerDeg[2]),
                  glm::vec3(0.f, 0.f, 1.f));

  // Same Y-up → arm remap as parts; lean so blade sits above palm.
  const glm::mat4 orient =
      glm::rotate(glm::mat4(1.f), glm::radians(kToolWieldLeanDeg),
                  glm::vec3(1.f, 0.f, 0.f)) *
      glm::mat4(glm::vec4(1, 0, 0, 0), glm::vec4(0, 0, -1, 0),
                glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 0, 1));
  const glm::mat4 model =
      glm::translate(glm::mat4(1.f), socket) * orient * visualRot *
      glm::scale(glm::mat4(1.f),
                 glm::vec3(fitScale * kToolWieldScale * wieldScale)) *
      glm::translate(glm::mat4(1.f), -center + visualOff);
  const glm::mat4 mvp = mvpBase * model;

  static GLuint vao = 0, vbo = 0, ebo = 0;
  if (vao == 0)
  {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
  }

  GLuint fallbackTex = MetalTex;
  if (itemId.find("wood") != std::string::npos)
  {
    fallbackTex = WoodTex != 0 ? WoodTex : MetalTex;
  }

  bool drew = false;
  for (const GltfPrimitiveCpu &prim : asset->primitives)
  {
    const auto &mesh = prim.mesh;
    if (mesh.interleavedPosUv.empty() || mesh.indices.empty())
    {
      continue;
    }
    GLuint tex = ItemGltfTextureCache::Instance().Get(absGltf, prim.textureStem);
    if (tex == 0)
    {
      tex = fallbackTex;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex != 0 ? tex : fallbackTex);
    Shader->Use();
    Shader->SetInt("texture0", 0);
    Shader->SetInt("uAnimFrame", 0);
    Shader->SetInt("uAnimFrameCount", 1);
    Shader->SetMat4("mvp_matrix", mvp);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.interleavedPosUv.size() *
                                         sizeof(float)),
                 mesh.interleavedPosUv.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned)),
                 mesh.indices.data(), GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                   GL_UNSIGNED_INT, nullptr);
    drew = true;
  }
  glBindVertexArray(0);
  return drew;
}

GLuint UFpViewmodelRenderer::ResolveBlockAtlas(const std::string &typeName) const
{
  if (!Blocks || !Textures || typeName.empty())
  {
    return 0;
  }
  const BlockDefinition *def = Blocks->GetByName(typeName);
  if (!def)
  {
    return 0;
  }
  const size_t typeId = Textures->GetTypeIdByName(def->Name);
  const auto &texMap = Textures->GetTextures();
  const auto it = texMap.find(typeId);
  if (it != texMap.end())
  {
    return it->second.GetTexture();
  }
  for (const auto &kv : texMap)
  {
    if (kv.second.GetName() == def->Name)
    {
      return kv.second.GetTexture();
    }
  }
  return 0;
}

GLuint UFpViewmodelRenderer::ResolvePlayerSkin(const std::string &speciesId,
                                               const std::string &skinId) const
{
  if (!CreatureTextures)
  {
    return 0;
  }
  return ResolveCreatureSpeciesTexture(*CreatureTextures, speciesId, "diffuse",
                                       "body", skinId);
}

bool UFpViewmodelRenderer::TryDrawSkinnedArms(const glm::mat4 &, bool)
{
  // TD-ITEM-004: load creature/item arm glTF into this pass when assets exist.
  // Do not switch to body-in-FP (camera inside world creature mesh).
  return false;
}

void UFpViewmodelRenderer::DrawBlockCube(const std::string &typeName,
                                         const glm::vec3 &socket,
                                         const glm::mat4 &mvpBase)
{
  const GLuint atlas = ResolveBlockAtlas(typeName);
  if (atlas == 0 || CubeVao == 0 || !Shader)
  {
    return;
  }
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlas);
  glBindVertexArray(CubeVao);
  const float s = 0.22f;
  const glm::mat4 tilt =
      glm::rotate(glm::mat4(1.f), glm::radians(-18.f), glm::vec3(1.f, 0.f, 0.f)) *
      glm::rotate(glm::mat4(1.f), glm::radians(28.f), glm::vec3(0.f, 1.f, 0.f));
  const glm::mat4 model =
      glm::translate(glm::mat4(1.f), socket) * tilt *
      glm::scale(glm::mat4(1.f), glm::vec3(s, s, s));
  Shader->SetMat4("mvp_matrix", mvpBase * model);
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void UFpViewmodelRenderer::DrawWorldOverlay(const FpViewmodelDrawParams &params)
{
  if (!Shader || params.FramebufferW <= 0 || params.FramebufferH <= 0)
  {
    return;
  }
  if (params.Active && !params.Active->empty &&
      params.Active->kind == InventoryEntryKind::Item)
  {
    SetActiveItemHint(params.Active->Id);
  }
  else if (params.Active && !params.Active->empty &&
           params.Active->kind == InventoryEntryKind::Block)
  {
    SetActiveItemHint({});
  }
  UGlStateScope glState(kGlMaskFpViewmodel3D);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, params.FramebufferW, params.FramebufferH);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const float aspect = static_cast<float>(params.FramebufferW) /
                       static_cast<float>(params.FramebufferH);
  const glm::mat4 projection =
      glm::perspective(glm::radians(kFovDeg), aspect, kNear, kFar);
  const glm::mat4 view =
      glm::lookAt(kEye, kTarget, glm::vec3(0.f, 1.f, 0.f));
  const glm::mat4 pv = projection * view;
  const GLuint skinAtlas =
      ResolvePlayerSkin(params.SpeciesId, params.SkinId);

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);

  const glm::mat4 rootR = BuildRootMatrix(false);
  if (!TryDrawSkinnedArms(pv * rootR, false))
  {
    DrawParts(ArmPartsRight(), pv * rootR, skinAtlas);
  }
  DrawHeld(params.Active, kHandSocketR, pv * rootR);

  const glm::mat4 rootL = BuildRootMatrix(true);
  if (!TryDrawSkinnedArms(pv * rootL, true))
  {
    DrawParts(ArmPartsLeft(), pv * rootL, skinAtlas);
  }
  DrawHeld(params.Offhand, kHandSocketL, pv * rootL);

  Shader->Unuse();
}

} // namespace cutum
