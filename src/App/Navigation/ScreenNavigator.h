#ifndef SCREENNAVIGATOR_H
#define SCREENNAVIGATOR_H

namespace cutum
{

class UApplication;

/// Menu and screen transitions (extracted incrementally from UApplication).
class UScreenNavigator
{
public:
  explicit UScreenNavigator(UApplication *application);

  void ShowMainMenu();
  // TODO: extract ShowSettings, ShowWorldSettings, ShowLoadWorld, ShowNewWorld

private:
  UApplication *Application;
};

} // namespace cutum

#endif
