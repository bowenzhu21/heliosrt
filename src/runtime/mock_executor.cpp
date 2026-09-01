#include "helios/runtime/mock_executor.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace helios::runtime {
namespace {

bool IsSha256(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

std::uint64_t Mix(std::uint64_t value) {
  value ^= value >> 30U;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27U;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

}  // namespace

std::size_t SelectBatchBucket(std::size_t active_sequences,
                              const std::vector<std::size_t>& sorted_buckets) {
  if (active_sequences == 0) throw std::invalid_argument("decode batch cannot be empty");
  const auto bucket = std::lower_bound(sorted_buckets.begin(), sorted_buckets.end(), active_sequences);
  if (bucket == sorted_buckets.end()) throw std::out_of_range("no batch bucket can hold request");
  return *bucket;
}

void ValidateEngineDescriptor(const EngineDescriptor& descriptor) {
  if (!IsSha256(descriptor.engine_sha256) || !IsSha256(descriptor.model_sha256)) {
    throw std::invalid_argument("engine and model hashes must be lowercase or uppercase SHA-256");
  }
  if (descriptor.vocabulary_size == 0 ||
      descriptor.vocabulary_size > static_cast<std::size_t>(std::numeric_limits<TokenId>::max()) ||
      descriptor.maximum_batch_size == 0 || descriptor.maximum_sequence_length == 0) {
    throw std::invalid_argument("engine dimensions must be non-zero and representable");
  }
}

MockExecutor::MockExecutor(EngineDescriptor descriptor, std::vector<std::size_t> batch_buckets,
                           std::uint64_t seed)
    : descriptor_(std::move(descriptor)), seed_(seed) {
  ValidateEngineDescriptor(descriptor_);
  if (batch_buckets.empty() ||
      !std::is_sorted(batch_buckets.begin(), batch_buckets.end()) ||
      std::adjacent_find(batch_buckets.begin(), batch_buckets.end()) != batch_buckets.end() ||
      batch_buckets.front() == 0 || batch_buckets.back() > descriptor_.maximum_batch_size) {
    throw std::invalid_argument("batch buckets must be unique, sorted, non-zero, and in bounds");
  }
  capabilities_ = BackendCapabilities{"mock", std::move(batch_buckets),
                                      descriptor_.vocabulary_size,
                                      descriptor_.maximum_batch_size,
                                      descriptor_.maximum_sequence_length, false};
}

std::vector<DecodeOutput> MockExecutor::Decode(const std::vector<DecodeInput>& batch) {
  (void)SelectBatchBucket(batch.size(), capabilities_.batch_buckets);
  std::unordered_set<std::uint64_t> sequence_ids;
  std::vector<DecodeOutput> outputs;
  outputs.reserve(batch.size());
  for (const auto& input : batch) {
    if (input.sequence_id == 0 || !sequence_ids.insert(input.sequence_id).second) {
      throw std::invalid_argument("decode sequence IDs must be unique and non-zero");
    }
    if (input.last_token < 0 ||
        static_cast<std::size_t>(input.last_token) >= descriptor_.vocabulary_size) {
      throw std::out_of_range("input token is outside the vocabulary");
    }
    if (input.position >= descriptor_.maximum_sequence_length) {
      throw std::out_of_range("decode position exceeds engine profile");
    }
    std::uint64_t value = seed_ ^ Mix(input.sequence_id) ^ Mix(input.position + 1) ^
                          Mix(static_cast<std::uint64_t>(input.last_token) + 1);
    const auto token = static_cast<TokenId>(Mix(value) % descriptor_.vocabulary_size);
    outputs.push_back(DecodeOutput{input.sequence_id, token});
  }
  return outputs;
}

}  // namespace helios::runtime
