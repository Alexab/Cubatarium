#ifndef WEATHERAUTOCONTROLLER_H
#define WEATHERAUTOCONTROLLER_H

namespace cutum
{

class UWorld;

class UWeatherAutoController
{
public:
  static void Tick(UWorld &world, float dt_seconds);
};

} // namespace cutum

#endif // WEATHERAUTOCONTROLLER_H
