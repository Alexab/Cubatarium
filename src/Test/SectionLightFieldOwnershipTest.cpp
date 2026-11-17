#include "World/Chunks/ChunkManager.h"
#include <cstdlib>
#include <iostream>

using namespace cutum;
static void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
int main()
{
  UChunkManager manager;
  const glm::ivec3 air{-4, -3, 7};
  const auto geometry_revision = manager.GetGeometryInputRevision();
  auto field = manager.EnsureSectionLightField(air);
  const auto identity = field->GetIncarnation();
  Expect(!manager.HasChunk(air) && manager.GetResidentChunkCount() == 0 &&
             manager.GetResidentColumnCount() == 0,
         "allocating a field must not invent geometry or a resident column");
  std::array<uint8_t, CHUNK_VOLUME> bytes{};
  bytes[UChunk::LocalIndex({15, 2, 0})] = 0xb3;
  auto revision = manager.GetInputRevision();
  Expect(manager.ReplaceSectionLightData(air, bytes) &&
             manager.GetInputRevision() > revision &&
             manager.GetGeometryInputRevision() == geometry_revision &&
             field->GetRevision() == 1 && field->GetSkyRevision() == 1 &&
             field->GetBlockRevision() == 1,
         "independent light writes change field/full clocks without geometry "
         "mutation");
  revision = manager.GetInputRevision();
  Expect(!manager.ReplaceSectionLightData(air, bytes) &&
             manager.GetInputRevision() == revision,
         "identical field installation must preserve revisions");
  manager.EnsureChunk(air);
  auto *chunk = manager.GetChunk(air);
  Expect(&chunk->GetSectionLightField() == field.get() &&
             field->GetIncarnation() == identity &&
             chunk->GetLightData() == bytes,
         "geometry admission must adapt the existing canonical light resource");
  Expect(!manager.ReleaseAbsentSectionLightField(air),
         "a resident geometry adapter must retain its canonical field");
  chunk->SetLightLocal({15, 2, 0}, 4, 11);
  Expect(field->GetData()[UChunk::LocalIndex({15, 2, 0})] == 0xb4 &&
             field->GetSkyRevision() == 2 && field->GetBlockRevision() == 1,
         "legacy writes and field reads must share one payload and channel "
         "revisions");
  UChunk detached(*chunk);
  revision = manager.GetInputRevision();
  detached.SetLightLocal({15, 2, 0}, 1, 1);
  Expect(detached.GetSectionLightField().GetIncarnation() != identity &&
             field->GetData()[UChunk::LocalIndex({15, 2, 0})] == 0xb4 &&
             manager.GetInputRevision() == revision,
         "a copied chunk must not acquire a world field or input clock");
  *chunk = detached;
  Expect(
      &chunk->GetSectionLightField() == field.get() &&
          field->GetIncarnation() != identity &&
          field->GetData()[UChunk::LocalIndex({15, 2, 0})] == 0x11,
      "managed assignment preserves binding while retiring resource identity");
  const auto retired_identity = field->GetIncarnation();
  manager.RemoveChunk(air);
  Expect(!manager.GetSectionLightField(air),
         "geometry eviction retires its field");
  manager.EnsureChunk({8, -2, 9});
  Expect(field->GetIncarnation() == retired_identity &&
             field->GetData()[UChunk::LocalIndex({15, 2, 0})] == 0x11 &&
             manager.GetChunk({8, -2, 9})->GetLightFieldRevision() == 0,
         "free-list reuse must not mutate a retained retired field snapshot");
  auto replacement = manager.EnsureSectionLightField(air);
  Expect(replacement->GetIncarnation() != retired_identity &&
             replacement->GetRevision() == 0,
         "same-coordinate reallocation must not resurrect a retired resource");
  Expect(manager.ReleaseAbsentSectionLightField(air) &&
             !manager.GetSectionLightField(air),
         "absent geometry fields need explicit bounded-lifetime retirement");
  manager.Clear();
  Expect(manager.GetSectionLightFieldCount() == 0,
         "world clear retires all geometry and independent light resources");
  UChunk sampled({0, -3, 0});
  SectionLightVolume volume;
  volume.cells.fill(0x72);
  Expect(sampled.ReplaceComputedLightVolume(volume, true, true),
         "install padded domains");
  const auto &sampled_field = sampled.GetSectionLightField();
  auto sky_revision = sampled_field.GetSkyRevision();
  auto block_revision = sampled_field.GetBlockRevision();
  auto full_revision = sampled_field.GetRevision();
  Expect(!sampled.ReplaceComputedLightVolume(volume, true, true) &&
             sampled_field.GetRevision() == full_revision,
         "padded no-op retains revisions");
  volume.cells[SectionLightVolume::Index(17, 1, 1)] = 0x79;
  Expect(
      sampled.ReplaceComputedLightVolume(volume, true, false) &&
          sampled_field.GetSkyRevision() == sky_revision + 1 &&
          sampled_field.GetBlockRevision() == block_revision &&
          sampled.GetLightData()[0] == 0x72,
      "halo-only sky change advances correct channel without interior changes");
  volume.cells.fill(0xa0);
  sampled.ReplaceComputedLightVolume(volume, false, true);
  Expect(
      sampled.GetLightData()[0] == 0xa2 &&
          sampled_field.GetPaddedData()
                  ->cells[SectionLightVolume::Index(17, 1, 1)] == 0xa9 &&
          sampled_field.GetPaddedDomains() == 3,
      "partial padded install preserves the other channel including its halo");
  sampled.SetLightLocal({0, 0, 0}, 4, 10);
  Expect(sampled_field.GetPaddedDomains() == 2,
         "legacy sky write invalidates sky padding while preserving block "
         "padding");
  {
    UChunkManager retained;
    std::array<uint8_t, CHUNK_VOLUME> lit{};
    lit.fill(0x90);
    for (int x = 0; x < 6; ++x)
      retained.ReplaceSectionLightData({x, -3, 0}, lit);
    const auto old_snapshot = retained.GetSectionLightField({0, -3, 0});
    const auto geometry_clock = retained.GetGeometryInputRevision();
    size_t calls = 0;
    Expect(retained.RetireAbsentSectionLightFields(2,
                                                   [&](glm::ivec3)
                                                   {
                                                     ++calls;
                                                     return false;
                                                   }) == 2 &&
               calls == 2 && retained.GetSectionLightFieldCount() == 4 &&
               retained.GetGeometryInputRevision() == geometry_clock &&
               retained.GetResidentChunkCount() == 0 &&
               old_snapshot->GetData()[0] == 0x90,
           "retirement obeys its visit budget without geometry mutation or "
           "changing retained snapshots");
    retained.EnsureChunk({3, -3, 0});
    const auto old_identity =
        retained.GetSectionLightField({4, -3, 0})->GetIncarnation();
    retained.ReleaseAbsentSectionLightField({4, -3, 0});
    auto replacement = retained.EnsureSectionLightField({4, -3, 0});
    Expect(replacement->GetIncarnation() != old_identity,
           "reallocated absent resource needs a fresh sweep identity");
    Expect(
        retained.RetireAbsentSectionLightFields(2, [](glm::ivec3 coord)
                                                { return coord.x == 5; }) == 1,
        "scan first retires the older unneeded field and drops resident work");
    calls = 0;
    Expect(retained.RetireAbsentSectionLightFields(1,
                                                   [&](glm::ivec3)
                                                   {
                                                     ++calls;
                                                     return false;
                                                   }) == 0 &&
               calls == 0 &&
               retained.GetSectionLightField({4, -3, 0}) == replacement,
           "an old queued incarnation must not retire its same-coordinate "
           "replacement");
    for (int i = 0; i < 10; ++i)
    {
      calls = 0;
      retained.RetireAbsentSectionLightFields(2,
                                              [&](glm::ivec3 coord)
                                              {
                                                ++calls;
                                                return coord.x == 5;
                                              });
      Expect(calls <= 2,
             "stale sweep entries must not exceed the visit budget");
    }
    Expect(retained.GetSectionLightFieldCount() == 2 &&
               retained.GetSectionLightField({3, -3, 0}) &&
               retained.GetSectionLightField({5, -3, 0}) &&
               !retained.GetSectionLightField({4, -3, 0}),
           "fair sweep retains demand and resident adapters while retiring "
           "stale incarnations");
    retained.Clear();
    Expect(retained.RetireAbsentSectionLightFields(
               100,
               [&](glm::ivec3)
               {
                 Expect(false, "clear must discard sweep records");
                 return false;
               }) == 0,
           "clear also retires pending sweep work");
  }
  return 0;
}
