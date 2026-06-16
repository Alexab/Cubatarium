#include "Pose/CreaturePosePresenterRegistry.h"
#include "Pose/TerrestrialBipedPosePresenter.h"

namespace cutum
{

void UCreaturePosePresenterRegistry::Register(
    std::unique_ptr<ICreaturePosePresenter> presenter)
{
  if (!presenter)
  {
    return;
  }
  const size_t index = static_cast<size_t>(presenter->GetArchetype());
  if (index < Presenters.size())
  {
    Presenters[index] = std::move(presenter);
  }
}

ICreaturePosePresenter *
UCreaturePosePresenterRegistry::Get(LocomotionArchetype archetype) const
{
  const size_t index = static_cast<size_t>(archetype);
  if (index >= Presenters.size())
  {
    return nullptr;
  }
  return Presenters[index].get();
}

void UCreaturePosePresenterRegistry::Clear()
{
  for (auto &entry : Presenters)
  {
    entry.reset();
  }
}

void RegisterDefaultCreaturePosePresenters(
    UCreaturePosePresenterRegistry &registry)
{
  registry.Register(std::make_unique<UTerrestrialBipedPosePresenter>());
}

} // namespace cutum
