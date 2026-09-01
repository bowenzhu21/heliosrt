#include <cstdlib>
#include <iostream>
#include <random>
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

  allocator.Allocate(1, 3);
  allocator.Allocate(2, 5);
  Check(allocator.Stats().allocated_pages == 8, "allocation accounting");
  allocator.ReleaseLast(2, 2);
  allocator.ReleaseAll(1);
  Check(allocator.Stats().allocated_pages == 3, "release accounting");

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

