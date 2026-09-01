# Benchmarking contract

Every benchmark result is a pair:

1. A versioned result document matching `bench/schema/benchmark-result.schema.json`.
2. The complete environment fingerprint produced by `scripts/environment_fingerprint.py`.

Run the local checks with:

```bash
make validate
make fingerprint
```

Only records with `measured: true` may contribute performance claims. Simulations, examples,
forecasts, and dry runs must set `measured: false` and leave unavailable performance values null.

For a comparative benchmark, both runs must have the same GPU model/count, driver, CUDA,
TensorRT, model and engine hashes, precision, token lengths, concurrency, and power/clock policy.
Each configuration uses a warm-up followed by at least five independent measurement windows.
Raw per-request samples are retained even when the report only displays percentiles.

The validator enforces terminal request accounting and percentile ordering. CI archives the
fingerprint so a regression comparison cannot silently cross incompatible environments.
