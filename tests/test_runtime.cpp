#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "helios/kv/page_allocator.hpp"
#include "helios/runtime/mock_executor.hpp"
#include "helios/scheduler/continuous_scheduler.hpp"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::string Hash(char digit) { return std::string(64, digit); }

}  // namespace

int main() {
  using helios::runtime::DecodeInput;
  using helios::runtime::EngineDescriptor;
  using helios::runtime::MockExecutor;

  EngineDescriptor descriptor{Hash('a'), Hash('b'), helios::runtime::Precision::kFp16, 32000, 8,
                              2048};
  MockExecutor executor(descriptor, {1, 2, 4, 8}, 1234);
  Check(executor.Capabilities().name == "mock", "mock capability name");
  Check(helios::runtime::SelectBatchBucket(3, {1, 2, 4, 8}) == 4, "bucket selection");

  const std::vector<DecodeInput> batch{{1, 42, 12}, {2, 99, 31}, {3, 7, 100}};
  const auto first = executor.Decode(batch);
  const auto second = executor.Decode(batch);
  Check(first.size() == batch.size(), "one output per active sequence");
  for (std::size_t index = 0; index < first.size(); ++index) {
    Check(first[index].sequence_id == batch[index].sequence_id, "output order is stable");
    Check(first[index].next_token == second[index].next_token, "mock decode is deterministic");
  }

  bool rejected_duplicate = false;
  try {
    executor.Decode({DecodeInput{1, 1, 0}, DecodeInput{1, 2, 0}});
  } catch (const std::invalid_argument&) {
    rejected_duplicate = true;
  }
  Check(rejected_duplicate, "duplicate sequences are rejected");

  bool rejected_profile = false;
  try {
    executor.Decode({DecodeInput{1, 1, 2048}});
  } catch (const std::out_of_range&) {
    rejected_profile = true;
  }
  Check(rejected_profile, "out-of-profile position is rejected");

  helios::kv::PageAllocator allocator(32, 16);
  helios::scheduler::ContinuousScheduler scheduler({4, 4, 128}, &allocator);
  scheduler.Submit(10, 16, 2, 20);
  scheduler.Submit(11, 16, 2, 20);
  scheduler.AdmitPrefillBatch();
  const auto scheduled = scheduler.FormDecodeBatch();
  std::vector<DecodeInput> scheduled_inputs;
  for (const auto id : scheduled) scheduled_inputs.push_back(DecodeInput{id, 1, 16});
  Check(executor.Decode(scheduled_inputs).size() == scheduled.size(),
        "scheduler batch satisfies executor contract");

  std::cout << "PASS runtime (descriptor validation, buckets, deterministic decode contract)\n";
}
