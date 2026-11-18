#pragma once

#include "World/Lighting/SectionRenderLightSnapshot.h"
#include <array>
#include <cstdint>

namespace cutum
{
/// A render-thread resource lease accompanying the exact geometry artifact.
/// No GL declarations: mesh builders and headless publication tests can carry
/// its lifetime without depending on a particular graphics backend.
class SectionRenderLightBinding
{
public:
  virtual ~SectionRenderLightBinding() = default;
  virtual std::array<uint32_t, 4> ShaderWords() const = 0;
  virtual bool IsLive() const = 0;
  uint64_t WorldEpoch() const { return Epoch; }
  const SectionGeometryRenderInputs &GeometryInputs() const { return Geometry; }
  const SectionRenderLightSnapshot &LightSnapshot() const { return Light; }
  bool Matches(uint64_t epoch, const SectionGeometryRenderInputs &geometry,
               const SectionRenderLightSnapshot &light) const
  {
    return Epoch == epoch && Geometry.Matches(geometry) &&
           Light.MatchesGeometry(geometry) && light.MatchesGeometry(geometry) &&
           Light.FieldIncarnation() == light.FieldIncarnation() &&
           Light.FieldRevision() == light.FieldRevision() && IsLive();
  }

protected:
  SectionRenderLightBinding(uint64_t epoch,
                            SectionGeometryRenderInputs geometry,
                            SectionRenderLightSnapshot light)
      : Epoch(epoch), Geometry(std::move(geometry)), Light(std::move(light))
  {
  }
  void SetCompatibleLight(SectionRenderLightSnapshot light)
  {
    Light = std::move(light);
  }

private:
  uint64_t Epoch;
  SectionGeometryRenderInputs Geometry;
  SectionRenderLightSnapshot Light;
};
} // namespace cutum
