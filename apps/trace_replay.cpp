#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "helios/kv/page_allocator.hpp"
#include "helios/scheduler/continuous_scheduler.hpp"

namespace {

struct TraceRequest {
  std::uint64_t arrival;
  std::uint64_t id;
  std::size_t prompt_tokens;
  std::size_t max_new_tokens;
  std::uint64_t deadline;
  std::int64_t cancel_tick;
};

std::vector<std::string> Split(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ',')) fields.push_back(field);
  return fields;
}

std::vector<TraceRequest> LoadTrace(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("unable to open trace: " + path);
  std::string line;
  std::getline(input, line);  // Header.
  std::vector<TraceRequest> requests;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#') continue;
    const auto fields = Split(line);
    if (fields.size() != 6) throw std::runtime_error("trace row must contain six fields");
    requests.push_back(TraceRequest{std::stoull(fields[0]), std::stoull(fields[1]),
                                    std::stoull(fields[2]), std::stoull(fields[3]),
                                    std::stoull(fields[4]), std::stoll(fields[5])});
  }
  std::stable_sort(requests.begin(), requests.end(), [](const auto& left, const auto& right) {
    return left.arrival == right.arrival ? left.id < right.id : left.arrival < right.arrival;
  });
  return requests;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string path = argc > 1 ? argv[1] : "bench/traces/smoke.csv";
    const auto trace = LoadTrace(path);
    helios::kv::PageAllocator allocator(64, 16);
    helios::scheduler::ContinuousScheduler scheduler({8, 4, 256}, &allocator);

    std::size_t next = 0;
    constexpr std::uint64_t kMaximumTicks = 100000;
    while (scheduler.Now() < kMaximumTicks) {
      while (next < trace.size() && trace[next].arrival == scheduler.Now()) {
        const auto& request = trace[next++];
        scheduler.Submit(request.id, request.prompt_tokens, request.max_new_tokens,
                         request.deadline);
      }
      for (const auto& request : trace) {
        if (request.cancel_tick >= 0 &&
            static_cast<std::uint64_t>(request.cancel_tick) == scheduler.Now()) {
          scheduler.Cancel(request.id);
        }
      }

      scheduler.AdmitPrefillBatch();
      scheduler.CompleteDecodeStep(scheduler.FormDecodeBatch());
      const auto stats = scheduler.Stats();
      const std::size_t terminal = stats.completed + stats.cancelled + stats.deadline_exceeded;
      if (next == trace.size() && terminal == trace.size()) break;
      scheduler.AdvanceTime();
    }

    const auto stats = scheduler.Stats();
    std::cout << "{\"requests\":" << trace.size() << ",\"completed\":" << stats.completed
              << ",\"cancelled\":" << stats.cancelled << ",\"deadline_exceeded\":"
              << stats.deadline_exceeded << ",\"ticks\":" << scheduler.Now()
              << ",\"allocated_pages\":" << allocator.Stats().allocated_pages
              << ",\"invariants\":" << (allocator.VerifyInvariants() ? "true" : "false")
              << "}\n";
    return allocator.Stats().allocated_pages == 0 && allocator.VerifyInvariants() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "trace replay failed: " << error.what() << '\n';
    return 1;
  }
}

