#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "helios/kv/page_allocator.hpp"

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
  PageAllocator allocator(256, 16);
  Check(allocator.PagesRequired(0) == 0, "zero tokens need zero pages");
  Check(allocator.PagesRequired(17) == 2, "page rounding");
  Check(allocator.Allocate(99, 0).empty(), "zero-page allocation is a no-op");
  Check(allocator.Stats().active_sequences == 0, "zero-page allocation creates no sequence");

  allocator.Allocate(1, 3);
  allocator.Allocate(2, 5);
  Check(allocator.Stats().allocated_pages == 8, "allocation accounting");
  allocator.ReleaseLast(2, 2);
  allocator.ReleaseAll(1);
  Check(allocator.Stats().allocated_pages == 3, "release accounting");
  Check(allocator.PagesOwned(2) == 3, "per-sequence accounting");

  const auto before_oom = allocator.Stats();
  bool saw_oom = false;
  try {
    allocator.Allocate(3, before_oom.free_pages + 1);
  } catch (const std::runtime_error&) {
    saw_oom = true;
  }
  Check(saw_oom, "out-of-memory is reported");
  Check(allocator.Stats().allocated_pages == before_oom.allocated_pages,
        "out-of-memory allocation is atomic");
  Check(allocator.PagesOwned(3) == 0, "failed allocation creates no page table");

  PageAllocator faulted(8, 4, [](std::size_t staged) {
    if (staged == 3) throw std::runtime_error("injected allocation failure");
  });
  bool saw_fault = false;
  try {
    faulted.Allocate(7, 5);
  } catch (const std::runtime_error&) {
    saw_fault = true;
  }
  Check(saw_fault, "allocation hook injects a pre-commit failure");
  Check(faulted.Stats().free_pages == 8, "injected failure leaks no pages");
  Check(faulted.PagesOwned(7) == 0, "injected failure creates no ownership");
  Check(faulted.VerifyInvariants(), "invariants survive injected failure");

  bool saw_invalid_release = false;
  try {
    allocator.ReleaseLast(2, 4);
  } catch (const std::invalid_argument&) {
    saw_invalid_release = true;
  }
  Check(saw_invalid_release, "invalid release is rejected");
  Check(allocator.PagesOwned(2) == 3, "invalid release changes no ownership");

  std::mt19937 generator(42);
  std::uniform_int_distribution<std::uint64_t> id_distribution(1, 128);
  std::uniform_int_distribution<int> operation_distribution(0, 2);
  std::unordered_set<std::uint64_t> active;
  for (int iteration = 0; iteration < 100000; ++iteration) {
    const auto id = id_distribution(generator);
    if (operation_distribution(generator) == 0) {
      allocator.ReleaseAll(id);
      active.erase(id);
    } else if (allocator.CanAllocate(1)) {
      allocator.Allocate(id, 1);
      active.insert(id);
    }
    std::string error;
    Check(allocator.VerifyInvariants(&error), error.c_str());
  }
  for (const auto id : active) allocator.ReleaseAll(id);
  allocator.ReleaseAll(2);
  Check(allocator.Stats().free_pages == 256, "all pages returned after stress test");
  Check(allocator.VerifyInvariants(), "final invariants");

  std::cout << "PASS page_allocator (100000 randomized operations)\n";
}
