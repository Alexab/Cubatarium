#include "World/Chunks/ChunkBuffer.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "WorldGen/Core/PopulationScratchBudget.h"

#include <iostream>
#include <vector>

namespace
{

constexpr const char *kTestName = "chunk_buffer_capture_test";

void Expect(bool ok, const char *msg)
{
  if (!ok)
  {
    std::cerr << kTestName << " FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

} // namespace

int main(int argc,char **argv)
{
  if(argc>1 && std::string(argv[1])=="--scratch-budget-only") {
    using namespace cutum;
    auto budget=std::make_shared<UBufferAllocationBudget>(256*1024);
    auto peer=std::make_shared<BufferBudgetJob>(); budget->Reserve(*peer,64*1024);
    UBlockWorld scratch; UChunkBuffer transfer;
    bool refused=false; size_t sections=0;
    {
      UBufferBudgetScope scope({budget,std::make_shared<BufferBudgetJob>()});
      UPopulationScratchBudget scratch_budget(scratch.GetChunkManager(),ActiveBufferBudget);
      scratch.SetCaptureBuffer(&transfer);
      try {
        for(int n=0;n<64;++n) scratch.SetBlock({n*CHUNK_SIZE,-40,0},BLOCK_AIR);
      } catch(const UBufferBudgetExceeded &) {refused=true;}
      sections=scratch.GetChunkManager().GetResidentChunkCount();
      std::cout << "scratch_air_sections=" << sections << " transfer_owned=" << transfer.GetOwnedAllocationBytes()
          << " reserved=" << budget->UsedBytes() << " refused=" << refused << std::endl;
      scratch.SetCaptureBuffer(nullptr); scratch.Clear(); transfer.Clear();
    }
    budget->Release(*peer,64*1024);
    Expect(refused && sections<64 && budget->UsedBytes()==0 && budget->PeakBytes()<=256*1024 &&
        budget->LimitBytes()==256*1024,"AIR-only scratch allocation obeys generation quota");
    scratch.SetBlock({1000,-40,0},BLOCK_AIR);
    Expect(budget->UsedBytes()==0 && scratch.GetChunkManager().GetResidentChunkCount()==1,
        "scratch hook detaches and ordinary AIR materialization remains unchanged");
    scratch.Clear();
    std::cout << "scratch allocation budget PASS" << std::endl; return 0;
  }
  cutum::UBlockWorld world;
  cutum::UChunkBuffer buffer;
  world.SetCaptureBuffer(&buffer);

  const glm::ivec3 solid(1, 4, 2);
  const glm::ivec3 fluid_pos(1, 5, 2);
  world.SetBlock(solid, 8);
  world.SetBlock(fluid_pos, 9);
  const auto fluid =
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Water);
  world.SetFluidState(fluid_pos, fluid);

  Expect(buffer.GetBlock(solid) == 8, "solid mirrored to capture buffer");
  Expect(buffer.GetBlock(fluid_pos) == 9, "fluid block mirrored");
  Expect(buffer.GetFluidPacked(fluid_pos) == cutum::PackFluidCellState(fluid),
         "fluid packed mirrored");

  world.SetBlock(solid, cutum::BLOCK_AIR);
  Expect(buffer.GetBlock(solid) == cutum::BLOCK_AIR, "AIR erase mirrored");

  world.SetCaptureBuffer(nullptr);
  world.SetBlock(glm::ivec3(3, 3, 3), 8);
  Expect(buffer.GetBlock(glm::ivec3(3, 3, 3)) == cutum::BLOCK_AIR,
         "no mirror after capture cleared");

  cutum::UBlockWorld live;
  buffer.SetBlock(glm::ivec3(0, 1, 0), 8);
  buffer.ApplyTo(live);
  Expect(live.GetBlock(glm::ivec3(0, 1, 0)) == 8, "ApplyTo still works");

  cutum::UChunkBuffer tracked;
  Expect(tracked.GetOwnedAllocationBytes()==0,"empty buffer allocates no payload");
  for(int n=0;n<1024;++n) tracked.SetBlock({n%16,n/256,(n/16)%16},8);
  tracked.SetFluidPacked({1,0,0},cutum::PackFluidCellState(fluid));
  tracked.SetLightPacked({1,0,0},0x9A);
  const auto allocated=tracked.GetAllocationSnapshot();
  const auto owned=tracked.GetOwnedAllocationBytes();
  Expect(allocated.live_bytes>1024*sizeof(cutum::BlockId) && owned>allocated.live_bytes,
         "accounting includes actual map nodes buckets and fixed storage");
  auto copied=tracked;
  copied.SetBlock({1,0,0},cutum::BLOCK_AIR);
  Expect(tracked.GetBlock({1,0,0})==8 && copied.GetBlock({1,0,0})==cutum::BLOCK_AIR &&
      tracked.GetFluidPacked({1,0,0})==cutum::PackFluidCellState(fluid),
      "deep buffer copy owns independent mutable maps");
  cutum::UChunkBuffer assigned; assigned=tracked;
  Expect(assigned.GetLightPacked({1,0,0})==0x9A,"copy assignment preserves sparse light");
  auto moved=std::move(tracked);
  Expect(tracked.GetOwnedAllocationBytes()==0 && moved.GetOwnedAllocationBytes()==owned &&
      moved.GetAllocationSnapshot().peak_bytes==allocated.peak_bytes,
      "move transfers exact allocation owner without allocating another payload");
  tracked.Clear(); tracked.SetBlock({9,9,9},9);
  Expect(moved.GetBlock({9,9,9})!=9 && tracked.GetBlock({9,9,9})==9,
         "moved-from buffer can be cleared and reused independently");
  std::vector<cutum::UChunkBuffer> relocations;
  relocations.push_back(std::move(moved)); relocations.reserve(64);
  Expect(relocations[0].GetOwnedAllocationBytes()==owned &&
      relocations[0].GetAllocationSnapshot().peak_bytes==allocated.peak_bytes,
      "container relocation keeps resource identity and allocation charge");
  cutum::UChunkBuffer destination; destination.SetBlock({2,2,2},9);
  destination=std::move(relocations[0]);
  cutum::UBlockWorld copy_world; destination.ApplyTo(copy_world);
  Expect(copy_world.GetBlock({1,0,0})==8 && destination.GetOwnedAllocationBytes()==owned,
         "move assignment and real ApplyTo retain data and exact charge");
  destination.Clear(); copied.Clear(); assigned.Clear(); tracked.Clear();
  Expect(destination.GetOwnedAllocationBytes()==0 && copied.GetOwnedAllocationBytes()==0 &&
      assigned.GetOwnedAllocationBytes()==0 && tracked.GetOwnedAllocationBytes()==0,
      "clear releases map nodes buckets and fixed payload storage");
  std::cout << "chunk_buffer_allocator nodes_buckets=" << allocated.live_bytes
      << " peak=" << allocated.peak_bytes << " owned=" << owned
      << " inline=" << sizeof(cutum::UChunkBuffer) << " clear=0 copy_move=PASS\n";
  auto quota=std::make_shared<cutum::UBufferAllocationBudget>(64*1024);
  auto first_job=std::make_shared<cutum::BufferBudgetJob>();
  auto second_job=std::make_shared<cutum::BufferBudgetJob>();
  cutum::UChunkBuffer first_buffer,second_buffer;
  { cutum::UBufferBudgetScope scope({quota,first_job}); first_buffer.SetBlock({1,1,1},8); }
  const auto first_reserved=quota->UsedBytes();
  Expect(first_reserved>=first_buffer.GetOwnedAllocationBytes(),"quota covers fixed storage and allocator payload");
  bool refused=false;
  try { cutum::UBufferBudgetScope scope({quota,second_job}); second_buffer.SetBlock({2,2,2},9); }
  catch(const cutum::UBufferBudgetExceeded &) { refused=true; }
  Expect(refused && second_buffer.GetOwnedAllocationBytes()==0 && quota->UsedBytes()==first_reserved,
         "competing buffer refuses before allocating and refunds failed construction");
  first_buffer.Clear();
  Expect(quota->UsedBytes()==0,"actual destruction releases all fixed and chunked credits");
  { cutum::UBufferBudgetScope scope({quota,second_job}); second_buffer.SetBlock({2,2,2},9); }
  auto charged_move=std::move(second_buffer);
  Expect(quota->UsedBytes()>0 && charged_move.GetBlock({2,2,2})==9,
         "move retains budget owner beyond thread-local scope");
  second_buffer.Clear();
  Expect(quota->UsedBytes()>0,"clearing moved-from object cannot release live payload credit");
  charged_move.Clear();
  Expect(quota->UsedBytes()==0 && quota->PeakBytes()<=quota->LimitBytes(),
         "destination clear releases quota without overshoot");
  std::cout << "chunk_buffer_compute_quota refused=1 moved_owner=1 released=0 PASS\n";

  std::cout << kTestName << " OK" << std::endl;
  return 0;
}
