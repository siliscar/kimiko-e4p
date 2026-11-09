#!/usr/bin/env python3
"""中文：按固定场景汇总阅读日志，保留失败和样本量门槛。
English: Summarize reader logs per fixed scenario, retaining failures and sample gates.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

TIMES = ("total_us", "input_us", "software_us", "action_us", "source_us", "read_us",
         "parse_us", "layout_us", "paint_us", "display_us", "join_us", "lock_us",
         "glyph_read_us", "raster_us")
MEMORY = ("internal_free", "internal_min", "internal_largest", "psram_free",
          "psram_min", "psram_largest", "ui_stack_free")
COUNTS = ("glyph_hits", "glyph_misses")


def parse_samples(text):
    rows = []
    for number, line in enumerate(text.splitlines(), 1):
        if "READER_PERF " not in line:
            continue
        parts = line.split("READER_PERF ", 1)[1].split()
        try:
            row = dict(part.split("=", 1) for part in parts)
            assert len(row) == len(parts) and row["v"] == "1" and row["op"]
            for field in ("id", "complete", "heap_local", *TIMES, *MEMORY, *COUNTS):
                row[field] = int(row[field])
                assert row[field] >= 0
            assert row["complete"] in (0, 1) and row["heap_local"] in (0, 1)
            assert row["software_us"] <= row["total_us"]
        except (ValueError, KeyError, AssertionError) as error:
            raise ValueError(f"Invalid READER_PERF record at line {number}") from error
        rows.append(row)
    if not rows:
        raise ValueError("No READER_PERF samples found")
    return rows


def percentiles(values):
    values = sorted(values)
    return {"p50": values[math.ceil(len(values) * .50) - 1],
            "p95": values[math.ceil(len(values) * .95) - 1], "max": values[-1]}


def summarize(rows, minimum):
    result = {}
    for operation in sorted({row["op"] for row in rows}):
        all_rows = [row for row in rows if row["op"] == operation]
        completed = [row for row in all_rows if row["complete"]]
        item = {"samples": len(all_rows), "complete": len(completed),
                "incomplete": len(all_rows) - len(completed),
                "enough_samples": len(completed) >= minimum, "timing_us": {}, "memory_bytes": {}}
        if completed:
            item["timing_us"] = {field: percentiles([row[field] for row in completed]) for field in TIMES}
            item["timing_us"]["dispatch_to_present_us"] = percentiles(
                [row["input_us"] + row["total_us"] for row in completed])
            for field in MEMORY:
                eligible = [row[field] for row in completed if not field.endswith("_min") or row["heap_local"]]
                if eligible:
                    item["memory_bytes"][field] = {"min": min(eligible), "max": max(eligible)}
            item["font_counters"] = {field: {"sum": sum(row[field] for row in completed)} for field in COUNTS}
        result[operation] = item
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--min-samples", type=int, default=20)
    args = parser.parse_args()
    if args.min_samples < 1:
        parser.error("--min-samples must be positive")
    manifest = json.loads(args.manifest.read_text())
    # 固定文件校验值和设备设置随结果保存。/ Keep fixture checksums and device settings alongside results.
    for field in ("book_sha256", "font_sha256", "firmware_sha256", "settings"):
        if not manifest.get(field):
            parser.error("manifest missing " + field)
    raw = args.log.read_bytes()
    result = {"schema": 1, "scenario": args.scenario, "manifest": manifest,
              "log_sha256": hashlib.sha256(raw).hexdigest(), "min_samples": args.min_samples,
              "operations": summarize(parse_samples(raw.decode("utf-8", "replace")), args.min_samples)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    for operation, item in result["operations"].items():
        print(f"{args.scenario}/{operation}: complete={item['complete']} incomplete={item['incomplete']} enough={item['enough_samples']}")
    return 0 if all(item["enough_samples"] for item in result["operations"].values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
