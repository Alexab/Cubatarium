#pragma once
#include "Creatures/Visual/CreatureVisualBackendSelect.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Test/Native/CreatureQaResourceCacheProbe.h"
#include "ThirdParty/stb_image_write.h"
#include <array>

namespace cutum
{
inline void
ExportCreatureQaTextureMappingProbe(UCreaturePreviewRenderer &preview,
                                    const std::vector<std::string> &ids,
                                    const std::filesystem::path &directory)
{
  const char *enabled = std::getenv("CUBA_FLIGHT_QA_RESOURCE_CACHE_PROBE");
  if (!enabled || std::string(enabled) != "1")
    return;
  using Json = nlohmann::json;
  const auto fixtureRoot =
      std::filesystem::absolute(directory / "texture_mapping_fixture");
  struct Scenario
  {
    std::string species, skin, kind, firstStem;
    CreatureDefinition definition;
    std::vector<std::string> stems;
  };
  std::vector<Scenario> scenarios;
  Json report = {{"schema", "creature-texture-mapping-probe.v1"},
                 {"scope",
                  "Synthetic native B/G/R draw submissions and diagnostic GPU "
                  "readback; not art, FP, save or full pack acceptance"},
                 {"cases", Json::array()},
                 {"gpu_textures", Json::array()},
                 {"texture_ids", Json::object()}};
  auto writePng = [](const std::filesystem::path &file,
                     const std::array<unsigned char, 4> &color)
  {
    std::array<unsigned char, 64> pixels{};
    for (size_t i = 0; i < 16; ++i)
      std::copy(color.begin(), color.end(), pixels.begin() + i * 4);
    if (!stbi_write_png(file.string().c_str(), 4, 4, 4, pixels.data(), 16))
      throw std::runtime_error("Diagnostic PNG failed");
  };
  for (const auto &id : ids)
  {
    const auto *def = preview.GetSpecies()->Get(id);
    if (!def)
      throw std::runtime_error("Mapping probe species missing");
    std::vector<std::string> stems;
    if (def->visual.backend == "bone_skeleton")
    {
      const auto mesh = CreatureBoneSkeletonCache::Instance().Load(
          id, def->visual.boneSkeleton.geometryFile,
          def->visual.boneSkeleton.geometryId, def->visual.sourceDirectory);
      if (!mesh)
        throw std::runtime_error("Mapping probe bone asset missing");
      const auto stem = def->visual.boneSkeleton.textureStem.empty()
                            ? "diffuse"
                            : def->visual.boneSkeleton.textureStem;
      for (const auto &bone : mesh->boneMeshes)
        for (const auto &cube : bone.cubes)
          if (!cube.interleavedPosUv.empty() && !cube.indices.empty())
            stems.push_back(stem);
    }
    else if (def->visual.backend == "gltf_skeleton")
    {
      const auto mesh = CreatureGltfCache::Instance().Load(
          id, def->visual.gltf.modelPath, def->visual.sourceDirectory);
      if (!mesh)
        throw std::runtime_error("Mapping probe glTF missing");
      for (const auto &primitive : mesh->primitives)
        stems.push_back(primitive.textureStem);
    }
    else
      for (const auto &part : def->visual.Parts)
        stems.push_back(part.textureStem.empty() ? def->visual.defaultTextureKey
                                                 : part.textureStem);
    if (stems.empty() && def->visual.backend == "rigid_voxels")
      stems.push_back(def->visual.defaultTextureKey.empty()
                          ? "body"
                          : def->visual.defaultTextureKey);
    if (stems.empty())
      throw std::runtime_error("Mapping probe needs material stems: " + id);
    std::vector<std::string> kinds = {"mapped", "unmapped", "missing_mapped",
                                      "direct_key", "missing_skin"};
    if (def->visual.backend == "bone_skeleton")
      kinds.push_back("legacy_atlas");
    for (const auto &kind : kinds)
    {
      Scenario s{id,   "qa_texture_" + id + "_" + kind,
                 kind, stems.front(),
                 *def, stems};
      if (kind == "unmapped" && def->visual.backend == "bone_skeleton")
      {
        s.definition.visual.boneSkeleton.textureStem = "qa_unmapped";
        std::fill(s.stems.begin(), s.stems.end(), "qa_unmapped");
        s.firstStem = "qa_unmapped";
      }
      const auto folder = fixtureRoot / s.skin;
      std::filesystem::create_directories(folder / "textures");
      Json skin = {
          {"id", s.skin},
          {"creature_id", id},
          {"visual",
           {{"texture", "qa_unrelated"}, {"texture_map", Json::object()}}}};
      if (kind == "mapped" || kind == "missing_mapped")
        for (const auto &stem : s.stems)
          skin["visual"]["texture_map"][stem] =
              kind == "mapped" ? "variant" : "absent";
      if (kind == "unmapped")
        skin["visual"]["texture_map"]["qa_unrelated"] = "variant";
      if (kind == "direct_key")
        skin["visual"]["texture"] = s.firstStem;
      writePng(folder / "textures/variant.png", {231, 37, 89, 255});
      writePng(folder / "textures/diffuse.png", {17, 91, 217, 255});
      if (kind == "direct_key")
      {
        if (std::filesystem::path(s.firstStem) !=
                std::filesystem::path(s.firstStem).filename() ||
            s.firstStem.empty())
          throw std::runtime_error("Unsafe diagnostic texture stem");
        writePng(folder / "textures" / (s.firstStem + ".png"),
                 {43, 199, 71, 255});
      }
      if (kind != "missing_skin")
      {
        std::ofstream file(folder / "skin.json");
        file << skin.dump(2) << '\n';
        if (!file)
          throw std::runtime_error("Diagnostic skin failed");
      }
      scenarios.push_back(std::move(s));
    }
  }
  USkinDefinitionStorage skins;
  skins.Load(fixtureRoot.string());
  UCreatureTextureStorage textures;
  struct Cleanup
  {
    UCreatureTextureStorage &textures;
    ~Cleanup() { textures.LoadFromCreatureAndSkinRoots("", ""); }
  } cleanup{textures};
  textures.LoadFromCreatureAndSkinRoots(
      CreatureGltfCache::Instance().GetCreaturesRoot(), fixtureRoot.string());
  bool passed = true;
  RenderSettings settings;
  settings.CreatureTexturedParts = true;
  settings.CreatureWireframeOverlay = false;
  for (const auto &s : scenarios)
  {
    const auto &def = s.definition;
    UCreature creature(1, s.species, glm::vec3(0.f),
                       glm::vec3(0.f, def.eyeHeight, 0.f));
    creature.GetBoundsMutable().profile = def.bounds;
    creature.GetBoundsMutable().currentSizeBlocks = def.bounds.restSizeBlocks;
    CreatureLocomotionFacts facts;
    facts.state = LocomotionState::Walk;
    facts.horizontalSpeed = def.locomotion.walkSpeed;
    facts.animPhase = .375f;
    auto draw = [&](const std::string &skin)
    {
      creature.SetSkinId(skin);
      auto visual = CreateCreatureVisual(def);
      visual->SetAppearance(ResolveCreatureAppearance(*preview.GetSpecies(),
                                                      skins, s.species, skin));
      visual->UpdatePose(creature, facts, CreaturePoseParams{}, def, .1375f);
      CreatureDrawQueue queue;
      CreatureDrawContext context;
      context.Queue = &queue;
      context.Textures = &textures;
      context.Settings = &settings;
      visual->SubmitDraw(context, glm::mat4(1.f));
      UCreatureQaResourceDrawSnapshot result;
      queue.Flush(result);
      std::sort(result.geometryHashes.begin(), result.geometryHashes.end());
      return result;
    };
    const auto base = draw("");
    const auto actual = draw(s.skin);
    std::vector<GLuint> expected;
    std::vector<std::string> expectedKeys;
    for (const auto &stem : s.stems)
    {
      std::string key;
      if (s.kind == "mapped")
        key = "skin/" + s.skin + "/variant";
      else if (s.kind == "legacy_atlas")
        key = "skin/" + s.skin + "/diffuse";
      else if (s.kind == "direct_key" && stem == s.firstStem)
        key = "skin/" + s.skin + "/" + stem;
      if (key.empty())
      {
        if (def.visual.backend == "rigid_voxels")
          key = s.species + "/" + (stem.empty() ? "body" : stem);
        else
          for (const auto &candidate :
               {stem, std::string("diffuse"), def.visual.defaultTextureKey,
                std::string("body")})
            if (!candidate.empty() &&
                textures.GetTexture(s.species + "/" + candidate))
            {
              key = s.species + "/" + candidate;
              break;
            }
      }
      const auto texture = textures.GetTexture(key);
      if (!texture)
        throw std::runtime_error("Mapping oracle texture missing: " + key);
      report["texture_ids"][key] = texture;
      expectedKeys.push_back(key);
      expected.push_back(texture);
    }
    std::sort(expected.begin(), expected.end());
    const bool valid = actual.textures == expected &&
                       actual.draws == base.draws && actual.draws > 0 &&
                       actual.geometryHashes == base.geometryHashes;
    passed = passed && valid;
    report["cases"].push_back(
        {{"species", s.species},
         {"backend", def.visual.backend},
         {"skin", s.skin},
         {"runtime_backend",
          CreatureUsesBillboardSprite(def) ? "sprite" : def.visual.backend},
         {"kind", s.kind},
         {"stems", s.stems},
         {"first_stem", s.firstStem},
         {"expected_keys", expectedKeys},
         {"expected_textures", expected},
         {"actual_textures", actual.textures},
         {"draws", actual.draws},
         {"base_geometry_hashes", base.geometryHashes},
         {"actual_geometry_hashes", actual.geometryHashes},
         {"base_draws", base.draws},
         {"geometry_unchanged", actual.geometryHashes == base.geometryHashes},
         {"passed", valid}});
    for (const auto &stem :
         {std::string("variant"), std::string("diffuse"),
          s.kind == "direct_key" ? s.firstStem : std::string("variant")})
    {
      const auto key = "skin/" + s.skin + "/" + stem;
      const auto texture = textures.GetTexture(key);
      report["texture_ids"][key] = texture;
      UGlStateScope state(kGlMaskIconFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, texture, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Mapping GPU readback failed");
      std::array<unsigned char, 4> rgba{};
      glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
      glDeleteFramebuffers(1, &fbo);
      const std::array<unsigned char, 4> wanted =
          s.kind == "direct_key" && stem == s.firstStem
              ? std::array<unsigned char, 4>{43, 199, 71, 255}
          : stem == "variant" ? std::array<unsigned char, 4>{231, 37, 89, 255}
                              : std::array<unsigned char, 4>{17, 91, 217, 255};
      const bool gpuValid = rgba == wanted;
      passed = passed && gpuValid;
      report["gpu_textures"].push_back({{"key", key},
                                        {"texture", texture},
                                        {"rgba", rgba},
                                        {"passed", gpuValid}});
    }
  }
  report["passed"] = passed;
  std::ofstream stream(directory / "texture_mapping_probe.json");
  stream << report.dump(2) << '\n';
  stream.close();
  if (!stream)
    throw std::runtime_error("Texture mapping report failed");
  if (!passed)
    throw std::runtime_error(
        "Texture mapping/fallback differs between creature backends");
}
} // namespace cutum
