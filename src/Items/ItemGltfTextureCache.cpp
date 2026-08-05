#include "Items/ItemGltfTextureCache.h"

#include "Render/GlIncludes.h"
#include "ThirdParty/stb_image.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

GLuint LoadPng(const std::filesystem::path &path)
{
  if (!std::filesystem::is_regular_file(path))
  {
    return 0;
  }
  int w = 0, h = 0, ch = 0;
  unsigned char *data =
      stbi_load(path.string().c_str(), &w, &h, &ch, 4);
  if (!data)
  {
    return 0;
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(data);
  return tex;
}

void IndexPngDir(const std::filesystem::path &dir,
                 std::unordered_map<std::string, GLuint> &out)
{
  if (!std::filesystem::is_directory(dir))
  {
    return;
  }
  for (const auto &entry : std::filesystem::directory_iterator(dir))
  {
    if (!entry.is_regular_file())
    {
      continue;
    }
    const auto ext = entry.path().extension().string();
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg")
    {
      continue;
    }
    const std::string stem = entry.path().stem().string();
    if (out.find(stem) != out.end())
    {
      continue;
    }
    const GLuint tex = LoadPng(entry.path());
    if (tex != 0)
    {
      out[stem] = tex;
    }
  }
}

void LoadFromGltfJson(const std::filesystem::path &gltfPath,
                      std::unordered_map<std::string, GLuint> &out)
{
  std::ifstream in(gltfPath);
  if (!in)
  {
    return;
  }
  nlohmann::json gltf;
  try
  {
    in >> gltf;
  }
  catch (...)
  {
    return;
  }
  const std::filesystem::path modelDir = gltfPath.parent_path();
  if (!gltf.contains("images") || !gltf["images"].is_array())
  {
    return;
  }
  std::vector<std::filesystem::path> imagePaths;
  for (const auto &img : gltf["images"])
  {
    if (!img.contains("uri"))
    {
      continue;
    }
    const std::string uri = img["uri"].get<std::string>();
    if (uri.empty() || uri.find("data:") == 0)
    {
      continue;
    }
    imagePaths.push_back(modelDir / uri);
  }
  if (!gltf.contains("materials") || !gltf["materials"].is_array())
  {
    for (const auto &p : imagePaths)
    {
      const std::string stem = p.stem().string();
      if (out.find(stem) == out.end())
      {
        const GLuint tex = LoadPng(p);
        if (tex != 0)
        {
          out[stem] = tex;
        }
      }
    }
    return;
  }
  for (const auto &mat : gltf["materials"])
  {
    std::string stem = mat.value("name", std::string());
    if (stem.empty())
    {
      continue;
    }
    int texIndex = -1;
    if (mat.contains("pbrMetallicRoughness") &&
        mat["pbrMetallicRoughness"].contains("baseColorTexture"))
    {
      texIndex =
          mat["pbrMetallicRoughness"]["baseColorTexture"].value("index", -1);
    }
    if (texIndex < 0 || !gltf.contains("textures") ||
        texIndex >= static_cast<int>(gltf["textures"].size()))
    {
      if (mat.contains("pbrMetallicRoughness"))
      {
        const auto &pbr = mat["pbrMetallicRoughness"];
        if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() &&
            pbr["baseColorFactor"].size() >= 3)
        {
          const std::string key =
              stem.empty() ? "__factor__" : stem + "_factor";
          if (out.find(key) != out.end())
          {
            continue;
          }
          const auto &c = pbr["baseColorFactor"];
          const unsigned char rgba[4] = {
              static_cast<unsigned char>(c[0].get<float>() * 255.f),
              static_cast<unsigned char>(c[1].get<float>() * 255.f),
              static_cast<unsigned char>(c[2].get<float>() * 255.f),
              static_cast<unsigned char>(
                  (c.size() > 3 ? c[3].get<float>() : 1.f) * 255.f)};
          GLuint tex = 0;
          glGenTextures(1, &tex);
          glBindTexture(GL_TEXTURE_2D, tex);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, rgba);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
          glBindTexture(GL_TEXTURE_2D, 0);
          out[key] = tex;
          if (stem.empty())
          {
            out["body"] = tex;
          }
        }
      }
      continue;
    }
    const int srcIndex = gltf["textures"][texIndex].value("source", -1);
    if (srcIndex < 0 || srcIndex >= static_cast<int>(gltf["images"].size()))
    {
      continue;
    }
    const std::string uri = gltf["images"][srcIndex].value("uri", "");
    if (uri.empty() || uri.find("data:") == 0)
    {
      continue;
    }
    const auto imgPath = modelDir / uri;
    if (out.find(stem) != out.end())
    {
      continue;
    }
    const GLuint tex = LoadPng(imgPath);
    if (tex != 0)
    {
      out[stem] = tex;
    }
  }
}

} // namespace

ItemGltfTextureCache &ItemGltfTextureCache::Instance()
{
  static ItemGltfTextureCache inst;
  return inst;
}

void ItemGltfTextureCache::EnsureLoaded(const std::filesystem::path &modelGltfAbs)
{
  const std::string key = modelGltfAbs.parent_path().generic_string();
  if (ByModelDir.find(key) != ByModelDir.end())
  {
    return;
  }
  std::unordered_map<std::string, GLuint> texMap;
  LoadFromGltfJson(modelGltfAbs, texMap);
  IndexPngDir(modelGltfAbs.parent_path() / "Textures", texMap);
  IndexPngDir(modelGltfAbs.parent_path(), texMap);
  ByModelDir[key] = std::move(texMap);
}

GLuint ItemGltfTextureCache::Get(const std::filesystem::path &modelGltfAbs,
                                 const std::string &textureStem)
{
  std::lock_guard<std::mutex> lock(Mu);
  EnsureLoaded(modelGltfAbs);
  const std::string key = modelGltfAbs.parent_path().generic_string();
  const auto &texMap = ByModelDir[key];
  if (!textureStem.empty())
  {
    const auto it = texMap.find(textureStem);
    if (it != texMap.end())
    {
      return it->second;
    }
  }
  if (!texMap.empty())
  {
    return texMap.begin()->second;
  }
  return 0;
}

void ItemGltfTextureCache::Clear()
{
  std::lock_guard<std::mutex> lock(Mu);
  for (auto &kv : ByModelDir)
  {
    for (auto &t : kv.second)
    {
      if (t.second != 0)
      {
        glDeleteTextures(1, &t.second);
      }
    }
  }
  ByModelDir.clear();
}

} // namespace cutum
