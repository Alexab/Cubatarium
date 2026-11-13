"""Content and source provenance for published original items."""
from pathlib import Path
import hashlib
import json

DEPENDENCIES = ("tools/item_materials.yaml", "tools/itemgen/builders.py", "tools/itemgen/export_gltf.py", "tools/itemgen/texturegen.py", "tools/itemgen/pipeline.py", "tools/itemgen/manifest.py")
def update_manifest(root: Path, item_id: str | None = None) -> dict:
    path=root/"models/items/MANIFEST.json"
    data=json.loads(path.read_text(encoding="utf-8")) if path.exists() else {"items":{}}
    data.update(version=3,generator="itemgen-v2",provenance="Original Cubatarium geometry and procedural textures; no Minecraft assets")
    ids=[item_id] if item_id else sorted(p.stem for p in (root/"content/items").glob("*.json"))
    if item_id is None:data["items"]={}
    for id in ids:
        files=[root/"content/items"/(id+".json"),root/"tools/item_recipes"/(id+".yaml"),*sorted((root/"models/items"/id).rglob("*")),*[root/p for p in DEPENDENCIES]]
        data["items"][id]={"dependencies":list(DEPENDENCIES),"sha256":{p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()}}
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(data,indent=2)+"\n",encoding="utf-8")
    temporary.replace(path)
    return data

def validate_manifest(root: Path) -> list[str]:
    path=root/"models/items/MANIFEST.json"
    if not path.is_file():return ["missing publication manifest"]
    data=json.loads(path.read_text(encoding="utf-8"));errors=[]
    expected={p.stem for p in (root/"content/items").glob("*.json")}
    if set(data.get("items",{}))!=expected:errors.append("manifest catalog differs from item definitions")
    for id,entry in data.get("items",{}).items():
        required=[root/"content/items"/(id+".json"),root/"tools/item_recipes"/(id+".yaml"),*sorted((root/"models/items"/id).rglob("*"))]
        if any(p.relative_to(root).as_posix() not in entry.get("sha256",{}) for p in required if p.is_file()):errors.append(f"{id}: missing published resource hashes")
        for relative,expected_hash in entry.get("sha256",{}).items():
            source=(root/relative).resolve()
            if not source.is_relative_to(root.resolve()):errors.append(f"{id}: invalid dependency path");continue
            if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest()!=expected_hash:errors.append(f"{id}: stale or missing {relative}")
        if any(dep not in entry.get("sha256",{}) for dep in DEPENDENCIES):errors.append(f"{id}: missing generator dependency hashes")
    return errors
