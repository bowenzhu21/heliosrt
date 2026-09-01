#include "helios/scheduler/continuous_scheduler.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace helios::scheduler {

ContinuousScheduler::ContinuousScheduler(SchedulerConfig config, kv::PageAllocator* allocator)
    : config_(config), allocator_(allocator) {
  if (allocator_ == nullptr || config_.max_active_sequences == 0 ||
      config_.max_decode_batch == 0 || config_.prefill_token_budget == 0) {
    throw std::invalid_argument("scheduler configuration and allocator must be valid");
  }
}

void ContinuousScheduler::Submit(std::uint64_t request_id, std::size_t prompt_tokens,
                                 std::size_t max_new_tokens, std::uint64_t deadline_tick) {
  if (request_id == 0 || prompt_tokens == 0 || max_new_tokens == 0) {
    throw std::invalid_argument("request id and token counts must be non-zero");
  }
  if (deadline_tick <= now_) throw std::invalid_argument("deadline must be in the future");
  if (requests_.count(request_id) != 0) throw std::invalid_argument("duplicate request id");
  Request request{request_id, prompt_tokens, max_new_tokens};
  request.arrival_tick = now_;
  request.deadline_tick = deadline_tick;
  request.last_scheduled_tick = now_;
  requests_.emplace(request_id, request);
  prefill_queue_.push_back(request_id);
}

std::vector<std::uint64_t> ContinuousScheduler::AdmitPrefillBatch() {
  ExpireDeadlines();
  RequeueBlocked();
  OrderByUrgency(&prefill_queue_);
  std::vector<std::uint64_t> admitted;
  std::size_t token_budget = config_.prefill_token_budget;
  std::size_t attempts = prefill_queue_.size();

  while (attempts-- > 0 && !prefill_queue_.empty() &&
         decode_ready_queue_.size() < config_.max_active_sequences) {
    const auto id = prefill_queue_.front();
    prefill_queue_.pop_front();
    auto& request = requests_.at(id);
    const std::size_t pages = allocator_->PagesRequired(request.prompt_tokens);
    const std::size_t final_pages =
        allocator_->PagesRequired(request.prompt_tokens + request.max_new_tokens);
    const std::size_t reserve = final_pages > pages ? config_.decode_page_reserve : 0;

    if (request.prompt_tokens > token_budget && !admitted.empty()) {
      // Skip work that does not fit instead of creating head-of-line blocking.
      prefill_queue_.push_back(id);
      continue;
    }
    if (!allocator_->CanAllocate(pages + reserve)) {
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

std::vector<std::uint64_t> ContinuousScheduler::FormDecodeBatch() {
  ExpireDeadlines();
  OrderByUrgency(&decode_ready_queue_);
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
    request.last_scheduled_tick = now_;
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

void ContinuousScheduler::AdvanceTime(std::uint64_t ticks) {
  if (ticks > std::numeric_limits<std::uint64_t>::max() - now_) {
    now_ = std::numeric_limits<std::uint64_t>::max();
  } else {
    now_ += ticks;
  }
  ExpireDeadlines();
}

const Request& ContinuousScheduler::Get(std::uint64_t request_id) const {
  return requests_.at(request_id);
}

SchedulerStats ContinuousScheduler::Stats() const {
  SchedulerStats stats{prefill_queue_.size(), blocked_queue_.size(), decode_ready_queue_.size(),
                       0, 0, 0, now_};
  for (const auto& [id, request] : requests_) {
    (void)id;
    if (request.state == RequestState::kCompleted) ++stats.completed;
    if (request.state == RequestState::kCancelled) ++stats.cancelled;
    if (request.state == RequestState::kDeadlineExceeded) ++stats.deadline_exceeded;
  }
  return stats;
}

void ContinuousScheduler::ExpireDeadlines() {
  for (auto& [id, request] : requests_) {
    const bool terminal = request.state == RequestState::kCompleted ||
                          request.state == RequestState::kCancelled ||
                          request.state == RequestState::kDeadlineExceeded;
    if (terminal || request.deadline_tick > now_) continue;
    RemoveFromQueue(&prefill_queue_, id);
    RemoveFromQueue(&blocked_queue_, id);
    RemoveFromQueue(&decode_ready_queue_, id);
    allocator_->ReleaseAll(id);
    request.state = RequestState::kDeadlineExceeded;
  }
}

void ContinuousScheduler::RequeueBlocked() {
  const std::size_t blocked_count = blocked_queue_.size();
  for (std::size_t index = 0; index < blocked_count; ++index) {
    const auto id = blocked_queue_.front();
    blocked_queue_.pop_front();
    auto& request = requests_.at(id);
    const std::size_t pages = allocator_->PagesRequired(request.prompt_tokens);
    const std::size_t final_pages =
        allocator_->PagesRequired(request.prompt_tokens + request.max_new_tokens);
    const std::size_t reserve = final_pages > pages ? config_.decode_page_reserve : 0;
    if (allocator_->CanAllocate(pages + reserve)) {
      request.state = RequestState::kQueued;
      prefill_queue_.push_back(id);
    } else {
      blocked_queue_.push_back(id);
    }
  }
}

void ContinuousScheduler::OrderByUrgency(std::deque<std::uint64_t>* queue) const {
  std::stable_sort(queue->begin(), queue->end(), [this](std::uint64_t lhs, std::uint64_t rhs) {
    const auto& left = requests_.at(lhs);
    const auto& right = requests_.at(rhs);
    if (left.deadline_tick != right.deadline_tick) {
      return left.deadline_tick < right.deadline_tick;
    }
    if (left.arrival_tick != right.arrival_tick) return left.arrival_tick < right.arrival_tick;
    return left.id < right.id;
  });
}

void ContinuousScheduler::RemoveFromQueue(std::deque<std::uint64_t>* queue,
                                          std::uint64_t request_id) {
  const auto it = std::find(queue->begin(), queue->end(), request_id);
  if (it != queue->end()) queue->erase(it);
}

}  // namespace helios::scheduler
