#ifndef CAMERAPERSPECTIVE_H
#define CAMERAPERSPECTIVE_H

namespace cutum
{

enum class CameraPerspective
{
  FirstPerson,
  ThirdPersonBack,
  ThirdPersonFront,
};

inline CameraPerspective CycleCameraPerspective(CameraPerspective p)
{
  switch (p)
  {
  case CameraPerspective::FirstPerson:
    return CameraPerspective::ThirdPersonBack;
  case CameraPerspective::ThirdPersonBack:
    return CameraPerspective::ThirdPersonFront;
  default:
    return CameraPerspective::FirstPerson;
  }
}

inline const char *CameraPerspectiveLabel(CameraPerspective p)
{
  switch (p)
  {
  case CameraPerspective::ThirdPersonBack:
    return "Third person (back)";
  case CameraPerspective::ThirdPersonFront:
    return "Third person (front)";
  default:
    return "First person";
  }
}

} // namespace cutum

#endif
