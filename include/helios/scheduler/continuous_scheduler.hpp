#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "helios/kv/page_allocator.hpp"

namespace helios::scheduler {

enum class RequestState { kQueued, kBlockedForMemory, kDecoding, kCompleted, kCancelled };

struct Request {
  std::uint64_t id;
  std::size_t prompt_tokens;
  std::size_t max_new_tokens;
  std::size_t generated_tokens = 0;
  RequestState state = RequestState::kQueued;
};

struct SchedulerConfig {
  std::size_t max_active_sequences = 32;
  std::size_t max_decode_batch = 16;
  std::size_t prefill_token_budget = 4096;
};

struct SchedulerStats {
  std::size_t queued;
  std::size_t blocked;
  std::size_t active;
  std::size_t completed;
  std::size_t cancelled;
};

class ContinuousScheduler {
 public:
  ContinuousScheduler(SchedulerConfig config, kv::PageAllocator* allocator);

  void Submit(std::uint64_t request_id, std::size_t prompt_tokens,
              std::size_t max_new_tokens);
  std::vector<std::uint64_t> AdmitPrefillBatch();
  std::vector<std::uint64_t> FormDecodeBatch() const;
  void CompleteDecodeStep(const std::vector<std::uint64_t>& batch);
  bool Cancel(std::uint64_t request_id);

  [[nodiscard]] const Request& Get(std::uint64_t request_id) const;
  [[nodiscard]] SchedulerStats Stats() const;

 private:
  void RequeueBlocked();
  void RemoveFromQueue(std::deque<std::uint64_t>* queue, std::uint64_t request_id);

  SchedulerConfig config_;
  kv::PageAllocator* allocator_;
  std::unordered_map<std::uint64_t, Request> requests_;
  std::deque<std::uint64_t> prefill_queue_;
  std::deque<std::uint64_t> blocked_queue_;
  std::deque<std::uint64_t> decode_ready_queue_;
};

}  // namespace helios::scheduler

