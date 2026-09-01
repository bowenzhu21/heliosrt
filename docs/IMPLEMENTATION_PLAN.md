# HeliosRT implementation plan

## 1. Scope and engineering rules

The first public release supports one decoder-only, Llama-compatible model family, FP16/BF16,
Linux x86-64, NVIDIA compute capability 8.0+, batch buckets 1/2/4/8/16/32, and greedy/top-k
decoding. Features outside that boundary are deferred until the vertical slice is measured.

Every optimization must pass three gates:

1. Correctness against a PyTorch reference.
2. A benchmark against the immediately preceding configuration.
3. A saved environment fingerprint and raw request-level samples.

Resume numbers are never forecasts. They are promoted only from reproducible benchmark output.

## 2. Component boundaries

| Component | Responsibility | Test without GPU |
|---|---|---|
| `kv` | Physical page ownership and page tables | Yes |
| `scheduler` | Admission, batching, cancellation, fairness | Yes |
| `runtime` | TensorRT engine/context and CUDA stream lifecycle | Mock only |
| `plugins` | GPU-resident logits transform and sampling | No |
| `graph` | Persistent buffers and CUDA Graph cache | Mock only |
| `distributed` | Rank-local execution and NCCL collectives | Protocol only |
| `controller` | Go gRPC API, placement, health, retries | Yes |
| `bench` | Workloads, metrics, fingerprints, regression tests | Yes |

## 3. Milestones and exit criteria

### M0 - CPU foundation (started)

- Repository, builds, formatting, and CPU CI.
- Paged allocator with transactional OOM and exact ownership accounting.
- Scheduler with prefill/decode separation, memory blocking, cancellation, and cleanup.
- 100,000 randomized allocation/free operations with zero invariant failures.

Exit: `make test` and `make run` pass; all completed/cancelled requests own zero pages.

### M1 - Reproducible baseline

- Pin CUDA, TensorRT, compiler, driver floor, Python, and model revision in a dev container.
- Export a small Llama-compatible model to ONNX or use TensorRT's supported network path.
- Add PyTorch eager baseline and native C++ `enqueueV3` executable.
- Use persistent contexts, pinned host buffers, async copies, CUDA events, and NVTX ranges.
- Compare logits, greedy tokens, TTFT, ITL, throughput, and peak memory.

Exit: cosine similarity above 0.999 for supported precision and 99-100% greedy-token agreement.

### M2 - GPU-resident sampling plugin

- Implement max/logits preprocessing kernel, then top-k sampling.
- Wrap the kernel in TensorRT `IPluginV3` with serialization and shape/type validation.
- Test FP16/BF16, odd vocabulary sizes, batch buckets, invalid parameters, and deterministic seeds.
- Profile bandwidth, occupancy, registers, divergence, and launch count with Nsight Compute.

Exit: parity with the CPU oracle and a measured reduction in sampling-stage latency.

### M3 - CUDA Graph decode path

- Allocate persistent metadata/input/output buffers per batch bucket.
- Warm up and capture TensorRT decode + sampling + token copy.
- Cache graph executables by engine hash, profile, GPU, precision, and batch bucket.
- Fall back to normal enqueue for shape/plugin incompatibility.

Exit: identical tokens to uncaptured execution and greater than 90% graph hit rate in the steady
benchmark, with launch overhead and ITL reported before/after.

### M4 - Device-backed paged KV cache

- Connect the proven host allocator to persistent GPU page tables.
- Allocate at admission and each token boundary; free on completion, cancellation, and failure.
- Add reference counts only when prefix sharing is introduced.
- Test OOM, cancellation during prefill/decode, worker shutdown, eviction, and fragmentation.

Exit: zero leaks, double-frees, aliasing, and out-of-bounds accesses under sanitizer/stress runs.

### M5 - Continuous batching

- Add adaptive 0-2 ms microbatch collection and configurable token/page budgets.
- Keep latency-sensitive decode separate from long prefill work.
- Report queue delay separately from GPU execution.
- Add deadline-aware scoring and later tenant deficit round robin.

Exit: higher saturated throughput at concurrency 32+ while additional batching wait remains within
the configured service-level objective.

### M6 - Two-GPU tensor parallelism

- First build a standalone sharded SwiGLU/attention block with cuBLAS and NCCL.
- Run one process per GPU and distribute the NCCL unique ID through the control channel.
- Use compute/communication streams joined by events, without device-wide synchronization.
- Integrate rank-local TensorRT subgraphs; attempt an all-reduce plugin only after the oracle works.

Exit: numerical parity, reported all-reduce bandwidth/overlap, and honest NVLink or PCIe scaling.

### M7 - Serving and operations

- Go gRPC streaming API, worker registry, heartbeats, placement, and retry policy.
- Docker image, Helm chart, readiness/liveness, Prometheus metrics, signed engine manifests.
- Jenkins GPU lanes: PR smoke, selected performance gate, nightly matrix, weekly two-GPU tests.
- Bootstrap confidence intervals; fail reproducible correctness or greater-than-5% performance regressions.

Exit: worker failure is detected, routing stops, retryable requests reschedule, and benchmark
artifacts contain raw samples plus an environment fingerprint.

## 4. Benchmark contract

Run interactive (128/128), chat (512/256), long-prompt (2048/128), variable-length fragmentation,
and cancellation workloads at concurrency 1/8/32/64 where feasible. Restart the worker, warm up,
collect at least five independent 60-120 second windows, randomize configuration order, and save:

- p50/p95/p99 TTFT, ITL, and end-to-end latency.
- Output tokens/s, requests/s, deadline failures, queue time, and batch size.
- GPU time, utilization, launch count, graph hits, memory, and KV bytes/token.
- NCCL duration, effective bandwidth, decode-time share, and overlap.
- GPU UUID/topology, driver, CUDA, TensorRT, engine/model hashes, clocks, and power state.

The ablation order is PyTorch -> stock TensorRT -> sampling plugin -> paged KV -> continuous
batching -> CUDA Graphs -> NCCL. This makes each claimed improvement attributable.

## 5. GPU-less development strategy

Do all allocator/scheduler/control-plane/test/benchmark-format work locally. Use short cloud sessions:

1. A single 24 GB+ NVIDIA GPU for TensorRT/plugin correctness and profiling.
2. The same GPU type for stable single-GPU benchmark windows.
3. One two-GPU node at the end for NCCL; record whether the link is NVLink or PCIe.

Use a persistent container image and scripted run manifest so paid sessions execute known commands
instead of debugging ordinary build issues. Shut the instance down after copying raw artifacts.

## 6. Next ten commits

1. `build: add CPU-first HeliosRT scaffold`
2. `kv: harden page allocator with property and fault tests`
3. `sched: add deadlines, prefill fairness, and trace replay`
4. `bench: define metrics schema and environment fingerprint`
5. `build: add pinned CUDA TensorRT development container`
6. `baseline: add PyTorch workload and model export`
7. `runtime: add native TensorRT engine loader and enqueue loop`
8. `test: add logit and greedy-token parity suite`
9. `cuda: add GPU greedy selection kernel and microbenchmark`
10. `plugin: integrate TensorRT IPluginV3 sampling`

## 7. Immediate decisions needed later

- GitHub repository owner/name and whether it will be public.
- Initial model revision and license-compatible weights.
- GPU cloud/provider only when M1 is ready to run.
- Target GPU architecture for the container and CI lane.

None of those decisions block M0.
