# Engine artifact contract

A worker never loads an unnamed or mutable model/engine pair. `scripts/artifact_manifest.py`
records SHA-256 hashes, the immutable upstream model revision, precision, and optimization limits.

```bash
python3 scripts/artifact_manifest.py create \
  --model artifacts/model.safetensors \
  --engine artifacts/model.plan \
  --model-id organization/model \
  --model-revision IMMUTABLE_COMMIT \
  --precision fp16 \
  --maximum-batch-size 32 \
  --maximum-sequence-length 4096 \
  --output artifacts/manifest.json

python3 scripts/artifact_manifest.py verify artifacts/manifest.json
```

The GPU container follows the same rule. `docker/tensorrt-image.txt` pins the exact TensorRT 10.3
image digest resolved by compile CI. `scripts/build_gpu_container.sh` uses that digest by default
and rejects any floating override passed through `HELIOS_TENSORRT_IMAGE`.
