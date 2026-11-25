#pragma once
#include "Test/CatalogQueueOwnersCommon.h"

static void TestCatalogMeshQueueOwners(const char *path)
{
  using namespace catalog_queue_test;
  auto definitions=std::make_shared<UBlockDefinitionStorage>();Publish(definitions,0);
  UBlockRegistry registry(nullptr,definitions);UBlockWorld world;Scene(world,definitions.get(),8);
  UAsyncMeshBuilder builder(1);Require(builder.GetMaxPipelineDepth()==6,"mesh default depth six per worker");
  const auto submit=[&](int i){Require(builder.Enqueue(ChunkMeshSnapshot::Capture(world,{i,0,0},1),registry),"mesh accepted");};
  Gate queue;MeshBuilderAdmissionTestAccess::BlockWorker(builder,queue);queue.Wait();
  std::vector<std::weak_ptr<const BlockDefinitionCatalog>> old;
  for(int i=0;i<8;++i){old.push_back(Publish(definitions,i));submit(i);}
  Publish(definitions,9);
  Require(builder.EnqueueDetailed(ChunkMeshSnapshot::Capture(world,{9,0,0},1),registry)==MeshEnqueueResult::WorkSlotRejected,"ninth mesh shared work slot refused");
  ReleaseReads();Require(Live(old)==8,"eight queued closures own distinct catalogs");
  builder.CancelPending();Require(Live(old)==0,"queued cancellation releases captures without executing");
  queue.Release();Require(builder.WaitIdleFor(std::chrono::seconds(10)),"queue cancellation idle");
  builder.TakeDiscardedCoords();
  struct PausedMesher final:IUChunkMesher {
    Gate gate;
    const char *BackendName()const override{return "catalog-owner-real-cpu";}
    std::vector<GreedyQuad> BuildChunkMesh(const UBlockWorld &w,glm::ivec3 c,UBlockRegistry &r,const BlockDefinitionCatalog *p)override
    {gate.Pause();return UGreedyMesher::BuildChunkMesh(w,c,r,p);}
    std::vector<GreedyQuad> BuildChunkMesh(const ChunkMeshSnapshot &s,UBlockRegistry &r,const BlockDefinitionCatalog *p)override
    {gate.Pause();return UGreedyMesher::BuildChunkMesh(s,r,p);}
  } mesher;
  builder.SetMesher(&mesher);auto running=Publish(definitions,20);submit(0);mesher.gate.Wait();
  Publish(definitions,21);ReleaseReads();Require(!running.expired(),"running mesher owns captured material catalog");
  builder.CancelPending();Require(!running.expired(),"running cancel keeps capture until worker exits");
  mesher.gate.Release();Require(builder.WaitIdleFor(std::chrono::seconds(10)),"running mesh idle");
  builder.DrainCompleted(8);builder.TakeDiscardedCoords();Require(running.expired(),"late cancelled mesh retires payload");builder.SetMesher(nullptr);
  const auto completed=CompletedOwners(builder,definitions,submit);

  // A field can have an older material publication than the geometry capture.
  auto field_catalog=Publish(definitions,50);
  RelightJobSpec spec;spec.min_world_y=world.GetVerticalProfile().MinY;spec.max_world_y=world.GetVerticalProfile().MaxY;
  {
    auto light=UChunkRelightSnapshot::CaptureSections(world,spec,{{0,0,0}}).Compute(registry);
    Require(light.inputs_complete && light.chunks.size()==1,"dual-catalog real solver");
    world.GetChunkManager().GetChunk({0,0,0})->ReplaceComputedLightVolume(*light.chunks[0].padded_light,true,true);
    Require(world.RecordLightCalculation(light,definitions->GetCatalogSnapshot())==1,"dual-catalog field installed");
  }
  auto field_snapshot=ChunkMeshSnapshot::Capture(world,{0,0,0},1,nullptr,nullptr,MeshLightInputs::Baked);
  Require(field_snapshot.renderLight.has_value(),"field captured before material publication replacement");
  auto material_catalog=Publish(definitions,51);Gate dual;MeshBuilderAdmissionTestAccess::BlockWorker(builder,dual);dual.Wait();
  Require(builder.Enqueue(std::move(field_snapshot),registry),"dual-catalog capture queued");
  world.Clear();Publish(definitions,52);ReleaseReads();
  Require(!field_catalog.expired() && !material_catalog.expired(),"one real queued mesh retains old field and new material catalogs");
  builder.CancelPending();Require(field_catalog.expired() && material_catalog.expired(),"dual capture cancellation retires both payloads");
  dual.Release();Require(builder.WaitIdleFor(std::chrono::seconds(10)),"dual idle");
  WriteReport(path,{{"schema","catalog-mesh-queue-owners.v1"},{"status","PASS"},{"default_depth_per_worker",6},{"shared_work_slots",8},{"queued_distinct_catalogs",8},{"ninth_refused",true},{"queued_cancel_payloads",0},{"running_cancel_payloads",0},{"completed",completed},{"dual_field_material_catalogs",2},{"dual_after_cancel",0},{"scope","Actual builder closures, CPU mesher, completed queue and returned values;catalog payload only,not GPU upload or process heap acceptance"}});
}
