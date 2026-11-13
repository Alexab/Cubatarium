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

class UTempCatalog
{
public:
  UTempCatalog()
  {
    Root = std::filesystem::temp_directory_path() /
           ("creature_appearance_test_" + std::to_string(++Counter));
    std::filesystem::create_directories(Root);
  }
  ~UTempCatalog() { std::filesystem::remove_all(Root); }

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

int UTempCatalog::Counter = 0;

} // namespace

int main()
{
  using namespace cutum;
  int failures = 0;
  {
    HumanAppearance invalid;
    invalid.bodyVariant="bad";invalid.hairstyleId="../../bad";invalid.hairColorId="";invalid.Normalize();
    Expect(invalid.Key()=="male/short/chestnut","unknown appearance values normalize",failures);
    const auto legacy=nlohmann::json::object().get<HumanAppearance>();
    Expect(legacy.Key()=="male/short/chestnut","legacy appearance defaults",failures);
    for(auto body:HumanAppearance::Bodies)for(auto hair:HumanAppearance::Hairstyles)for(auto color:HumanAppearance::Colors) {
      HumanAppearance look;look.bodyVariant=body;look.hairstyleId=hair;look.hairColorId=color;
      nlohmann::json saved=look;
      Expect(saved.get<HumanAppearance>().Key()==look.Key(),"all 60 selections round trip",failures);
    }
    const auto malformed=nlohmann::json::parse(R"({"body_variant":3,"hairstyle_id":null,"hair_color_id":false})").get<HumanAppearance>();
    Expect(malformed.Key()==legacy.Key(),"malformed optional fields preserve defaults",failures);
  }
  UTempCatalog catalog;

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
    Expect(body && body->baseTextureAssetKey == "rigid_test/body",
           "mapped part retains its original material for missing-file fallback and UVs", failures);
    Expect(skinned.skinTextureOverrides.at("body") == "skin/skin_map/wool",
           "explicit mapping is available to skeletal and FP texture resolution", failures);
    Expect(skinned.skinTextureOverrides.find("face") == skinned.skinTextureOverrides.end(),
           "unmapped material is not replaced by another skin atlas", failures);
    Expect(face && face->textureAssetKey == "rigid_test/face",
           "stems absent from texture_map stay with the species", failures);
    Expect(skinned.wireframeColor == glm::vec4(1.f, 0.f, 0.f, 1.f),
           "compatible skin tint overrides wireframe color", failures);
    Expect(skinned.effectiveSkinId == "skin_map",
           "compatible selection is retained for skeletal backends", failures);
  }

  {
    const ResolvedCreatureAppearance foreign =
        ResolveCreatureAppearance(species, skins, "rigid_test", "skin_other");
    const ResolvedCreaturePart *body = FindPart(foreign, "body");
    Expect(body && body->textureAssetKey == "rigid_test/body",
           "skin bound to another creature is ignored", failures);
    Expect(foreign.effectiveSkinId.empty(),
           "foreign selection cannot reach skeletal texture lookup", failures);
    Expect(foreign.skinTextureOverrides.empty(),
           "foreign skin cannot publish texture overrides", failures);
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
    Expect(keyed.effectiveSkinId == "skin_key_only",
           "universal selection remains available to all backends", failures);
  }

  {
    const ResolvedCreatureAppearance bone =
        ResolveCreatureAppearance(species, skins, "bone_test", "");
    Expect(bone.Parts.empty(),
           "non-rigid backends resolve no rigid parts", failures);
    Expect(bone.defaultTextureKey == "body",
           "default texture key defaults to body when unset", failures);
    const auto atlas = ResolveCreatureAppearance(species, skins, "bone_test", "skin_key_only");
    Expect(atlas.skinTextureOverrides.at("diffuse") == "skin/skin_key_only/diffuse",
           "bone backend preserves the bundled whole-mesh diffuse atlas convention", failures);
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

  {
    const auto previousRevision = species.Revision.load();
    const auto emptyRoot = catalog.Root / "empty_catalog";
    std::filesystem::create_directories(emptyRoot);
    species.Load(emptyRoot.string());
    Expect(species.Count() == 0 && species.Get("rigid_test") == nullptr,
           "loading an empty catalog removes previous definitions", failures);
    Expect(species.Revision.load() > previousRevision,
           "empty catalog replacement invalidates cached appearances", failures);
  }

  {
    Expect(species.LoadFile(catalog.WriteCreature("human",R"({"id":"human","visual":{"backend":"bone_skeleton","geometry":"geometry.human"}})")),"human fixture",failures);
    const auto root=catalog.Root/"creatures"/"human";
    std::filesystem::create_directories(root/"appearance");std::filesystem::create_directories(root/"textures");
    const auto geometry=root/"appearance"/"female_long.geo.json";
    const auto atlas=root/"textures"/"appearance_female_ginger.png";
    std::ofstream(geometry)<<"{}";std::ofstream(atlas)<<"fixture";
    HumanAppearance look;look.bodyVariant="female";look.hairstyleId="long";look.hairColorId="ginger";
    auto resolved=ResolveCreatureAppearance(species,skins,"human","",look);
    Expect(resolved.geometryFileOverride=="appearance/female_long.geo.json","selected human geometry",failures);
    Expect(resolved.skinTextureOverrides["diffuse"]=="human/appearance_female_ginger","independent human atlas",failures);
    const auto different=ResolveCreatureAppearance(species,skins,"human","",HumanAppearance{});
    Expect(different.appearanceKey!=resolved.appearanceKey,"different selections have distinct cache keys",failures);
    const auto pack=catalog.Root/"legacy_pack"/"human"/"textures";
    std::filesystem::create_directories(pack);std::ofstream(pack/"diffuse.png")<<"legacy fixture";
    const auto revision=species.Revision.load();species.LoadOverlay((catalog.Root/"legacy_pack").string());
    Expect(species.Revision.load()>revision,"texture-only pack invalidates resolved appearance cache",failures);
    Expect(ResolveCreatureAppearance(species,skins,"human","",look).geometryFileOverride.empty(),"legacy atlas overlay retains matching base UV geometry",failures);
    std::ofstream(pack/"appearance_male_chestnut.png")<<"new fixture";
    species.LoadOverlay((catalog.Root/"legacy_pack").string());
    Expect(!ResolveCreatureAppearance(species,skins,"human","",look).geometryFileOverride.empty(),"new atlas overlay enables player customization",failures);
    std::filesystem::remove(atlas);
    Expect(ResolveCreatureAppearance(species,skins,"human","",look).geometryFileOverride.empty(),"missing atlas falls back with matching base geometry",failures);
  }

  if (failures != 0)
  {
    return 1;
  }
  std::cout << "creature_appearance_test: OK\n";
  return 0;
}
