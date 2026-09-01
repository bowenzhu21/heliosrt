#include "helios/runtime/tensorrt_executor.hpp"

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace helios::runtime {
namespace {

class Logger final : public nvinfer1::ILogger {
 public:
  void log(Severity severity, const char* message) noexcept override {
    if (severity <= Severity::kWARNING) last_message_ = message == nullptr ? "" : message;
  }
  [[nodiscard]] const std::string& LastMessage() const noexcept { return last_message_; }

 private:
  std::string last_message_;
};

template <typename T>
struct TensorRtDeleter {
  void operator()(T* object) const noexcept { delete object; }
};

template <typename T>
using TensorRtPtr = std::unique_ptr<T, TensorRtDeleter<T>>;

void CheckCuda(cudaError_t status, const char* operation) {
  if (status != cudaSuccess) {
    throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
  }
}

std::vector<char> ReadPlan(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("unable to open TensorRT plan: " + path);
  return std::vector<char>(std::istreambuf_iterator<char>(stream), {});
}

}  // namespace

class TensorRtExecutor::Impl {
 public:
  Impl(EngineDescriptor descriptor, Config config)
      : descriptor_(std::move(descriptor)), config_(std::move(config)) {
    ValidateEngineDescriptor(descriptor_);
    if (config_.batch_buckets.empty() || config_.batch_buckets.back() > descriptor_.maximum_batch_size) {
      throw std::invalid_argument("TensorRT batch buckets exceed the engine descriptor");
    }

    const auto plan = ReadPlan(config_.plan_path);
    runtime_.reset(nvinfer1::createInferRuntime(logger_));
    if (!runtime_) throw std::runtime_error("failed to create TensorRT runtime");
    engine_.reset(runtime_->deserializeCudaEngine(plan.data(), plan.size()));
    if (!engine_) {
      throw std::runtime_error("failed to deserialize TensorRT engine: " + logger_.LastMessage());
    }
    context_.reset(engine_->createExecutionContext());
    if (!context_) throw std::runtime_error("failed to create TensorRT execution context");

    ValidateTensorContract();
    CheckCuda(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking), "cudaStreamCreateWithFlags");
    CheckCuda(cudaMalloc(&device_input_, descriptor_.maximum_batch_size * sizeof(TokenId)),
              "cudaMalloc(input_ids)");
    CheckCuda(cudaMalloc(&device_logits_, descriptor_.maximum_batch_size *
                                             descriptor_.vocabulary_size * sizeof(float)),
              "cudaMalloc(logits)");
    host_input_.resize(descriptor_.maximum_batch_size);
    host_logits_.resize(descriptor_.maximum_batch_size * descriptor_.vocabulary_size);
    capabilities_ = BackendCapabilities{"tensorrt-stock", config_.batch_buckets,
                                        descriptor_.vocabulary_size,
                                        descriptor_.maximum_batch_size,
                                        descriptor_.maximum_sequence_length, false};
  }

  ~Impl() {
    if (device_logits_ != nullptr) cudaFree(device_logits_);
    if (device_input_ != nullptr) cudaFree(device_input_);
    if (stream_ != nullptr) cudaStreamDestroy(stream_);
  }

  [[nodiscard]] const BackendCapabilities& Capabilities() const noexcept { return capabilities_; }

  std::vector<DecodeOutput> Decode(const std::vector<DecodeInput>& batch) {
    const std::size_t bucket = SelectBatchBucket(batch.size(), config_.batch_buckets);
    std::unordered_set<std::uint64_t> sequence_ids;
    std::fill(host_input_.begin(), host_input_.begin() + bucket, 0);
    for (std::size_t index = 0; index < batch.size(); ++index) {
      const auto& input = batch[index];
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
      host_input_[index] = input.last_token;
    }

    const nvinfer1::Dims2 shape{static_cast<std::int32_t>(bucket), 1};
    if (!context_->setInputShape(config_.input_tensor.c_str(), shape) ||
        !context_->setTensorAddress(config_.input_tensor.c_str(), device_input_) ||
        !context_->setTensorAddress(config_.output_tensor.c_str(), device_logits_)) {
      throw std::runtime_error("failed to configure TensorRT tensor shapes or addresses");
    }
    CheckCuda(cudaMemcpyAsync(device_input_, host_input_.data(), bucket * sizeof(TokenId),
                              cudaMemcpyHostToDevice, stream_),
              "cudaMemcpyAsync(input_ids)");
    if (!context_->enqueueV3(stream_)) throw std::runtime_error("TensorRT enqueueV3 failed");
    const std::size_t logit_count = bucket * descriptor_.vocabulary_size;
    CheckCuda(cudaMemcpyAsync(host_logits_.data(), device_logits_, logit_count * sizeof(float),
                              cudaMemcpyDeviceToHost, stream_),
              "cudaMemcpyAsync(logits)");
    CheckCuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize");

    std::vector<DecodeOutput> outputs;
    outputs.reserve(batch.size());
    for (std::size_t row = 0; row < batch.size(); ++row) {
      const float* begin = host_logits_.data() + row * descriptor_.vocabulary_size;
      const float* end = begin + descriptor_.vocabulary_size;
      const auto token = static_cast<TokenId>(std::distance(begin, std::max_element(begin, end)));
      outputs.push_back(DecodeOutput{batch[row].sequence_id, token});
    }
    return outputs;
  }

 private:
  void ValidateTensorContract() const {
    if (engine_->getTensorIOMode(config_.input_tensor.c_str()) != nvinfer1::TensorIOMode::kINPUT ||
        engine_->getTensorDataType(config_.input_tensor.c_str()) != nvinfer1::DataType::kINT32) {
      throw std::invalid_argument("input_ids must be an INT32 input tensor");
    }
    if (engine_->getTensorIOMode(config_.output_tensor.c_str()) != nvinfer1::TensorIOMode::kOUTPUT ||
        engine_->getTensorDataType(config_.output_tensor.c_str()) != nvinfer1::DataType::kFLOAT) {
      throw std::invalid_argument("logits must be an FP32 output tensor");
    }
  }

  EngineDescriptor descriptor_;
  Config config_;
  Logger logger_;
  TensorRtPtr<nvinfer1::IRuntime> runtime_;
  TensorRtPtr<nvinfer1::ICudaEngine> engine_;
  TensorRtPtr<nvinfer1::IExecutionContext> context_;
  cudaStream_t stream_ = nullptr;
  void* device_input_ = nullptr;
  void* device_logits_ = nullptr;
  std::vector<TokenId> host_input_;
  std::vector<float> host_logits_;
  BackendCapabilities capabilities_;
};

TensorRtExecutor::TensorRtExecutor(EngineDescriptor descriptor, Config config)
    : impl_(std::make_unique<Impl>(std::move(descriptor), std::move(config))) {}

TensorRtExecutor::~TensorRtExecutor() = default;
TensorRtExecutor::TensorRtExecutor(TensorRtExecutor&&) noexcept = default;
TensorRtExecutor& TensorRtExecutor::operator=(TensorRtExecutor&&) noexcept = default;

const BackendCapabilities& TensorRtExecutor::Capabilities() const noexcept {
  return impl_->Capabilities();
}

std::vector<DecodeOutput> TensorRtExecutor::Decode(const std::vector<DecodeInput>& batch) {
  return impl_->Decode(batch);
}

}  // namespace helios::runtime
