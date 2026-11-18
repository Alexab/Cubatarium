#include "Render/Lighting/SectionRenderLightBindings.h"
#include "Render/Lighting/SectionLightBindingTable.h"
#include <limits>

namespace cutum
{
struct SectionRenderLightBindings::State
{
  SectionLightBindingTable table;
  uint64_t epoch{0};
  std::shared_ptr<const uint64_t> geometry_identity;
  std::vector<SectionLightBindingTable::Handle> pending_release;
  bool active{false};
  bool ordered_fallback{false};
  void Retire(SectionLightBindingTable::Handle handle)
  {
    if (!table.Release(handle) && table.IsLive(handle))
      pending_release.push_back(handle);
  }
};

struct SectionRenderLightBindings::Entry final : SectionRenderLightBinding
{
  std::weak_ptr<State> state;
  SectionLightBindingTable::Handle handle;
  Entry(std::shared_ptr<State> owner, SectionLightBindingTable::Handle handle,
        const SectionGeometryRenderInputs &geometry,
        const SectionRenderLightSnapshot &light)
      : SectionRenderLightBinding(owner->epoch, geometry, light), state(owner),
        handle(handle)
  {
  }
  ~Entry() override
  {
    if (auto owner = state.lock(); owner && owner->active)
      owner->Retire(handle);
  }
  bool IsLive() const override
  {
    const auto owner = state.lock();
    return owner && owner->active && owner->epoch == WorldEpoch() &&
           (handle.valid ? owner->table.IsLive(handle)
                         : owner->ordered_fallback);
  }
  std::array<uint32_t, 4> ShaderWords() const override
  {
    return IsLive() ? handle.ShaderWords() : std::array<uint32_t, 4>{};
  }
  void UpdateSnapshot(const SectionRenderLightSnapshot &light)
  {
    SetCompatibleLight(light);
  }
};

SectionRenderLightBindings::~SectionRenderLightBindings() { Reset(); }

bool SectionRenderLightBindings::BeginWorld(
    uint64_t world_epoch,
    std::shared_ptr<const uint64_t> world_geometry_identity,
    uint32_t binding_limit, size_t atlas_bytes, uint32_t page_limit,
    bool enable_ordered_fallback)
{
  if (!world_epoch || !world_geometry_identity)
    return false;
  Reset();
  auto state = std::make_shared<State>();
  if (!state->table.Initialize(binding_limit, atlas_bytes, page_limit,
                               enable_ordered_fallback))
    return false;
  // Each live table slot has at most one Entry. Reserving its entire bounded
  // capacity makes final lease destruction allocation-free, including errors.
  state->pending_release.reserve(state->table.Capacity());
  state->epoch = world_epoch;
  state->geometry_identity = std::move(world_geometry_identity);
  state->active = true;
  state->ordered_fallback = enable_ordered_fallback;
  Current = std::move(state);
  return true;
}

void SectionRenderLightBindings::Reset()
{
  if (Current)
  {
    Current->active = false;
    Current->table.Reset();
    Current.reset();
  }
}

namespace
{
std::optional<glm::ivec3> SectionOrigin(glm::ivec3 coord)
{
  glm::ivec3 origin;
  for (int axis = 0; axis < 3; ++axis)
  {
    const int64_t value = int64_t(coord[axis]) * CHUNK_SIZE;
    // The entire padded volume must have representable integer coordinates.
    if (value < int64_t(std::numeric_limits<int>::min()) +
                    SectionLightVolume::Radius ||
        value > int64_t(std::numeric_limits<int>::max()) -
                    (CHUNK_SIZE + SectionLightVolume::Radius - 1))
      return std::nullopt;
    origin[axis] = static_cast<int>(value);
  }
  return origin;
}
} // namespace

std::shared_ptr<SectionRenderLightBinding>
SectionRenderLightBindings::Prepare(uint64_t source_world_epoch,
                                    const SectionGeometryRenderInputs &geometry,
                                    const SectionRenderLightSnapshot &light)
{
  if (!Current || !Current->active || source_world_epoch != Current->epoch ||
      geometry.world_geometry_identity != Current->geometry_identity ||
      !light.MatchesGeometry(geometry) || !light.Volume())
    return {};
  const auto origin = SectionOrigin(geometry.coord);
  if (!origin)
    return {};
  if (glGetError() != GL_NO_ERROR)
    return {};
  auto handle = Current->table.Create(
      {source_world_epoch, light.FieldIncarnation(), light.FieldRevision()},
      light.Volume(), *origin);
  if (!handle)
  {
    if (!Current->ordered_fallback)
      return {};
    handle = Current->table.CreateDeferred();
  }
  try
  {
    return std::make_shared<Entry>(
        Current, handle.value_or(SectionLightBindingTable::Handle{}), geometry,
        light);
  }
  catch (...)
  {
    if (handle)
      Current->Retire(*handle);
    throw;
  }
}

bool SectionRenderLightBindings::Refresh(
    const std::shared_ptr<SectionRenderLightBinding> &binding,
    uint64_t source_world_epoch, const SectionRenderLightSnapshot &light)
{
  const auto entry = std::dynamic_pointer_cast<Entry>(binding);
  if (!entry || !Current || entry->state.lock() != Current ||
      source_world_epoch != Current->epoch || !entry->IsLive() ||
      !light.MatchesGeometry(entry->GeometryInputs()) || !light.Volume())
    return false;
  const auto &previous = entry->LightSnapshot();
  if (previous.FieldIncarnation() == light.FieldIncarnation() &&
      previous.FieldRevision() == light.FieldRevision())
  {
    if (previous.Volume() != light.Volume() &&
        previous.Volume()->cells != light.Volume()->cells)
      return false;
    entry->UpdateSnapshot(light);
    return true;
  }
  const auto origin = SectionOrigin(entry->GeometryInputs().coord);
  if (!origin || glGetError() != GL_NO_ERROR)
    return false;
  if (!entry->handle.valid ||
      !Current->table.Replace(
          entry->handle,
          {source_world_epoch, light.FieldIncarnation(), light.FieldRevision()},
          light.Volume(), *origin))
  {
    if (!Current->ordered_fallback ||
        (entry->handle.valid && !Current->table.Defer(entry->handle)))
      return false;
  }
  entry->UpdateSnapshot(light);
  return true;
}

bool SectionRenderLightBindings::IsResident(
    const std::shared_ptr<SectionRenderLightBinding> &binding) const
{
  const auto entry = std::dynamic_pointer_cast<Entry>(binding);
  return entry && Current && entry->state.lock() == Current &&
         entry->IsLive() && Current->table.HasResidentPage(entry->handle);
}

std::optional<std::array<uint32_t, 4>> SectionRenderLightBindings::PrepareDraw(
    const std::shared_ptr<SectionRenderLightBinding> &binding)
{
  const auto entry = std::dynamic_pointer_cast<Entry>(binding);
  if (!entry || !Current || entry->state.lock() != Current || !entry->IsLive())
    return std::nullopt;
  if (IsResident(binding))
    return entry->ShaderWords();
  const auto origin = SectionOrigin(entry->GeometryInputs().coord);
  if (!Current->ordered_fallback || !origin)
    return std::nullopt;
  const auto &light = entry->LightSnapshot();
  // A deferred stable handle may regain a page after retired work completes.
  // Its words stay unchanged, so no geometry/instance buffer update is needed.
  if (entry->handle.valid &&
      Current->table.Replace(entry->handle,
                             {entry->WorldEpoch(), light.FieldIncarnation(),
                              light.FieldRevision()},
                             light.Volume(), *origin))
    return entry->ShaderWords();
  const auto handle = Current->table.StageForOrderedDraw(
      {entry->WorldEpoch(), light.FieldIncarnation(), light.FieldRevision()},
      light.Volume(), *origin);
  return handle ? std::optional(handle->ShaderWords()) : std::nullopt;
}

bool SectionRenderLightBindings::Bind(unsigned atlas_unit,
                                      unsigned table_unit) const
{
  return Current && Current->active &&
         Current->table.Bind(atlas_unit, table_unit);
}

std::array<uint32_t, 2> SectionRenderLightBindings::ShaderIncarnation() const
{
  return Current ? Current->table.ShaderIncarnation()
                 : std::array<uint32_t, 2>{};
}

void SectionRenderLightBindings::CollectRetired()
{
  if (Current)
  {
    // Failed invalidation remains owned until a later render-thread retry.
    // Do not silently leak a slot when final destruction encounters GL errors.
    for (unsigned n = 0; n < 64 && !Current->pending_release.empty(); ++n)
    {
      const auto handle = Current->pending_release.back();
      if (Current->table.IsLive(handle) && !Current->table.Release(handle))
        break;
      Current->pending_release.pop_back();
    }
    Current->table.CollectRetired();
  }
}
} // namespace cutum
