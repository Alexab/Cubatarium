#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/CreatureAppearance.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

void Expect(bool condition, const char *message, int &failures)
{
  if (!condition)
  {
    std::cerr << "creature_appearance_test: " << message << '\n';
    ++failures;
  }
}

const cutum::ResolvedCreaturePart *
FindPart(const cutum::ResolvedCreatureAppearance &appearance,
         const std::string &partId)
{
  for (const auto &part : appearance.Parts)
  {
    if (part.partId == partId)
    {
      return &part;
    }
  }
  return nullptr;
}

class TempCatalog
{
public:
  TempCatalog()
  {
    Root = std::filesystem::temp_directory_path() /
           ("creature_appearance_test_" + std::to_string(++Counter));
    std::filesystem::create_directories(Root);
  }
  ~TempCatalog() { std::filesystem::remove_all(Root); }

  std::string WriteCreature(const std::string &id, const std::string &body)
  {
    const std::filesystem::path dir = Root / "creatures" / id;
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "creature.json";
    std::ofstream out(path);
    out << body;
    return path.string();
  }

  std::string WriteSkin(const std::string &id, const std::string &body)
  {
    const std::filesystem::path dir = Root / "skins" / id;
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "skin.json";
    std::ofstream out(path);
    out << body;
    return path.string();
  }

  std::filesystem::path Root;

private:
  static int Counter;
};

int TempCatalog::Counter = 0;

} // namespace

int main()
{
  using namespace cutum;
  int failures = 0;
  TempCatalog catalog;

  UCreatureDefinitionStorage species;
  Expect(species.LoadFile(catalog.WriteCreature(
              "rigid_test", R"({
  "id": "rigid_test",
  "display_name": "Rigid Test",
  "visual": {
    "backend": "rigid_voxels",
    "default_texture": "body",
    "parts": [
      {"id": "body", "size": [0.9, 0.9, 0.6], "offset": [0, 0.45, 0], "texture": "body"},
      {"id": "face", "size": [0.8, 0.8, 0.1], "offset": [0, 1.3, 0.31], "texture": "face"},
      {"id": "unstitched", "size": [0.5, 0.5, 0.5], "offset": [0, 2, 0]}
    ]
  }
})")),
         "load rigid_test definition", failures);
  Expect(species.LoadFile(catalog.WriteCreature(
              "bone_test", R"({
  "id": "bone_test",
  "display_name": "Bone Test",
  "visual": {
    "backend": "bone_skeleton",
    "geometry_file": "geometry.geo.json"
  }
})")),
         "load bone_test definition", failures);
  Expect(species.LoadFile(catalog.WriteCreature(
              "noparts_test", R"({
  "id": "noparts_test",
  "display_name": "No Parts Test",
  "visual": {"backend": "rigid_voxels"}
})")),
         "load noparts_test definition", failures);

  USkinDefinitionStorage skins;
  catalog.WriteSkin("skin_map", R"({
  "id": "skin_map",
  "creature_id": "rigid_test",
  "visual": {
    "texture": "body",
    "texture_map": {"body": "wool"},
    "wireframe_color": [1.0, 0.0, 0.0, 1.0]
  }
})");
  catalog.WriteSkin("skin_other", R"({
  "id": "skin_other",
  "creature_id": "sheep",
  "visual": {"texture": "body"}
})");
  catalog.WriteSkin("skin_key_only", R"({
  "id": "skin_key_only",
  "visual": {"texture": "face"}
})");
  skins.Load((catalog.Root / "skins").string());
  Expect(skins.Get("skin_map") != nullptr, "load skin_map skin", failures);
  Expect(skins.Get("skin_other") != nullptr, "load skin_other skin", failures);
  Expect(skins.Get("skin_key_only") != nullptr, "load skin_key_only skin", failures);

  {
    const ResolvedCreatureAppearance base =
        ResolveCreatureAppearance(species, skins, "rigid_test", "");
    Expect(!base.useWireframeFallback, "known species has no wireframe fallback", failures);
    Expect(base.visualBackend == "rigid_voxels", "backend string propagates", failures);
    Expect(base.defaultTextureKey == "body", "default texture key propagates", failures);
    Expect(base.Parts.size() == 3, "all declared rigid parts resolve", failures);
    const ResolvedCreaturePart *body = FindPart(base, "body");
    const ResolvedCreaturePart *face = FindPart(base, "face");
    const ResolvedCreaturePart *unstitched = FindPart(base, "unstitched");
    Expect(body && body->textureAssetKey == "rigid_test/body",
           "explicit stem maps to species/stem", failures);
    Expect(face && face->textureAssetKey == "rigid_test/face",
           "second stem maps independently", failures);
    Expect(unstitched && unstitched->textureAssetKey == "rigid_test/body",
           "part without a texture falls back to the default key", failures);
  }

  {
    const ResolvedCreatureAppearance skinned =
        ResolveCreatureAppearance(species, skins, "rigid_test", "skin_map");
    const ResolvedCreaturePart *body = FindPart(skinned, "body");
    const ResolvedCreaturePart *face = FindPart(skinned, "face");
    Expect(body && body->textureAssetKey == "skin/skin_map/wool",
           "texture_map stems remap to skin/<id>/<mapped>", failures);
    Expect(face && face->textureAssetKey == "rigid_test/face",
           "stems absent from texture_map stay with the species", failures);
    Expect(skinned.wireframeColor == glm::vec4(1.f, 0.f, 0.f, 1.f),
           "compatible skin tint overrides wireframe color", failures);
  }

  {
    const ResolvedCreatureAppearance foreign =
        ResolveCreatureAppearance(species, skins, "rigid_test", "skin_other");
    const ResolvedCreaturePart *body = FindPart(foreign, "body");
    Expect(body && body->textureAssetKey == "rigid_test/body",
           "skin bound to another creature is ignored", failures);
  }

  {
    const ResolvedCreatureAppearance keyed =
        ResolveCreatureAppearance(species, skins, "rigid_test", "skin_key_only");
    const ResolvedCreaturePart *body = FindPart(keyed, "body");
    const ResolvedCreaturePart *face = FindPart(keyed, "face");
    Expect(body && body->textureAssetKey == "rigid_test/body",
           "unbound skin leaves other stems untouched", failures);
    Expect(face && face->textureAssetKey == "skin/skin_key_only/face",
           "skin with no creature_id matches any species by texture key", failures);
  }

  {
    const ResolvedCreatureAppearance bone =
        ResolveCreatureAppearance(species, skins, "bone_test", "");
    Expect(bone.Parts.empty(),
           "non-rigid backends resolve no rigid parts", failures);
    Expect(bone.defaultTextureKey == "body",
           "default texture key defaults to body when unset", failures);
  }

  {
    const ResolvedCreatureAppearance noparts =
        ResolveCreatureAppearance(species, skins, "noparts_test", "");
    Expect(noparts.Parts.size() == 1,
           "rigid species without parts gets a single fallback part", failures);
    const ResolvedCreaturePart *part = noparts.Parts.empty() ? nullptr : &noparts.Parts[0];
    Expect(part && part->partId == "body" &&
               part->textureAssetKey == "noparts_test/body",
           "fallback part uses the body stem", failures);
  }

  {
    const ResolvedCreatureAppearance unknown =
        ResolveCreatureAppearance(species, skins, "no_such_species", "");
    Expect(unknown.useWireframeFallback,
           "unknown species triggers wireframe fallback", failures);
    Expect(unknown.Parts.empty(), "unknown species resolves no parts", failures);
  }

  if (failures != 0)
  {
    return 1;
  }
  std::cout << "creature_appearance_test: OK\n";
  return 0;
}
