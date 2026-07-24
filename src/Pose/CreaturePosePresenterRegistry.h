#ifndef CREATUREPOSEPRESENTERREGISTRY_H
#define CREATUREPOSEPRESENTERREGISTRY_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Pose/IUCreaturePosePresenter.h"
#include <array>
#include <memory>

namespace cutum
{

class UCreaturePosePresenterRegistry
{
public:
  void Register(std::unique_ptr<IUCreaturePosePresenter> presenter);
  IUCreaturePosePresenter *Get(LocomotionArchetype archetype) const;
  void Clear();

private:
  std::array<std::unique_ptr<IUCreaturePosePresenter>, 5> Presenters{};
};

void RegisterDefaultCreaturePosePresenters(
    UCreaturePosePresenterRegistry &registry);

} // namespace cutum

#endif
