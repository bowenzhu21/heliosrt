#!/usr/bin/env python3
"""Create and verify immutable model/engine manifests for HeliosRT workers."""

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def create_manifest(args: argparse.Namespace) -> dict[str, Any]:
    if not args.model_revision or args.model_revision.lower() in {"main", "latest"}:
        raise ValueError("model_revision must be an immutable revision, not main/latest")
    return {
        "artifact_manifest_version": 1,
        "model": {
            "id": args.model_id,
            "revision": args.model_revision,
            "path": args.model.name,
            "sha256": sha256(args.model),
        },
        "engine": {"path": args.engine.name, "sha256": sha256(args.engine)},
        "runtime": {
            "precision": args.precision,
            "maximum_batch_size": args.maximum_batch_size,
            "maximum_sequence_length": args.maximum_sequence_length,
        },
    }


def verify_manifest(path: Path) -> None:
    with path.open(encoding="utf-8") as stream:
        manifest = json.load(stream)
    if manifest.get("artifact_manifest_version") != 1:
        raise ValueError("unsupported artifact_manifest_version")
    for artifact_name in ("model", "engine"):
        artifact = manifest[artifact_name]
        artifact_path = path.parent / artifact["path"]
        actual = sha256(artifact_path)
        if actual != artifact["sha256"]:
            raise ValueError(f"{artifact_name} SHA-256 mismatch: expected {artifact['sha256']}, got {actual}")
    runtime = manifest["runtime"]
    if runtime["maximum_batch_size"] <= 0 or runtime["maximum_sequence_length"] <= 0:
        raise ValueError("runtime dimensions must be positive")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create")
    create.add_argument("--model", type=Path, required=True)
    create.add_argument("--engine", type=Path, required=True)
    create.add_argument("--model-id", required=True)
    create.add_argument("--model-revision", required=True)
    create.add_argument("--precision", choices=("fp16", "bf16", "int8"), required=True)
    create.add_argument("--maximum-batch-size", type=int, required=True)
    create.add_argument("--maximum-sequence-length", type=int, required=True)
    create.add_argument("--output", type=Path, required=True)
    verify = subparsers.add_parser("verify")
    verify.add_argument("manifest", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "create":
        manifest = create_manifest(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"created manifest: {args.output}")
    else:
        verify_manifest(args.manifest)
        print(f"PASS artifact manifest: {args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
