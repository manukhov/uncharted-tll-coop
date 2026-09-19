"""Bounded offline query trace ABI and pure report checks; never reads memory."""
from __future__ import annotations

import ctypes
import statistics
from collections import defaultdict

CAPACITY = 128


class QuerySample(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in
                ("address", "region_base", "region_bytes", "ticks")] + [
        (name, ctypes.c_uint32) for name in ("last_stage", "flags")]


class QueryTrace(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ("size", "version", "capacity", "count", "overflow", "reserved")] + [
        ("entries", QuerySample * CAPACITY)]


def new_trace(capacity=CAPACITY):
    return QueryTrace(size=ctypes.sizeof(QueryTrace), version=1, capacity=capacity)


def fixture_ranges(image, fixture):
    """Classify queried STORAGE, not the caller's semantic role or live lifetime."""
    from p99_preparation_fixture import (BASE, ENTRIES, RETAINED, BUCKETS, SENTINEL,
        BUCKET_COUNT, INITIALIZED, LOCK, SKEL_EPOCH, ANIM_EPOCH, ENTRY_COUNT)
    ranges = [(image.address + ENTRIES - BASE, fixture.table_count * 64,
               "fixed_table", "retarget_table"),
              (image.address + RETAINED - BASE, 16, "retained_slot", "retained_slot")]
    for name, va, size in (("bucket_pointer", BUCKETS, 8), ("sentinel_pointer", SENTINEL, 8),
            ("bucket_count", BUCKET_COUNT, 4), ("initialized", INITIALIZED, 1),
            ("lock", LOCK, 8), ("skel_epoch", SKEL_EPOCH, 4),
            ("anim_epoch", ANIM_EPOCH, 4), ("entry_count", ENTRY_COUNT, 4)):
        ranges.append((image.address + va - BASE, size, "image_registry", name))
    owner = {"player", "fg", "controller", "grenade", "weapon", "parent",
             "retained", "weapon_handle", "job", "local_pose"}
    for name, buffer in fixture.buffers.items():
        category = ("registry_buckets" if name == "registry_buckets" else
                    "registry_nodes" if name.startswith("registry_") else
                    "borrowed_owner" if name in owner else "borrowed_source")
        ranges.append((buffer.address, buffer.size, category, name))
    return ranges


def classify(address, ranges):
    matches = [(category, name, address - base) for base, size, category, name in ranges
               if base <= address < base + size]
    if len(matches) != 1:
        raise ValueError(f"query address has {len(matches)} owned classifications: {address:#x}")
    category, name, offset = matches[0]
    return {"category": category, "storage": name, "offset": offset}


def validate_trace(trace, stats, ranges):
    if (trace.size != ctypes.sizeof(QueryTrace) or trace.version != 1 or trace.reserved or
            not 1 <= trace.capacity <= CAPACITY or trace.overflow or
            trace.count != stats.virtual_queries or not 0 < trace.count <= trace.capacity):
        raise ValueError("query trace ABI/count/capacity/overflow differs")
    entries = []
    for index in range(trace.count):
        value = trace.entries[index]
        if (value.flags != 7 or not 1 <= value.last_stage <= 8 or not value.region_bytes or
                value.region_base + value.region_bytes > 0xFFFFFFFFFFFFFFFF or
                not value.region_base <= value.address < value.region_base + value.region_bytes or
                value.ticks > stats.total_ticks):
            raise ValueError(f"invalid successful query trace entry {index}")
        entries.append({"ordinal": index, **classify(value.address, ranges),
            "address": value.address, "region_base": value.region_base,
            "region_bytes": value.region_bytes, "ticks": value.ticks,
            "last_stage": value.last_stage, "flags": value.flags})
    if sum(item["ticks"] for item in entries) != stats.virtual_query_ticks:
        raise ValueError("trace query times do not exactly reconcile with the adapter total")
    if any(any(bytes(trace.entries[index])) for index in range(trace.count, CAPACITY)):
        raise ValueError("trace wrote beyond its declared count")
    return entries


def summarize(traces, frequency, distribution):
    if len(traces) < 2 or frequency <= 0:
        raise ValueError("trace requires first plus warm samples and valid frequency")
    topology = [{key: value for key, value in item.items() if key != "ticks"}
                for item in traces[0]]
    for trace in traces[1:]:
        if [{key: value for key, value in item.items() if key != "ticks"}
                for item in trace] != topology:
            raise ValueError("query topology changed across identical trace samples")
    per_query = []
    for ordinal, identity in enumerate(topology):
        per_query.append(identity | {"warm_time": distribution([
            trace[ordinal]["ticks"] * 1000 / frequency for trace in traces[1:]])})
    groups = defaultdict(list)
    for ordinal, identity in enumerate(topology):
        groups[identity["category"]].append(ordinal)
    categories = {}
    for category, ordinals in sorted(groups.items()):
        categories[category] = {"queries_per_preparation": len(ordinals),
            "region_sizes": sorted({topology[index]["region_bytes"] for index in ordinals}),
            "warm_total": distribution([sum(trace[index]["ticks"] for index in ordinals) *
                1000 / frequency for trace in traces[1:]])}
    image_share = []
    for trace in traces[1:]:
        total = sum(item["ticks"] for item in trace)
        if total <= 0:
            raise ValueError("query time must be positive for cost shares")
        image_ticks = sum(item["ticks"] for item in trace if item["category"] in
                          ("fixed_table", "retained_slot", "image_registry"))
        image_share.append(100 * image_ticks / total)
    return {"capacity": CAPACITY, "count": len(topology), "overflow": 0,
        "timing_reconciles_exactly": True,
        "stage_note": "last_stage is the existing diagnostic marker, NOT a restored call-stack phase",
        "classification_note": "fixture storage identity, not caller role or a live lifetime certificate",
        "image_query_share_percent": {"min": min(image_share), "median": statistics.median(image_share),
                                      "max": max(image_share)},
        "categories": categories, "per_query": per_query}
