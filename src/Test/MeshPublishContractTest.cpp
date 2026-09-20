#include "Render/Mesh/MeshPublishContract.h"
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

  if (gFails != 0)
  {
    std::fprintf(stderr, "%d MeshPublishContract / ColumnVisualState fails\n",
                 gFails);
    return 1;
  }
  std::puts("MeshPublishContractTest OK");
  return 0;
}
