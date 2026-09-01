#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace helios::runtime {

using TokenId = std::int32_t;

enum class Precision { kFp32, kFp16, kBf16, kInt8 };

struct EngineDescriptor {
  std::string engine_sha256;
  std::string model_sha256;
  Precision precision = Precision::kFp16;
  std::size_t vocabulary_size = 0;
  std::size_t maximum_batch_size = 0;
  std::size_t maximum_sequence_length = 0;
};

struct BackendCapabilities {
  std::string name;
  std::vector<std::size_t> batch_buckets;
  std::size_t vocabulary_size;
  std::size_t maximum_batch_size;
  std::size_t maximum_sequence_length;
  bool supports_cuda_graphs;
};

struct DecodeInput {
  std::uint64_t sequence_id;
  TokenId last_token;
  std::size_t position;
};

struct DecodeOutput {
  std::uint64_t sequence_id;
  TokenId next_token;
};

[[nodiscard]] std::size_t SelectBatchBucket(
    std::size_t active_sequences, const std::vector<std::size_t>& sorted_buckets);
void ValidateEngineDescriptor(const EngineDescriptor& descriptor);

class Executor {
 public:
  virtual ~Executor() = default;
  [[nodiscard]] virtual const BackendCapabilities& Capabilities() const noexcept = 0;
  virtual std::vector<DecodeOutput> Decode(const std::vector<DecodeInput>& batch) = 0;
};

}  // namespace helios::runtime
