#pragma once

#include <memory>
#include <string>
#include <vector>

#include "helios/runtime/executor.hpp"

namespace helios::runtime {

// Stock TensorRT decode baseline. The initial engine contract is intentionally narrow:
// input_ids: INT32 [batch, 1], logits: FP32 [batch, vocabulary_size]. Paged KV bindings are
// introduced after numerical parity for this baseline is established.
class TensorRtExecutor final : public Executor {
 public:
  struct Config {
    std::string plan_path;
    std::string input_tensor = "input_ids";
    std::string output_tensor = "logits";
    std::vector<std::size_t> batch_buckets{1, 2, 4, 8, 16, 32};
  };

  TensorRtExecutor(EngineDescriptor descriptor, Config config);
  ~TensorRtExecutor() override;

  TensorRtExecutor(const TensorRtExecutor&) = delete;
  TensorRtExecutor& operator=(const TensorRtExecutor&) = delete;
  TensorRtExecutor(TensorRtExecutor&&) noexcept;
  TensorRtExecutor& operator=(TensorRtExecutor&&) noexcept;

  [[nodiscard]] const BackendCapabilities& Capabilities() const noexcept override;
  std::vector<DecodeOutput> Decode(const std::vector<DecodeInput>& batch) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace helios::runtime
