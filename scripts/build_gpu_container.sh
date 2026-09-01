#!/usr/bin/env bash
set -euo pipefail

PINNED_IMAGE="$(tr -d '[:space:]' < docker/tensorrt-image.txt)"
TENSORRT_IMAGE="${HELIOS_TENSORRT_IMAGE:-${PINNED_IMAGE}}"
if [[ "${TENSORRT_IMAGE}" != *@sha256:* ]]; then
  echo "refusing a floating GPU image tag; use image@sha256:digest" >&2
  exit 2
fi

docker build \
  --build-arg "TENSORRT_IMAGE=${TENSORRT_IMAGE}" \
  --file docker/Dockerfile.gpu \
  --tag heliosrt-gpu:dev \
  .
