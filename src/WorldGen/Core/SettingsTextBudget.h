#pragma once
#include "Core/Memory/BufferAllocationBudget.h"
#include "WorldGen/Core/RuntimeSettingsText.h"

namespace cutum
{
// Charge a fresh settings/string copy before construction. Declare this
// before the copied value so destruction releases text before its credit.
class USettingsTextBudget
{
public:
  explicit USettingsTextBudget(size_t bytes) : Owner(ActiveBufferBudget),Bytes(bytes)
  {if(Owner.budget && Bytes) Owner.budget->Reserve(*Owner.job,Bytes);}
  explicit USettingsTextBudget(const ProceduralSettings &settings,size_t extra=0)
      : USettingsTextBudget(AddTextCapacityBytes(RuntimeSettingsTextCapacityBytes(settings),extra)) {}
  ~USettingsTextBudget() {if(Owner.budget && Bytes) Owner.budget->Release(*Owner.job,Bytes);}
  USettingsTextBudget(const USettingsTextBudget &)=delete;
  USettingsTextBudget &operator=(const USettingsTextBudget &)=delete;
private:
  BufferBudgetContext Owner;
  size_t Bytes{0};
};
} // namespace cutum
