#pragma once

#include <cstdint>
#include <vector>

#include "helios/runtime/executor.hpp"

namespace helios::runtime {

// CPU-only deterministic backend used to validate lifecycle and batching behavior before
// TensorRT is available. It is a correctness oracle for orchestration, not a performance model.
class MockExecutor final : public Executor {
 public:
  MockExecutor(EngineDescriptor descriptor, std::vector<std::size_t> batch_buckets,
               std::uint64_t seed = 0x48454c494f535254ULL);

  [[nodiscard]] const BackendCapabilities& Capabilities() const noexcept override {
    return capabilities_;
  }
  std::vector<DecodeOutput> Decode(const std::vector<DecodeInput>& batch) override;

 private:
  EngineDescriptor descriptor_;
  BackendCapabilities capabilities_;
  std::uint64_t seed_;
};

}  // namespace helios::runtime
