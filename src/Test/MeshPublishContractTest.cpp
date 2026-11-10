#include "Render/Mesh/MeshPublishContract.h"
#include "Render/Mesh/SeamCoverageManifest.h"
#include "Render/Mesh/FluidColumnSummary.h"
#include "Core/FrameDeadline.h"
#include "World/Lighting/LightReferenceCompare.h"
#include "World/Streaming/ColumnVisualState.h"
#include "World/Streaming/MeshLitGate.h"
#include "World/Streaming/MeshWorkAdmission.h"

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
  // A34: observational dark cleared → Ok (MeshLitGate owns lit→dark).
  {
    using cutum::ClearObservationalDarkFromLightValid;
    ArtifactManifest dark_got = m;
    ArtifactManifest dark_exp = m;
    dark_got.light_valid = false;
    dark_exp.light_valid = true;
    Expect(ValidatePublicationCandidate(dark_got, dark_exp) ==
               PublicationValidation::LightInvalid,
           "pre-clear dark still LightInvalid");
    ClearObservationalDarkFromLightValid(dark_got, dark_exp);
    Expect(ValidatePublicationCandidate(dark_got, dark_exp) ==
               PublicationValidation::Ok,
           "A34 observational dark cleared accepts first publish");
  }
  // A34: provenance SourceMismatch still rejects after clear.
  {
    using cutum::ClearObservationalDarkFromLightValid;
    ArtifactManifest g = m;
    ArtifactManifest e = m;
    g.light_valid = false;
    e.source_geom_rev = 42;
    g.source_geom_rev = 7;
    ClearObservationalDarkFromLightValid(g, e);
    Expect(ValidatePublicationCandidate(g, e) ==
               PublicationValidation::SourceMismatch,
           "A34 provenance SourceMismatch kept");
  }
  Expect(!ShouldRejectDarkMeshCommit(true, false, false, false, 0),
         "A34 MeshLitGate: first dark without prior lit OK");
  Expect(ShouldRejectDarkMeshCommit(true, false, true, false, 0),
         "A34 MeshLitGate: dark over prior lit still rejects");
  PublicationEpochs draw{};
  PublicationEpochs live{};
  live.resident_table_revision = 3;
  draw.resident_table_revision = 1;
  m.light_valid = true;
  Expect(ValidatePublicationCandidate(m, m, draw, live) ==
             PublicationValidation::TableEpochStale,
         "stale resident table rejected");

  // A31-06: capture≠live between capture and commit must reject; draw==live
  // epochs must not mask SourceMismatch.
  {
    ArtifactManifest captured{};
    captured.world_epoch = 1;
    captured.chunk_xyz = {2, 0, 3};
    captured.incarnation = 7;
    captured.content_rev = 10;
    captured.source_geom_rev = 10;
    captured.source_light_rev = 5;
    captured.light_valid = true;
    captured.material_catalog_rev = 1;
    captured.neighbor_halo_rev = 42;
    ArtifactManifest live_m = captured;
    live_m.content_rev = 11;
    live_m.source_geom_rev = 11;
    PublicationEpochs draw_ep{};
    draw_ep.resident_table_revision = 3;
    draw_ep.transparent_order_key = 1;
    PublicationEpochs live_ep = draw_ep;
    Expect(ValidatePublicationCandidate(captured, live_m, draw_ep, live_ep) ==
               PublicationValidation::SourceMismatch,
           "A31-06 fault: capture≠live source must reject");
    live_ep.resident_table_revision = 4;
    ArtifactManifest same = captured;
    Expect(ValidatePublicationCandidate(same, same, draw_ep, live_ep) ==
               PublicationValidation::TableEpochStale,
           "A31-06 fault: table epoch stale with matching source");
  }

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

  // A25 R3: reference fill helper + order-epoch stale draw rejection.
  {
    using cutum::FillReferenceSkyBlockLight;
    using cutum::PublicationEpochs;
    uint8_t ref[8] = {};
    FillReferenceSkyBlockLight(ref, 2, 15, 0, 0, 0, 12);
    Expect(ref[0] != 0, "reference packed light written");
    PublicationEpochs draw_o{};
    PublicationEpochs live_o{};
    live_o.transparent_order_key = 5;
    draw_o.transparent_order_key = 2;
    ArtifactManifest okm{};
    okm.light_valid = true;
    Expect(ValidatePublicationCandidate(okm, okm, draw_o, live_o) ==
               PublicationValidation::OrderEpochStale,
           "A25 P3: stale order key rejected");
  }

  // A25 R4: CapDirtyAdmitUnderThrash
  {
    using cutum::CapDirtyAdmitUnderThrash;
    Expect(CapDirtyAdmitUnderThrash(1, 25) == 4, "FD repair raises to 4");
    Expect(CapDirtyAdmitUnderThrash(8, 25, /*dropped=*/900) == 2,
           "thrash caps at 2 over FD raise");
    Expect(CapDirtyAdmitUnderThrash(8, 0, /*dropped=*/900) == 2,
           "thrash alone caps at 2");
  }

  // A26 N2: deferred retirement + stale draw reject
  {
    using cutum::ShouldDeferMeshRetirement;
    using cutum::ShouldRejectStaleDrawCommands;
    Expect(ShouldDeferMeshRetirement(/*free=*/3, /*live=*/5, /*fence=*/2,
                                     /*still_live=*/false),
           "fence behind free → defer");
    Expect(!ShouldDeferMeshRetirement(3, 5, 3, false), "fence ready → free ok");
    Expect(ShouldDeferMeshRetirement(5, 5, 9, true), "still live → defer");
    PublicationEpochs draw{};
    PublicationEpochs live{};
    live.resident_table_revision = 4;
    draw.resident_table_revision = 2;
    Expect(ShouldRejectStaleDrawCommands(draw, live), "stale table draw");
    draw.resident_table_revision = 4;
    Expect(!ShouldRejectStaleDrawCommands(draw, live), "fresh table draw");
  }

  // A26 N3: seam full coverage + reference BFS + peer-ready
  {
    using cutum::PeerReadyBeforeSubscribe;
    using cutum::PropagateReferenceBlockLight;
    using cutum::SeamCoverageFullySatisfied;
    using cutum::ShouldCommitSeamCoverage;
    SeamCoverageManifest debt{};
    for (int f = 0; f < 6; ++f)
    {
      debt.peer_coverage_gen[static_cast<size_t>(f)] = 2;
    }
    debt.seam_artifact_generation = 5;
    uint64_t pubs[6] = {2, 2, 2, 2, 2, 1};
    Expect(!SeamCoverageFullySatisfied(debt, pubs), "one peer lag");
    pubs[5] = 2;
    Expect(SeamCoverageFullySatisfied(debt, pubs), "all peers ok");
    Expect(ShouldCommitSeamCoverage(debt, 5), "seam commit gen ok");
    Expect(!ShouldCommitSeamCoverage(debt, 4), "seam commit gen lag");
    using cutum::MakeProvisionalSeamCoverage;
    const auto prov =
        MakeProvisionalSeamCoverage(glm::ivec3(1, 2, 3), 9ull, 7ull);
    Expect(prov.provisional && prov.seam_artifact_generation == 7,
           "A30 provisional seam extract");
    Expect(ShouldCommitSeamCoverage(prov, 7), "provisional commit ok");
    Expect(PeerReadyBeforeSubscribe(2, 2), "peer ready");
    Expect(!PeerReadyBeforeSubscribe(0, 2), "peer not ready");

    uint8_t solid[8] = {};
    uint8_t light[8] = {};
    const size_t upd =
        PropagateReferenceBlockLight(light, solid, /*n=*/2, 0, 0, 0, 8);
    Expect(upd >= 1, "BFS wrote source");
    Expect((light[0] & 0x0F) == 8, "source level kept");
  }

  // A26 N4: resumable cursor + unified admission pools
  {
    using cutum::AdvanceWorkCursor;
    using cutum::CanAdmitUnifiedWork;
    using cutum::ResumableWorkCursor;
    using cutum::ShouldResumeWorkCursor;
    using cutum::UnifiedAdmissionPools;
    ResumableWorkCursor cur{};
    cur.active = true;
    cur.manifest_generation = 9;
    cur.total = 10;
    cur.index = 3;
    Expect(ShouldResumeWorkCursor(cur, 9), "resume same gen");
    Expect(!ShouldResumeWorkCursor(cur, 10), "stale gen");
    AdvanceWorkCursor(cur, 7);
    Expect(!cur.active && cur.index == 10, "cursor completed");
    UnifiedAdmissionPools pools{};
    Expect(CanAdmitUnifiedWork(pools, 0, 0, 0, 0), "empty pools admit");
    Expect(!CanAdmitUnifiedWork(pools, 8, 0, 0, 0), "snapshot full");
  }

  // A26 N5: fluid wrap / material / hitch / worker enqueue
  {
    using cutum::DrainOneFluidSummaryWorker;
    using cutum::FluidColumnSummary;
    using cutum::FluidColumnSummaryRequest;
    using cutum::FluidMaterialIdentityChanged;
    using cutum::FluidSummaryWorkerJob;
    using cutum::ShouldDeferFluidFullColumnScan;
    using cutum::ShouldRejectFluidMapHitch;
    using cutum::TryEnqueueFluidSummaryWorker;
    using cutum::WrapFluidSurfaceOrigin;
    int ox = -1;
    int oz = 17;
    WrapFluidSurfaceOrigin(ox, oz, 16, 16);
    Expect(ox == 15 && oz == 1, "toroidal wrap");
    Expect(FluidMaterialIdentityChanged(1, 2, 0, 0), "hash change");
    Expect(FluidMaterialIdentityChanged(1, 1, 3, 4), "rep change");
    Expect(!FluidMaterialIdentityChanged(1, 1, 3, 3), "same identity");
    Expect(ShouldDeferFluidFullColumnScan(64, false, true), "tall defer");
    Expect(ShouldRejectFluidMapHitch(50.0, 8.0), "hitch reject");
    Expect(!ShouldRejectFluidMapHitch(3.0, 8.0), "under budget");
    FluidSummaryWorkerJob job{};
    FluidColumnSummaryRequest req{};
    req.y_min = 3;
    req.height = 16;
    // A39 P4: empty-flags enqueue is RejectedRetryable (never silent ready=false).
    Expect(!TryEnqueueFluidSummaryWorker(job, req, true),
           "empty-flags rejected");
    Expect(!job.enqueued, "empty job not flagged");
    // A29 U3: drain with flags fills ready summary.
    {
      std::vector<uint8_t> flags(static_cast<size_t>(16 * 16 * 16), 0);
      flags[static_cast<size_t>((8 * 16 + 0) * 16 + 0)] = 1;
      FluidSummaryWorkerJob filled{};
      Expect(TryEnqueueFluidSummaryWorker(filled, req, true, flags.data(),
                                          flags.size(), 42ull, 7),
             "flagged worker enqueued");
      FluidColumnSummary built{};
      Expect(DrainOneFluidSummaryWorker(built), "flagged drain");
      Expect(built.ready && built.tops[0] == 8, "worker filled tops");
    }
  }

    // A31 P1: mutually exclusive defect class.
  {
    using cutum::ChunkDefectClass;
    using cutum::ClassifyChunkDefect;
    Expect(ClassifyChunkDefect(true, false, false, false, false, false, false,
                               false) == ChunkDefectClass::GeometryMissing,
           "P1 geometry missing");
    Expect(ClassifyChunkDefect(true, true, true, false, false, false, false,
                               false) == ChunkDefectClass::GeometryCulled,
           "P1 culled");
    Expect(ClassifyChunkDefect(true, true, false, true, false, false, false,
                               false) == ChunkDefectClass::LightStaleOrInvalid,
           "P1 light");
    Expect(ClassifyChunkDefect(true, true, false, false, true, false, false,
                               false) == ChunkDefectClass::SeamPeerMissing,
           "P1 seam");
    Expect(ClassifyChunkDefect(true, true, false, false, false, false, false,
                               true) == ChunkDefectClass::LegalDark,
           "P1 legal dark wins");
  }

  // A36 S3: PublicationCandidateAccepted is the sole commit gate helper.
  {
    using cutum::PublicationCandidateAccepted;
    using cutum::PublicationValidation;
    Expect(PublicationCandidateAccepted(PublicationValidation::Ok),
           "Ok accepted");
    Expect(!PublicationCandidateAccepted(PublicationValidation::SourceMismatch),
           "SourceMismatch rejected");
    Expect(!PublicationCandidateAccepted(PublicationValidation::LightInvalid),
           "LightInvalid rejected");
  }

  // A39 P5: Cross expected≠got fault-inject — SourceMismatch when live desire drifts.
  {
    using cutum::PublicationCandidateAccepted;
    using cutum::PublicationValidation;
    using cutum::ValidateCrossOrShellPublication;
    const PublicationValidation mismatch = ValidateCrossOrShellPublication(
        /*got_geom=*/10, /*got_light=*/5, /*light_valid=*/true,
        /*expected_geom=*/11, /*expected_light=*/5);
    Expect(mismatch == PublicationValidation::SourceMismatch,
           "A39 Cross got≠expected SourceMismatch");
    Expect(!PublicationCandidateAccepted(mismatch),
           "A39 Cross mismatch rejected");
    const PublicationValidation ok =
        ValidateCrossOrShellPublication(10, 5, true, 10, 5);
    Expect(PublicationCandidateAccepted(ok), "A39 Cross got==expected Ok");
  }

  // A31 P3 / A35 R2: seam negatives — missing peer / one-of-six.
  {
    using cutum::SeamCoverageFullySatisfied;
    using cutum::SeamCoveragePeerSatisfied;
    SeamCoverageManifest missing{};
    for (int f = 0; f < 6; ++f)
    {
      missing.peer_coverage_gen[static_cast<size_t>(f)] = 4;
    }
    uint64_t pubs_missing[6] = {0, 0, 0, 0, 0, 0};
    Expect(!SeamCoverageFullySatisfied(missing, pubs_missing),
           "P3 missing peer gens must fail");
    Expect(!SeamCoveragePeerSatisfied(missing, 0, 0),
           "P3 face0 missing peer gen");
    uint64_t pubs_one[6] = {4, 4, 4, 4, 4, 3};
    Expect(!SeamCoverageFullySatisfied(missing, pubs_one),
           "P3 one-of-six lag must fail");
    pubs_one[5] = 4;
    Expect(SeamCoverageFullySatisfied(missing, pubs_one),
           "P3 all six peers ok");
  }

  // A35 R0/P4: fluid worker shutdown is joinable (no detach hang).
  {
    using cutum::EnsureFluidSummaryWorkerStarted;
    using cutum::ShutdownFluidSummaryWorker;
    using cutum::TryEnqueueFluidSummaryWorker;
    using cutum::FluidSummaryWorkerJob;
    using cutum::FluidColumnSummaryRequest;
    EnsureFluidSummaryWorkerStarted();
    FluidSummaryWorkerJob job{};
    FluidColumnSummaryRequest req{};
    req.height = 16;
    std::vector<uint8_t> flags(static_cast<size_t>(16 * 16 * 16), 0);
    flags[0] = 1;
    (void)TryEnqueueFluidSummaryWorker(job, req, true, flags.data(),
                                       flags.size());
    Expect(ShutdownFluidSummaryWorker(2000), "fluid worker shutdown joins");
    // Leave stopped — process exit must not destroy a joinable thread.
  }

  if (gFails != 0)
  {
    std::fprintf(stderr, "%d MeshPublishContract / ColumnVisualState fails\n",
                 gFails);
    return 1;
  }
  std::puts("MeshPublishContractTest OK");
  return 0;
}
