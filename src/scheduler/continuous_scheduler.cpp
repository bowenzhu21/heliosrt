#include "helios/scheduler/continuous_scheduler.hpp"

#include <algorithm>
#include <stdexcept>

namespace helios::scheduler {

ContinuousScheduler::ContinuousScheduler(SchedulerConfig config, kv::PageAllocator* allocator)
    : config_(config), allocator_(allocator) {
  if (allocator_ == nullptr || config_.max_active_sequences == 0 ||
      config_.max_decode_batch == 0 || config_.prefill_token_budget == 0) {
    throw std::invalid_argument("scheduler configuration and allocator must be valid");
  }
}

void ContinuousScheduler::Submit(std::uint64_t request_id, std::size_t prompt_tokens,
                                 std::size_t max_new_tokens) {
  if (request_id == 0 || prompt_tokens == 0 || max_new_tokens == 0) {
    throw std::invalid_argument("request id and token counts must be non-zero");
  }
  if (requests_.count(request_id) != 0) throw std::invalid_argument("duplicate request id");
  requests_.emplace(request_id, Request{request_id, prompt_tokens, max_new_tokens});
  prefill_queue_.push_back(request_id);
}

std::vector<std::uint64_t> ContinuousScheduler::AdmitPrefillBatch() {
  RequeueBlocked();
  std::vector<std::uint64_t> admitted;
  std::size_t token_budget = config_.prefill_token_budget;
  std::size_t attempts = prefill_queue_.size();

  while (attempts-- > 0 && !prefill_queue_.empty() &&
         decode_ready_queue_.size() < config_.max_active_sequences) {
    const auto id = prefill_queue_.front();
    prefill_queue_.pop_front();
    auto& request = requests_.at(id);
    const std::size_t pages = allocator_->PagesRequired(request.prompt_tokens);

    if (request.prompt_tokens > token_budget && !admitted.empty()) {
      prefill_queue_.push_front(id);
      break;
    }
    if (!allocator_->CanAllocate(pages)) {
      request.state = RequestState::kBlockedForMemory;
      blocked_queue_.push_back(id);
      continue;
    }

    allocator_->Allocate(id, pages);
    request.state = RequestState::kDecoding;
    decode_ready_queue_.push_back(id);
    admitted.push_back(id);
    token_budget = request.prompt_tokens >= token_budget ? 0 : token_budget - request.prompt_tokens;
    if (token_budget == 0) break;
  }
  return admitted;
}

std::vector<std::uint64_t> ContinuousScheduler::FormDecodeBatch() const {
  const std::size_t count = std::min(config_.max_decode_batch, decode_ready_queue_.size());
  return std::vector<std::uint64_t>(decode_ready_queue_.begin(), decode_ready_queue_.begin() + count);
}

void ContinuousScheduler::CompleteDecodeStep(const std::vector<std::uint64_t>& batch) {
  for (const auto id : batch) {
    auto& request = requests_.at(id);
    if (request.state != RequestState::kDecoding) {
      throw std::logic_error("decode batch contains a non-decoding request");
    }

    const std::size_t tokens_before = request.prompt_tokens + request.generated_tokens;
    const std::size_t tokens_after = tokens_before + 1;
    const std::size_t pages_before = allocator_->PagesRequired(tokens_before);
    const std::size_t pages_after = allocator_->PagesRequired(tokens_after);
    if (pages_after > pages_before) {
      if (!allocator_->CanAllocate(1)) continue;  // Backpressure; retry this request next step.
      allocator_->Allocate(id, 1);
    }

    ++request.generated_tokens;
    RemoveFromQueue(&decode_ready_queue_, id);
    if (request.generated_tokens == request.max_new_tokens) {
      request.state = RequestState::kCompleted;
      allocator_->ReleaseAll(id);
    } else {
      decode_ready_queue_.push_back(id);  // Round-robin fairness.
    }
  }
}

bool ContinuousScheduler::Cancel(std::uint64_t request_id) {
  auto it = requests_.find(request_id);
  if (it == requests_.end()) return false;
  auto& request = it->second;
  if (request.state == RequestState::kCompleted || request.state == RequestState::kCancelled) {
    return false;
  }
  RemoveFromQueue(&prefill_queue_, request_id);
  RemoveFromQueue(&blocked_queue_, request_id);
  RemoveFromQueue(&decode_ready_queue_, request_id);
  allocator_->ReleaseAll(request_id);
  request.state = RequestState::kCancelled;
  return true;
}

const Request& ContinuousScheduler::Get(std::uint64_t request_id) const {
  return requests_.at(request_id);
}

SchedulerStats ContinuousScheduler::Stats() const {
  SchedulerStats stats{prefill_queue_.size(), blocked_queue_.size(), decode_ready_queue_.size(), 0,
                       0};
  for (const auto& [id, request] : requests_) {
    (void)id;
    if (request.state == RequestState::kCompleted) ++stats.completed;
    if (request.state == RequestState::kCancelled) ++stats.cancelled;
  }
  return stats;
}

void ContinuousScheduler::RequeueBlocked() {
  while (!blocked_queue_.empty()) {
    const auto id = blocked_queue_.front();
    blocked_queue_.pop_front();
    requests_.at(id).state = RequestState::kQueued;
    prefill_queue_.push_back(id);
  }
}

void ContinuousScheduler::RemoveFromQueue(std::deque<std::uint64_t>* queue,
                                          std::uint64_t request_id) {
  const auto it = std::find(queue->begin(), queue->end(), request_id);
  if (it != queue->end()) queue->erase(it);
}

}  // namespace helios::scheduler

