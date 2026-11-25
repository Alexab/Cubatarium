#pragma once
#include "Test/CatalogQueueOwnersCommon.h"

static int TestCatalogRelightQueueOwners(const char *path)
{
  using namespace catalog_queue_test;
  auto definitions=std::make_shared<UBlockDefinitionStorage>();Publish(definitions,0);
  UBlockRegistry registry(nullptr,definitions);UBlockWorld world;Scene(world,definitions.get(),8);
  UAsyncRelightBuilder builder(1);Require(builder.GetMaxPipelineDepth()==8,"relight default depth eight per worker");
  RelightJobSpec spec;spec.min_world_y=0;spec.max_world_y=15;spec.include_skylight=false;
  spec.include_known_air_outputs=true;spec.known_air_output_sections={{0,0,0}};
  uint64_t job=9200;
  const auto submit=[&](int){spec.job_id=++job;builder.EnqueueJob(world,spec,registry);Require(builder.HasInFlightJob(job),"relight accepted");};
  std::promise<void> entered,release;auto started=entered.get_future();
  AsyncRelightLifecycleTestAccess::BlockWorker(builder,entered,release.get_future().share());
  Require(started.wait_for(std::chrono::seconds(10))==std::future_status::ready,"queued relight gate");
  std::vector<std::weak_ptr<const BlockDefinitionCatalog>> old;
  for(int i=0;i<8;++i){old.push_back(Publish(definitions,i));submit(i);}
  Publish(definitions,9);spec.job_id=++job;builder.EnqueueJob(world,spec,registry);
  Require(!builder.HasInFlightJob(job),"ninth relight work slot refused");ReleaseReads();Require(Live(old)==8,"eight queued relight captures");
  builder.CancelPending();Require(Live(old)==0,"queued relight cancellation retires captures");
  release.set_value();Require(builder.WaitIdleFor(std::chrono::seconds(10)),"queued relight idle");builder.TakeDiscardedJobs();
  std::promise<void> running_entered,running_release;auto running_started=running_entered.get_future();
  RunningEntered=&running_entered;RunningRelease=running_release.get_future().share();RunningHookClaimed=false;
  auto running=Publish(definitions,20);spec.job_id=9106;builder.EnqueueJob(world,spec,registry);
  Require(running_started.wait_for(std::chrono::seconds(10))==std::future_status::ready,"actual relight worker entered");
  Publish(definitions,21);ReleaseReads();Require(!running.expired(),"running relight owns captured catalog");
  builder.CancelPending();Require(!running.expired(),"cancelled running relight retains capture until exit");
  running_release.set_value();Require(builder.WaitIdleFor(std::chrono::seconds(10)),"running relight idle");RunningEntered=nullptr;
  builder.DrainCompleted(8);builder.TakeDiscardedJobs();Require(running.expired(),"late relight retires capture");
  const auto completed=CompletedOwners(builder,definitions,submit);
  // The legacy Enqueue entry point must replace the snapshot's prior input pin.
  auto prior=Publish(definitions,50);auto snapshot=UChunkRelightSnapshot::CaptureSections(world,spec,{{0,0,0}});
  snapshot.SetInputCatalog(definitions->GetCatalogSnapshot());auto captured=Publish(definitions,51);
  builder.SetCompletedCapacity(2);spec.job_id=++job;
  builder.Enqueue(std::move(snapshot),registry);Publish(definitions,52);ReleaseReads();
  Require(builder.WaitIdleFor(std::chrono::seconds(10)),"legacy relight idle");
  Require(prior.expired() && !captured.expired(),"legacy path replaces prior pin with one material catalog");
  auto results=builder.DrainCompleted(8);Require(results.size()==1 && results[0].content_certificate,"legacy compute creates real certificate");
  CatalogPublicationRootInspector inspector;inspector.RelightResult(results[0]);
  Require(inspector.Catalogs().Objects(CatalogObjectKind::Blocks)==1,"result and certificate alias single catalog");
  results.clear();Require(captured.expired(),"legacy returned owner release");
  WriteReport(path,{{"schema","catalog-relight-queue-owners.v1"},{"status","PASS"},{"default_depth_per_worker",8},{"shared_work_slots",8},{"queued_distinct_catalogs",8},{"ninth_refused",true},{"queued_cancel_payloads",0},{"running_cancel_payloads",0},{"completed",completed},{"legacy_prior_pin_retired",true},{"result_certificate_catalogs",1},{"scope","Actual relight capture, solver, completed queue and returned values;catalog payload only,not process heap acceptance"}});return 0;
}
