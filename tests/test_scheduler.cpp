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

  PageAllocator deadline_allocator(16, 4);
  ContinuousScheduler deadlines(SchedulerConfig{3, 1, 16}, &deadline_allocator);
  deadlines.Submit(10, 4, 5, 20);
  deadlines.Submit(11, 4, 1, 3);
  deadlines.AdmitPrefillBatch();
  const auto urgent_batch = deadlines.FormDecodeBatch();
  Check(urgent_batch.size() == 1 && urgent_batch.front() == 11,
        "earliest deadline is decoded first");
  deadlines.CompleteDecodeStep(urgent_batch);
  Check(deadlines.Get(11).state == RequestState::kCompleted, "urgent request completes");
  deadlines.AdvanceTime(20);
  Check(deadlines.Get(10).state == RequestState::kDeadlineExceeded, "deadline expiry state");
  Check(deadline_allocator.Stats().allocated_pages == 0, "deadline expiry releases pages");

  PageAllocator blocked_allocator(3, 4);
  ContinuousScheduler blocked(SchedulerConfig{2, 2, 16}, &blocked_allocator);
  blocked.Submit(20, 8, 1, 50);
  blocked.Submit(21, 8, 1, 50);
  Check(blocked.AdmitPrefillBatch().size() == 1, "first request consumes KV capacity");
  Check(blocked.Stats().blocked == 1, "second request blocks for memory");
  blocked.CompleteDecodeStep(blocked.FormDecodeBatch());
  Check(blocked.AdmitPrefillBatch().size() == 1, "blocked request is admitted after pages free");
  blocked.CompleteDecodeStep(blocked.FormDecodeBatch());
  Check(blocked_allocator.Stats().allocated_pages == 0, "memory-blocked flow leaks no pages");

  std::cout << "PASS scheduler (admission, deadlines, memory blocking, cancellation, cleanup)\n";
}
