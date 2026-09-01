#include <iostream>

#include "helios/kv/page_allocator.hpp"
#include "helios/scheduler/continuous_scheduler.hpp"

int main() {
  helios::kv::PageAllocator allocator(128, 16);
  helios::scheduler::ContinuousScheduler scheduler({8, 4, 2048}, &allocator);

  for (std::uint64_t id = 1; id <= 12; ++id) {
    scheduler.Submit(id, 32 + (id % 4) * 16, 8 + (id % 5));
  }

  std::size_t iterations = 0;
  while (scheduler.Stats().completed < 12) {
    scheduler.AdmitPrefillBatch();
    scheduler.CompleteDecodeStep(scheduler.FormDecodeBatch());
    scheduler.AdvanceTime();
    ++iterations;
  }

  const auto pages = allocator.Stats();
  std::cout << "HeliosRT CPU scheduler simulation\n"
            << "requests_completed=12\n"
            << "decode_iterations=" << iterations << '\n'
            << "kv_pages_allocated_after_completion=" << pages.allocated_pages << '\n'
            << "allocator_invariants=" << (allocator.VerifyInvariants() ? "valid" : "invalid")
            << '\n';
}
