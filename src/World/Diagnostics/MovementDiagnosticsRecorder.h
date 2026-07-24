#ifndef MOVEMENTDIAGNOSTICSRECORDER_H
#define MOVEMENTDIAGNOSTICSRECORDER_H

#include <memory>
#include <string>

namespace cutum
{

class UCamera;
class UWorld;

class UMovementDiagnosticsRecorder
{
public:
  static void Update(UWorld &world, const std::shared_ptr<UCamera> &camera,
                     float prev_player_y);
  static void SaveToFile(const UWorld &world, const std::string &file_name);
};

} // namespace cutum

#endif
