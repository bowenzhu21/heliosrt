#!/usr/bin/env python3
"""Validate cross-field invariants in a HeliosRT benchmark result."""

import argparse
import json
from pathlib import Path
from typing import Any


REQUIRED_TOP_LEVEL = {
    "schema_version",
    "run_id",
    "timestamp_utc",
    "measured",
    "configuration",
    "environment",
    "outcomes",
    "metrics",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def validate_percentiles(name: str, values: dict[str, Any]) -> None:
    require(set(values) == {"p50", "p95", "p99"}, f"{name} must contain p50/p95/p99")
    present = [values[key] for key in ("p50", "p95", "p99")]
    require(all(value is None or isinstance(value, (int, float)) for value in present),
            f"{name} values must be numeric or null")
    require(all(value is None or value >= 0 for value in present), f"{name} cannot be negative")
    numeric = [value for value in present if value is not None]
    require(len(numeric) in (0, 3), f"{name} percentiles must be all null or all numeric")
    if numeric:
        require(numeric[0] <= numeric[1] <= numeric[2], f"{name} must satisfy p50 <= p95 <= p99")


def validate(document: dict[str, Any]) -> None:
    require(set(document) == REQUIRED_TOP_LEVEL, "unexpected or missing top-level fields")
    require(document["schema_version"] == 1, "unsupported schema_version")
    require(isinstance(document["measured"], bool), "measured must be boolean")

    outcomes = document["outcomes"]
    expected_outcomes = {"requests", "completed", "cancelled", "deadline_exceeded", "failed"}
    require(set(outcomes) == expected_outcomes, "invalid outcome fields")
    require(all(isinstance(value, int) and value >= 0 for value in outcomes.values()),
            "outcome counts must be non-negative integers")
    terminal = outcomes["completed"] + outcomes["cancelled"] + outcomes["deadline_exceeded"] + outcomes["failed"]
    require(outcomes["requests"] == terminal, "terminal outcome counts must equal requests")

    metrics = document["metrics"]
    for name in ("ttft_ms", "itl_ms", "request_latency_ms"):
        validate_percentiles(name, metrics[name])
    if document["measured"]:
        require(metrics["output_tokens_per_second"] is not None,
                "measured runs require output_tokens_per_second")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("result", type=Path)
    args = parser.parse_args()
    with args.result.open(encoding="utf-8") as stream:
        document = json.load(stream)
    validate(document)
    print(f"PASS benchmark contract: {args.result}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
