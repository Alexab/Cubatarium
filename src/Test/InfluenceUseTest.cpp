#include "Items/ItemDefinition.h"
#include "Items/ItemUseRegistry.h"

#include <iostream>

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::ItemDefinition;
  using cutum::ItemUseAction;
  using cutum::ItemUseActionKind;
  using cutum::ItemUseRegistry;

  Expect(ItemUseRegistry::ParseAction("eat") == ItemUseAction::Eat, "parse eat");
  Expect(ItemUseRegistry::ParseAction("drink") == ItemUseAction::Drink,
         "parse drink");
  Expect(ItemUseRegistry::ParseAction("none") == ItemUseAction::None,
         "parse none");

  ItemDefinition apple;
  apple.Id = "apple";
  apple.Use.Action = ItemUseActionKind::Eat;
  apple.Use.Satiety = 20.f;
  apple.Use.Health = 2.f;
  const auto params = ItemUseRegistry::FromDefinition(apple);
  Expect(params.Action == ItemUseAction::Eat, "apple action");
  Expect(params.SatietyDelta == 20.f, "apple satiety");
  Expect(params.HealthDelta == 2.f, "apple health");

  std::cout << "influence_use_test: OK" << std::endl;
  return 0;
}
