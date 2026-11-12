#ifndef CREATURETEXTURERESOLVER_H
#define CREATURETEXTURERESOLVER_H

typedef unsigned int GLuint;

#include <string>

namespace cutum
{

class UCreatureTextureStorage;
struct ResolvedCreatureAppearance;
struct ResolvedCreaturePart;

GLuint ResolveCreatureSpeciesTexture(const UCreatureTextureStorage &textures,
                                     const std::string &speciesId,
                                     const std::string &stem,
                                     const std::string &defaultStem,
                                     const ResolvedCreatureAppearance *appearance = nullptr);

GLuint ResolveCreaturePartTexture(const UCreatureTextureStorage &textures,
                                 const ResolvedCreaturePart &part);

bool LogCreatureMissingTextureOnce(const std::string &logKey,
                                   const std::string &message);

} // namespace cutum

#endif
