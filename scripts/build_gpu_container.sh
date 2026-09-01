#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${HELIOS_TENSORRT_IMAGE:-}" ]]; then
  echo "HELIOS_TENSORRT_IMAGE must name a TensorRT image by immutable digest" >&2
  exit 2
fi
if [[ "${HELIOS_TENSORRT_IMAGE}" != *@sha256:* ]]; then
  echo "refusing a floating GPU image tag; use image@sha256:digest" >&2
  exit 2
fi

docker build \
  --build-arg "TENSORRT_IMAGE=${HELIOS_TENSORRT_IMAGE}" \
  --file docker/Dockerfile.gpu \
  --tag heliosrt-gpu:dev \
  .
