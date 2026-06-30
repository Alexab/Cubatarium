#ifndef CREATURETEXTURERESOLVER_H
#define CREATURETEXTURERESOLVER_H

typedef unsigned int GLuint;

#include <string>

namespace cutum
{

class UCreatureTextureStorage;

GLuint ResolveCreatureSpeciesTexture(const UCreatureTextureStorage &textures,
                                     const std::string &speciesId,
                                     const std::string &stem,
                                     const std::string &defaultStem,
                                     const std::string &skinId = {});

bool LogCreatureMissingTextureOnce(const std::string &logKey,
                                   const std::string &message);

} // namespace cutum

#endif
