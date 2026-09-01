#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace helios::kv {

using PageId = std::uint32_t;
using SequenceId = std::uint64_t;

struct AllocatorStats {
  std::size_t total_pages;
  std::size_t allocated_pages;
  std::size_t free_pages;
  std::size_t active_sequences;
};

class PageAllocator {
 public:
  using AllocationHook = std::function<void(std::size_t staged_page_count)>;

  PageAllocator(std::size_t total_pages, std::size_t tokens_per_page,
                AllocationHook before_commit_hook = {});

  [[nodiscard]] std::size_t PagesRequired(std::size_t tokens) const;
  [[nodiscard]] bool CanAllocate(std::size_t page_count) const noexcept;
  [[nodiscard]] std::size_t PagesOwned(SequenceId sequence_id) const noexcept;

  // Allocation is transactional: an out-of-memory failure changes no state.
  std::vector<PageId> Allocate(SequenceId sequence_id, std::size_t page_count);
  void ReleaseLast(SequenceId sequence_id, std::size_t page_count);
  void ReleaseAll(SequenceId sequence_id) noexcept;

  [[nodiscard]] const std::vector<PageId>& PageTable(SequenceId sequence_id) const;
  [[nodiscard]] AllocatorStats Stats() const noexcept;
  [[nodiscard]] bool VerifyInvariants(std::string* error = nullptr) const;

 private:
  struct PageState {
    SequenceId owner = 0;
    bool allocated = false;
  };

  std::size_t tokens_per_page_;
  AllocationHook before_commit_hook_;
  std::vector<PageState> pages_;
  std::vector<PageId> free_pages_;
  std::unordered_map<SequenceId, std::vector<PageId>> page_tables_;
};

}  // namespace helios::kv
