#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>
#include <vector>

#include "helios/kv/page_allocator.hpp"

namespace helios::scheduler {

enum class RequestState {
  kQueued,
  kBlockedForMemory,
  kDecoding,
  kCompleted,
  kCancelled,
  kDeadlineExceeded
};

struct Request {
  std::uint64_t id;
  std::size_t prompt_tokens;
  std::size_t max_new_tokens;
  std::size_t generated_tokens = 0;
  RequestState state = RequestState::kQueued;
  std::uint64_t arrival_tick = 0;
  std::uint64_t deadline_tick = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t last_scheduled_tick = 0;
};

struct SchedulerConfig {
  std::size_t max_active_sequences = 32;
  std::size_t max_decode_batch = 16;
  std::size_t prefill_token_budget = 4096;
  std::size_t decode_page_reserve = 1;
};

struct SchedulerStats {
  std::size_t queued;
  std::size_t blocked;
  std::size_t active;
  std::size_t completed;
  std::size_t cancelled;
  std::size_t deadline_exceeded;
  std::uint64_t now;
};

class ContinuousScheduler {
 public:
  ContinuousScheduler(SchedulerConfig config, kv::PageAllocator* allocator);

  void Submit(std::uint64_t request_id, std::size_t prompt_tokens,
              std::size_t max_new_tokens,
              std::uint64_t deadline_tick = std::numeric_limits<std::uint64_t>::max());
  std::vector<std::uint64_t> AdmitPrefillBatch();
  std::vector<std::uint64_t> FormDecodeBatch();
  void CompleteDecodeStep(const std::vector<std::uint64_t>& batch);
  bool Cancel(std::uint64_t request_id);
  void AdvanceTime(std::uint64_t ticks = 1);

  [[nodiscard]] const Request& Get(std::uint64_t request_id) const;
  [[nodiscard]] SchedulerStats Stats() const;
  [[nodiscard]] std::uint64_t Now() const noexcept { return now_; }

 private:
  void ExpireDeadlines();
  void RequeueBlocked();
  void OrderByUrgency(std::deque<std::uint64_t>* queue) const;
  void RemoveFromQueue(std::deque<std::uint64_t>* queue, std::uint64_t request_id);

  SchedulerConfig config_;
  kv::PageAllocator* allocator_;
  std::unordered_map<std::uint64_t, Request> requests_;
  std::deque<std::uint64_t> prefill_queue_;
  std::deque<std::uint64_t> blocked_queue_;
  std::deque<std::uint64_t> decode_ready_queue_;
  std::uint64_t now_ = 0;
};

}  // namespace helios::scheduler
