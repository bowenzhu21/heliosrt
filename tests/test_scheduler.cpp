#include <cstdlib>
#include <iostream>

#include "helios/kv/page_allocator.hpp"
#include "helios/scheduler/continuous_scheduler.hpp"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using helios::kv::PageAllocator;
  using helios::scheduler::ContinuousScheduler;
  using helios::scheduler::RequestState;
  using helios::scheduler::SchedulerConfig;

  PageAllocator allocator(16, 4);
  ContinuousScheduler scheduler(SchedulerConfig{2, 2, 16}, &allocator);
  scheduler.Submit(1, 4, 3);
  scheduler.Submit(2, 5, 2);
  scheduler.Submit(3, 4, 1);

  Check(scheduler.AdmitPrefillBatch().size() == 2, "active-sequence admission limit");
  Check(scheduler.FormDecodeBatch().size() == 2, "decode batch formation");
  scheduler.CompleteDecodeStep(scheduler.FormDecodeBatch());
  Check(scheduler.Cancel(2), "decode cancellation");
  Check(scheduler.Get(2).state == RequestState::kCancelled, "cancel state");

  scheduler.AdmitPrefillBatch();
  for (int step = 0; step < 8 && scheduler.Stats().active > 0; ++step) {
    scheduler.CompleteDecodeStep(scheduler.FormDecodeBatch());
    scheduler.AdmitPrefillBatch();
  }
  Check(scheduler.Get(1).state == RequestState::kCompleted, "first request completes");
  Check(scheduler.Get(3).state == RequestState::kCompleted, "replacement request admitted");
  Check(allocator.Stats().allocated_pages == 0, "completion and cancellation release pages");
  Check(allocator.VerifyInvariants(), "allocator remains valid under scheduler");

  std::cout << "PASS scheduler (admission, round-robin decode, cancellation, cleanup)\n";
}

