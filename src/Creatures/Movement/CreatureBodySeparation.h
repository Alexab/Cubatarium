#ifndef CREATUREBODYSEPARATION_H
#define CREATUREBODYSEPARATION_H

namespace cutum
{

class UCreature;
class UWorld;

bool CreatureOverlapsOthers(const UWorld &world, const UCreature &creature);

bool SeparateFromBlocksAndCreatures(UWorld &world, UCreature &creature,
                                    int maxIterations = 1);

} // namespace cutum

#endif
