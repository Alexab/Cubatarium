#!/usr/bin/env python3
"""Bulk rename prefab->object in src/ after manual new files created."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "src"

REPLACEMENTS = [
    ("World/Prefabs/PrefabUtil.h", "World/Objects/ObjectUtil.h"),
    ("World/Prefabs/PrefabUtil.cpp", "World/Objects/ObjectUtil.cpp"),
    ("World/Prefabs/Prefab.h", "World/Objects/ObjectLibrary.h"),
    ("World/Prefabs/Prefab.cpp", "World/Objects/ObjectLibrary.cpp"),
    ("Gui/Cache/PrefabIconCache.h", "Gui/Cache/ObjectIconCache.h"),
    ("Gui/Cache/PrefabIconCache.cpp", "Gui/Cache/ObjectIconCache.cpp"),
    ("WorldGen/Features/PrefabFeatureConfig.h", "WorldGen/Features/ObjectFeatureConfig.h"),
    ("WorldGen/Features/PrefabFeatureConfig.cpp", "WorldGen/Features/ObjectFeatureConfig.cpp"),
    ("WorldGen/Features/PrefabFeaturePlacer.h", "WorldGen/Features/ObjectFeaturePlacer.h"),
    ("WorldGen/Features/PrefabFeaturePlacer.cpp", "WorldGen/Features/ObjectFeaturePlacer.cpp"),
    ("WorldGen/Features/PrefabPlacementConstraints.h", "WorldGen/Features/ObjectPlacementConstraints.h"),
    ("WorldGen/Features/PrefabPlacementConstraints.cpp", "WorldGen/Features/ObjectPlacementConstraints.cpp"),
    ("UPrefabFeatureConfigStorage", "UObjectFeatureConfigStorage"),
    ("UPrefabLibrary", "UObjectLibrary"),
    ("PrefabLibraryInstance", "ObjectLibraryInstance"),
    ("prefab_library", "object_library"),
    ("PrefabLibrary", "ObjectLibrary"),
    ("PrefabPlacementStats", "ObjectPlacementStats"),
    ("PrefabVoxel", "ObjectVoxel"),
    ("PrefabFeatureRule", "ObjectFeatureRule"),
    ("PrefabFeatureConfig", "ObjectFeatureConfig"),
    ("PrefabFeaturePool", "ObjectFeaturePool"),
    ("PrefabPlacementMode", "ObjectPlacementMode"),
    ("PrefabFeaturePlacer", "ObjectFeaturePlacer"),
    ("PrefabPlacementConstraints", "ObjectPlacementConstraints"),
    ("PrefabIconCache", "ObjectIconCache"),
    ("PlacePrefabAtForWorldGen", "PlaceObjectAtForWorldGen"),
    ("CanPlacePrefabAtForWorldGen", "CanPlaceObjectAtForWorldGen"),
    ("PlacePrefabAt", "PlaceObjectAt"),
    ("CanPlacePrefabAt", "CanPlaceObjectAt"),
    ("TryPlacePrefabPool", "TryPlaceObjectPool"),
    ("PlacePrefabAtWaterSurface", "PlaceObjectAtWaterSurface"),
    ("struct Prefab", "struct WorldObjectDefinition"),
    ("const Prefab &", "const WorldObjectDefinition &"),
    ("const Prefab *", "const WorldObjectDefinition *"),
    ("Prefab &", "WorldObjectDefinition &"),
    ("Prefab *", "WorldObjectDefinition *"),
    ("Prefab ", "WorldObjectDefinition "),
    ("ContentKind::UObject", "ContentKind::Object"),
    ("InventoryEntryKind::UObject", "InventoryEntryKind::Object"),
    ("IndexPrefabs", "IndexObjects"),
    ("GetCategory", "GetTags"),
    ("prefab_features.json", "object_features.json"),
    ("prefab_manifest.yaml", "object_manifest.yaml"),
    ("PrefabsPath", "ObjectsPath"),
    ('/"prefabs"', '/"objects"'),
    ('"prefabs"', '"objects"'),
    ('/prefabs/', '/objects/'),
    ("PlacePrefab(", "PlaceObject("),
    ("PrefabName", "ObjectName"),
    ('"prefab"', '"object"'),
    ("UPrefab", "UObject"),
    ("#ifndef PREFAB_", "#ifndef OBJECT_"),
    ("#define PREFAB_", "#define OBJECT_"),
]

SKIP = {
    "World/Prefabs/",
    "Storage/ObjectStorage",
    "Storage/Object.h",
    "Storage/ObjectImplementation",
    "IO/LegacyChunkJsonLoader",
}


def should_process(path: Path) -> bool:
    if path.suffix not in (".cpp", ".h", ".md"):
        return False
    rel = path.relative_to(ROOT).as_posix()
    for s in SKIP:
        if s in rel:
            return False
    if rel.startswith("World/Objects/"):
        return False
    return True


def main() -> None:
    for path in ROOT.rglob("*"):
        if not path.is_file() or not should_process(path):
            continue
        text = path.read_text(encoding="utf-8")
        orig = text
        for old, new in REPLACEMENTS:
            text = text.replace(old, new)
        if text != orig:
            path.write_text(text, encoding="utf-8")
            print("updated", path.relative_to(ROOT))


if __name__ == "__main__":
    main()
