#ifndef IUGAMECONTENT_H
#define IUGAMECONTENT_H

namespace cutum
{

class UBlockDefinitionStorage;
class UObjectLibrary;
class UCreatureDefinitionStorage;
struct WorldGenPack;

/// Read-only catalog of blocks, objects, creatures, and active worldgen pack.
class IUGameContent
{
public:
  virtual ~IUGameContent() = default;

  virtual const UBlockDefinitionStorage &Blocks() const = 0;
  virtual const UObjectLibrary &Objects() const = 0;
  virtual const UCreatureDefinitionStorage &Creatures() const = 0;
  virtual const WorldGenPack &ActiveWorldGenPack() const = 0;
};

} // namespace cutum

#endif
