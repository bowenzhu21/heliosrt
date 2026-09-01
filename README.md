# HeliosRT

HeliosRT is a native C++/CUDA transformer inference runtime and a lightweight serving plane.
Its purpose is to make the performance-critical parts of NVIDIA GPU inference explicit and
measurable: TensorRT execution, GPU sampling, paged KV memory, continuous batching, CUDA
Graphs, NCCL tensor parallelism, and reproducible performance CI.

The repository currently contains the CPU-testable foundation:

- A transactional, cancellation-safe paged KV allocator.
- A continuous-batching scheduler model with separate prefill, blocked, and decode queues.
- Round-robin decode scheduling, memory backpressure, and lifecycle cleanup.
- A 100,000-operation randomized allocator stress test.
- Deadline-aware scheduling, decode-page headroom, and deterministic trace replay.
- A versioned benchmark schema with cross-field validation and environment fingerprints.
- CMake, Make, GitHub Actions, and an initial Jenkins pipeline.

No performance claims in this repository are assumed. Resume metrics remain placeholders until
the benchmark harness produces reproducible results on a recorded hardware/software fingerprint.

## Quick start (CPU only)

```bash
make test
make test-sanitize
make fingerprint
make run
```

Or, on a machine with CMake and Ninja:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Architecture

```text
gRPC request -> Go control plane -> C++ worker
                                      |
                         continuous batch scheduler
                                      |
                    TensorRT prefill/decode contexts
                                      |
              paged KV cache + CUDA Graph + sampling plugin
                                      |
                         CUDA/cuBLAS <-> NCCL
```

See [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) for the gated build plan,
acceptance criteria, GPU rental strategy, and commit sequence.
See [docs/BENCHMARKING.md](docs/BENCHMARKING.md) for the measurement and result contract.
