#include "Creatures/Movement/CreatureFootprint.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_footprint_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  Expect(MinSolidSamplesForFootprint(0.5f) == 1,
         "small chicken footprint needs one solid sample");
  Expect(MinSolidSamplesForFootprint(1.8f) == 9,
         "cow footprint needs full grid support");

  Expect(EvaluateGroundSupport(true, 9, 0.5f),
         "chicken on full 3x3 plate");
  Expect(EvaluateGroundSupport(true, 9, 1.8f),
         "cow on full 3x3 plate");
  Expect(EvaluateGroundSupport(true, 1, 0.5f),
         "chicken center column only");
  Expect(!EvaluateGroundSupport(true, 1, 1.8f),
         "cow center column only is unsupported");
  Expect(!EvaluateGroundSupport(false, 9, 0.5f),
         "no center solid fails");

  std::cout << "creature_footprint_test: OK" << std::endl;
  return 0;
}
