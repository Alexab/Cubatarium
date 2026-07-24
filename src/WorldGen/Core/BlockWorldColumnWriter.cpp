#include "WorldGen/Core/BlockWorldColumnWriter.h"

namespace cutum
{

UBlockWorldColumnWriter::UBlockWorldColumnWriter(UBlockWorld &world,
                                                 UBlockRegistry &registry)
    : World(world), Registry(registry)
{
}

void UBlockWorldColumnWriter::OnColumnFinished(int, int, int, int)
{
}

} // namespace cutum
