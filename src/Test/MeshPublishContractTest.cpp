#include "Render/Mesh/MeshPublishContract.h"
#include "Render/Mesh/SeamCoverageManifest.h"
#include "World/Lighting/LightReferenceCompare.h"
#include "World/Streaming/ColumnVisualState.h"
#include "World/Streaming/MeshLitGate.h"

#include <cstdio>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    ++gFails;
  }
}

} // namespace

int main()
{
  using cutum::DecideMeshPublishAction;
  using cutum::MeshPublishAction;
  using cutum::MeshPublishLiveFreeDisjoint;
  using cutum::MeshPublishRevs;
  using cutum::ShouldAcceptMeshPublish;
  using cutum::ShouldExpirePriorLitHold;
  using cutum::ShouldPublishedEmptyAfterPriorLitExpire;
  using cutum::ShouldRejectDarkMeshCommit;
  using cutum::ShouldRetainPriorLitOverUnlitCandidate;
  using cutum::ColumnVisualAllowsPreferKick;
  using cutum::ColumnVisualState;
  using cutum::ColumnVisualStateForFullyDarkDrawable;
  using cutum::ColumnVisualForbidsTicketWithoutFifo;

  MeshPublishRevs a{1, 2, 3};
  MeshPublishRevs b{1, 2, 3};
  MeshPublishRevs c{1, 9, 3};
  Expect(ShouldAcceptMeshPublish(a, b, true), "accept matching revs");
  Expect(!ShouldAcceptMeshPublish(a, c, true), "reject light_rev mismatch");
  Expect(!ShouldAcceptMeshPublish(a, b, false), "reject when GPU not ready");

  Expect(DecideMeshPublishAction(true, true, true) == MeshPublishAction::Replace,
         "Replace on accept+drawable");
  Expect(DecideMeshPublishAction(true, false, true) ==
             MeshPublishAction::PublishedEmpty,
         "PublishedEmpty on accept+empty+prior");
  Expect(DecideMeshPublishAction(false, true, true) == MeshPublishAction::Retain,
         "Retain on reject");
  Expect(MeshPublishLiveFreeDisjoint(true, true) == false, "Live∩Free=0");
  Expect(MeshPublishLiveFreeDisjoint(false, true), "free non-live ok");

  Expect(ColumnVisualStateForFullyDarkDrawable(true, true) ==
             ColumnVisualState::NeedRelight,
         "FD drawable → NeedRelight");
  Expect(!ColumnVisualAllowsPreferKick(ColumnVisualState::NeedRemesh, true),
         "PreferKick blocked outside Publishing");
  Expect(ColumnVisualAllowsPreferKick(ColumnVisualState::Publishing, true),
         "PreferKick ok while Publishing+progress");
  Expect(ColumnVisualForbidsTicketWithoutFifo(ColumnVisualState::NeedRelight,
                                              true, false),
         "ticket without fifo forbidden");

  Expect(!ShouldExpirePriorLitHold(10), "hold young");
  Expect(ShouldExpirePriorLitHold(90), "hold expires at deadline");
  Expect(ShouldRetainPriorLitOverUnlitCandidate(true, false, true, 0),
         "retain young prior-lit");
  Expect(!ShouldRetainPriorLitOverUnlitCandidate(true, false, true, 90),
         "expire prior-lit hold latch");
  Expect(ShouldRejectDarkMeshCommit(true, false, true, false, 90),
         "expire still rejects dark Replace over prior");
  Expect(ShouldPublishedEmptyAfterPriorLitExpire(true, false, true, 90),
         "expire → PublishedEmpty path");
  Expect(!ShouldPublishedEmptyAfterPriorLitExpire(true, false, true, 10),
         "young hold → not PublishedEmpty yet");
  using cutum::ShouldRejectDarkOnGeomStaleAccept;
  Expect(ShouldRejectDarkOnGeomStaleAccept(true, true, true),
         "v6 geom-stale dark reject over lit");
  Expect(!ShouldRejectDarkOnGeomStaleAccept(false, true, true),
         "v6 non-dark geom-stale ok");

  using cutum::ArtifactManifest;
  using cutum::PublicationEpochs;
  using cutum::PublicationValidation;
  using cutum::ValidatePublicationCandidate;
  ArtifactManifest m{};
  m.world_epoch = 1;
  m.content_rev = 2;
  m.light_valid = true;
  ArtifactManifest m2 = m;
  Expect(ValidatePublicationCandidate(m, m2) == PublicationValidation::Ok,
         "manifest match ok");
  m2.content_rev = 9;
  Expect(ValidatePublicationCandidate(m, m2) ==
             PublicationValidation::SourceMismatch,
         "manifest content mismatch");
  m2 = m;
  m.light_valid = false;
  Expect(ValidatePublicationCandidate(m, m2) ==
             PublicationValidation::LightInvalid,
         "light_valid required");
  PublicationEpochs draw{};
  PublicationEpochs live{};
  live.resident_table_revision = 3;
  draw.resident_table_revision = 1;
  m.light_valid = true;
  Expect(ValidatePublicationCandidate(m, m, draw, live) ==
             PublicationValidation::TableEpochStale,
         "stale resident table rejected");

  using cutum::SeamCoverageManifest;
  using cutum::SeamCoveragePeerSatisfied;
  SeamCoverageManifest seam{};
  seam.peer_coverage_gen[0] = 7;
  Expect(!SeamCoveragePeerSatisfied(seam, 0, 3), "seam peer too old");
  Expect(SeamCoveragePeerSatisfied(seam, 0, 7), "seam peer ok");
  Expect(SeamCoveragePeerSatisfied(seam, 1, 0), "unused face ok");

  using cutum::CountLightFieldMismatches;
  using cutum::ShouldRejectStaleHaloLight;
  const uint8_t light_a[4] = {1, 2, 3, 4};
  const uint8_t light_b[4] = {1, 2, 3, 9};
  Expect(CountLightFieldMismatches(light_a, light_a, 4) == 0, "light ref match");
  Expect(CountLightFieldMismatches(light_a, light_b, 4) == 1, "light ref one miss");
  Expect(ShouldRejectStaleHaloLight(5, 5, 1, 2), "stale halo reject");
  Expect(!ShouldRejectStaleHaloLight(5, 5, 2, 2), "fresh halo ok");

  if (gFails != 0)
  {
    std::fprintf(stderr, "%d MeshPublishContract / ColumnVisualState fails\n",
                 gFails);
    return 1;
  }
  std::puts("MeshPublishContractTest OK");
  return 0;
}
