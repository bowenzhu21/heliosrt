#include "helios/kv/page_allocator.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace helios::kv {

PageAllocator::PageAllocator(std::size_t total_pages, std::size_t tokens_per_page,
                             AllocationHook before_commit_hook)
    : tokens_per_page_(tokens_per_page),
      before_commit_hook_(std::move(before_commit_hook)),
      pages_(total_pages) {
  if (total_pages == 0 || tokens_per_page == 0) {
    throw std::invalid_argument("total_pages and tokens_per_page must be non-zero");
  }
  free_pages_.reserve(total_pages);
  for (std::size_t index = total_pages; index > 0; --index) {
    free_pages_.push_back(static_cast<PageId>(index - 1));
  }
}

std::size_t PageAllocator::PagesRequired(std::size_t tokens) const {
  return tokens == 0 ? 0 : (tokens + tokens_per_page_ - 1) / tokens_per_page_;
}

bool PageAllocator::CanAllocate(std::size_t page_count) const noexcept {
  return page_count <= free_pages_.size();
}

std::size_t PageAllocator::PagesOwned(SequenceId sequence_id) const noexcept {
  const auto it = page_tables_.find(sequence_id);
  return it == page_tables_.end() ? 0 : it->second.size();
}

std::vector<PageId> PageAllocator::Allocate(SequenceId sequence_id, std::size_t page_count) {
  if (sequence_id == 0) {
    throw std::invalid_argument("sequence_id 0 is reserved");
  }
  if (!CanAllocate(page_count)) {
    throw std::runtime_error("KV page allocator out of memory");
  }
  if (page_count == 0) return {};

  // Stage page IDs and invoke the optional test hook before changing allocator state.
  // Any exception before the commit loop therefore leaves all invariants untouched.
  std::vector<PageId> staged;
  staged.reserve(page_count);
  for (std::size_t i = 0; i < page_count; ++i) {
    const PageId page = free_pages_[free_pages_.size() - i - 1];
    staged.push_back(page);
    if (before_commit_hook_) before_commit_hook_(i + 1);
  }

  auto existing = page_tables_.find(sequence_id);
  if (existing == page_tables_.end()) {
    std::vector<PageId> new_table;
    new_table.reserve(page_count);
    existing = page_tables_.emplace(sequence_id, std::move(new_table)).first;
  } else {
    existing->second.reserve(existing->second.size() + page_count);
  }

  auto& table = existing->second;
  for (const PageId page : staged) {
    free_pages_.pop_back();
    pages_.at(page) = PageState{sequence_id, true};
    table.push_back(page);
  }
  return staged;
}

void PageAllocator::ReleaseLast(SequenceId sequence_id, std::size_t page_count) {
  auto it = page_tables_.find(sequence_id);
  if (it == page_tables_.end() || page_count > it->second.size()) {
    throw std::invalid_argument("cannot release pages not owned by sequence");
  }
  auto& table = it->second;
  for (std::size_t i = 0; i < page_count; ++i) {
    const PageId page = table.back();
    table.pop_back();
    pages_.at(page) = PageState{};
    free_pages_.push_back(page);
  }
  if (table.empty()) {
    page_tables_.erase(it);
  }
}

void PageAllocator::ReleaseAll(SequenceId sequence_id) noexcept {
  auto it = page_tables_.find(sequence_id);
  if (it == page_tables_.end()) {
    return;
  }
  for (const PageId page : it->second) {
    pages_[page] = PageState{};
    free_pages_.push_back(page);
  }
  page_tables_.erase(it);
}

const std::vector<PageId>& PageAllocator::PageTable(SequenceId sequence_id) const {
  const auto it = page_tables_.find(sequence_id);
  if (it == page_tables_.end()) {
    throw std::out_of_range("sequence has no page table");
  }
  return it->second;
}

AllocatorStats PageAllocator::Stats() const noexcept {
  return AllocatorStats{pages_.size(), pages_.size() - free_pages_.size(), free_pages_.size(),
                        page_tables_.size()};
}

bool PageAllocator::VerifyInvariants(std::string* error) const {
  auto fail = [error](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };

  if (free_pages_.size() > pages_.size()) return fail("free page count exceeds total pages");
  std::unordered_set<PageId> seen_free;
  for (const PageId page : free_pages_) {
    if (page >= pages_.size()) return fail("free page index out of bounds");
    if (!seen_free.insert(page).second) return fail("page occurs twice in free list");
    if (pages_[page].allocated) return fail("allocated page occurs in free list");
    if (pages_[page].owner != 0) return fail("free page retains a non-zero owner");
  }

  std::unordered_set<PageId> seen_allocated;
  for (const auto& [sequence_id, table] : page_tables_) {
    if (table.empty()) return fail("empty page table retained");
    for (const PageId page : table) {
      if (page >= pages_.size()) return fail("page table index out of bounds");
      if (!seen_allocated.insert(page).second) return fail("page belongs to multiple sequences");
      if (!pages_[page].allocated || pages_[page].owner != sequence_id) {
        return fail("page metadata disagrees with page table");
      }
    }
  }

  if (seen_allocated.size() + seen_free.size() != pages_.size()) {
    return fail("allocated_pages + free_pages != total_pages");
  }
  return true;
}

}  // namespace helios::kv
