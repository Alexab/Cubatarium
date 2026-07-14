#include "Creatures/Visual/CreatureTextureResolver.h"

#include "Creatures/Visual/CreatureTextureStorage.h"
#include <iostream>
#include <unordered_set>

namespace cutum
{

namespace
{

std::unordered_set<std::string> gMissingCreatureTextureLogged;

} // namespace

GLuint ResolveCreatureSpeciesTexture(const UCreatureTextureStorage &textures,
                                     const std::string &speciesId,
                                     const std::string &stem,
                                     const std::string &defaultStem,
                                     const std::string &skinId)
{
  const std::string keys[] = {
      speciesId + "/" + stem,
      speciesId + "/diffuse",
      speciesId + "/" + defaultStem,
      speciesId + "/body",
  };
  for (const std::string &key : keys)
  {
    if (!key.empty() && key.back() != '/')
    {
      if (const GLuint tex = textures.GetTexture(key))
      {
        return tex;
      }
    }
  }
  if (!skinId.empty())
  {
    if (const GLuint tex = textures.GetTexture("skin/" + skinId + "/" + stem))
    {
      return tex;
    }
    if (const GLuint tex = textures.GetTexture("skin/" + skinId + "/diffuse"))
    {
      return tex;
    }
  }
  return 0;
}

bool LogCreatureMissingTextureOnce(const std::string &logKey,
                                   const std::string &message)
{
  if (!gMissingCreatureTextureLogged.insert(logKey).second)
  {
    return false;
  }
  std::cerr << message << std::endl;
  return true;
}

} // namespace cutum
