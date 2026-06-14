# HALO Optimization Guide

**Goal:** make the current HALO codebase handle large datasets more safely and more predictably, especially in the `10M` to `120M` record range.

This version is optimized for the code that actually exists today, not a generic future architecture. It focuses first on the real bottlenecks in:

- `src/Vector.h`
- `src/HashTable.h`
- `src/HashTable.cpp`
- `src/idTable.h`
- `src/idTable.cpp`
- `src/Halo.h`
- `src/Halo.cpp`
- `src/RecordStorage.h`
- `src/RecordStorage.cpp`
- `src/main.cpp`

---

## Quick Summary

If we only have time for a few changes, do these first:

1. Stop writing a full engine snapshot after every `2M` imported rows
2. Upgrade the existing `Vector::reserve(int)` path to safer `size_t` sizing, then add explicit pre-size support to `HashTable`, `IdTable`, `RecordStorage`, and index arrays
3. Replace risky `int` size/index fields on the hot path with `size_t` or explicit fixed-width types
4. Reduce duplicate string storage between `IdTable` and `HashTable`
5. Keep the current graph default off for very large imports unless it is truly needed
6. Add benchmarks before attempting heavy redesigns

These are much more relevant to this repo than jumping directly to SIMD, column storage, or double hashing.

---

## Code Audit Notes

This document was checked against the current source tree. Most of the direction is valid, but a few details need tighter wording before implementation.

### Accurate and worth doing

- Full import checkpoints really do call `Halo::saveToBinary()` on the accumulated engine, so checkpoint work grows with already-imported data.
- `HashTable::Node.key` and `IdTable::names` really do own duplicate copies of every unique ID string.
- `buildGraph` is already `false` by default, and graph edges use linear duplicate checks through `pushBackUnique()`.
- `RecordStorage::sortRecords()` uses merge sort, and the current merge creates two temporary vectors for every merge call.
- The binary format writes counts and string lengths as `int`, so widening in-memory types must be paired with a format-version decision.

### Corrected or needs care

- `Vector` already has `reserve(int)`. The real issue is that size, capacity, constructors, indexes, and `reserve()` still use `int`, and callers do not consistently pre-size large hot-path arrays.
- Do not make `HashTable` store raw pointers or references into `Vector<string> names` unless the string storage has stable addresses. The current `Vector` moves `string` objects when it resizes, so those references would become unsafe.
- The checkpoint write-amplification formula should include every full chunk that is snapshotted. For an exact `20M` records with `C = 2M`, the current loop writes snapshots for `2M, 4M, ..., 20M`.
- Widening types does not mean every ID field must become `uint64_t` immediately. For the current `120M` target, container sizes and record-index arrays are more urgent; compact internal IDs can stay `int32_t` if guarded by clear maximums.
- Graph mode is not a new feature to disable; it is already disabled. The action item is to keep it that way for huge imports and avoid enabling it implicitly from UI/load paths.
- Full multi-threaded CSV parsing is a later production-scale item, not a first-pass course-project optimization.

## Must-Do Checklist

If the target is "optimize hard but do not break the current system", these are the changes that should be done first.

### Must do now

1. Redesign import checkpoints so they do not serialize the entire accumulated engine every `2M` rows
2. Convert `Vector` size/capacity/reserve APIs from `int` to `size_t`, then add public `reserve()` / pre-size support for `HashTable`, `IdTable`, `RecordStorage`, and major index arrays
3. Replace risky `int` size/index fields on the ingest and indexing path with `size_t` or fixed-width types where the upper bound is explicit
4. Remove duplicated string ownership between `IdTable` and `HashTable`
5. Preserve the current `buildGraph = false` default for very large imports and document when graph rebuilding is allowed
6. Benchmark parse, checkpoint, sort, deduplicate, index build, and memory separately

### Do after the items above are stable

1. Reuse one merge-sort buffer, then evaluate radix sort for timestamp-heavy ordering
2. Add lazy index build or parallel index build
3. Add memory-mapped loading for `.dat` files
4. Evaluate multi-threaded CSV parsing only after the single-threaded import path is measured and stable

### Do not prioritize yet

1. Double hashing
2. SIMD
3. Column-oriented storage
4. Full multi-threaded CSV parsing for the current coursework milestone
5. Large architectural rewrites

Reason:

- the first group gives the safest win on the current codebase
- the second group gives more speed after the foundation is safer
- the last group adds complexity fast and should only come after measurement

---

## What The Current Code Does Well

- CSV format itself is flexible. IDs like `U1000000000` are still valid text.
- The parser only checks prefixes like `U`, `D`, `APP`, `R` and parses the numeric suffix, so large numeric values in the ID do not break CSV parsing.
- The generator now writes in streaming mode, so large CSV creation is mostly limited by disk throughput, not RAM.

---

## What Actually Breaks First

The main risk is **not** "an ID value becomes 1 billion".

The main risk is:

1. Too many **unique** users/devices/resources
2. Full accumulated-engine snapshots written repeatedly during chunked import
3. Too many resizes and rehashes during import
4. Too much RAM spent storing the same strings multiple times
5. `int`-based counters and indexes getting too close to their limits

In other words:

- `U1000000000` is fine as a string
- `1,000,000,000` unique users is not fine for this architecture

---

## Current Codebase Bottlenecks

### 1. Import checkpoints repeatedly serialize the full accumulated engine

Current files: `src/main.cpp`, `src/Halo.cpp`

Current behavior:

- `IMPORT_CHECKPOINT_ROWS` is `2,000,000`
- after each chunk for which `readSome()` reports non-EOF, `saveImportCheckpoint()` calls `Halo::saveToBinary()`
- `saveToBinary()` writes all ID tables and every record accumulated so far
- the snapshot is overwritten, but the write work has already been performed
- when row count is an exact multiple of `2M`, the last full chunk still reports non-EOF; EOF is discovered by the next empty read, so that final accumulated state is also snapshotted

Why this matters:

- checkpoint cost grows with the amount of data already imported
- a `20M` dataset is processed as ten `2M` chunks
- the accumulated snapshots contain `2M + 4M + ... + 20M = 110M` record writes
- at roughly `40` bytes per `DataRecords`, these snapshots write about `4.10 GiB` of raw record payload, excluding ID strings and metadata
- this is extra I/O in addition to reading the roughly `1.1 GB` CSV and performing final sort/index work

Complexity:

For `N` records and checkpoint size `C`, repeated full snapshots perform approximately:

```text
k = floor(N / C)
C + 2C + 3C + ... + kC = C * k * (k + 1) / 2
```

For exact multiples of `C`, the current loop still writes the `N`-record snapshot because EOF is only discovered on the next empty read. This is `O(N^2 / C)` snapshot work when `C` is fixed. It explains why increasing the input by `10x` can increase total load time by much more than `10x`.

Observed example in this repository:

- `2M` CSV: about `101.5 MB`, reported load time about `2.7 s`
- `20M` CSV: about `1,095 MB`, reported load time about `55 s`
- file size grows about `10.8x`, while elapsed time grows about `20.4x`

Recommended fixes, in priority order:

1. For normal uninterrupted imports, disable full-engine checkpoint snapshots.
2. If resumability is required, checkpoint less frequently and make the interval configurable.
3. Store only lightweight progress metadata when possible; an offset alone is not sufficient unless the already-imported engine can be reconstructed or persisted incrementally.
4. For robust resumability, write append-only binary chunks or partition files, then merge/finalize once at EOF.
5. Report checkpoint time and checkpoint bytes separately from CSV parse time.

Important correctness note:

Removing the `.dat` snapshot while keeping only the CSV offset would be incorrect. On resume, the program would skip earlier CSV rows without restoring their in-memory records and ID mappings.

### 2. `Vector` still uses `int` for size and capacity

Current file: `src/Vector.h`

Current risks:

- `count` and `capacity` are `int`
- `reserve()`, `setSize()`, constructors, and `operator[]` also take `int`
- growth uses `capacity * 2`
- many loops also use `int`

Why this matters:

- large imports can overflow `int`
- repeated growth causes many reallocations and copies
- anything close to billions of elements becomes unsafe

Impact:

- correctness risk for very large datasets
- significant ingest slowdown from repeated resize/copy

### 3. `HashTable` rehashes repeatedly and stores keys by value

Current files: `src/HashTable.h`, `src/HashTable.cpp`

Current risks:

- no public `reserve(expectedItems)`
- each `Node` stores `string key`
- collisions use linked-list chaining
- every rehash walks all existing nodes

Why this matters:

- with many unique IDs, import cost grows fast
- RAM usage increases because keys are stored again in the hash table
- cache locality is poor because nodes are heap-allocated one by one

### 4. `IdTable` stores the same strings twice

Current files: `src/idTable.h`, `src/idTable.cpp`

Current behavior:

- `names` stores every unique string once
- `HashTable::Node.key` stores the same string again

Why this matters:

- this doubles a major part of memory cost for large cardinality
- user/device-heavy datasets suffer the most

### 5. `Halo` indexing also relies heavily on `int`

Current files: `src/Halo.h`, `src/Halo.cpp`, `src/RecordStorage.h`, `src/RecordStorage.cpp`

Current risks:

- `userOffsets`, `deviceOffsets`, `resourceOffsets` use `Vector<int>`
- `userRecordIndices`, `deviceRecordIndices`, `resourceRecordIndices` use `Vector<int>`
- `records.size()` returns `int`
- many loops and binary search bounds use `int`

Why this matters:

- very large record counts and index arrays become fragile
- it limits how far this code can scale before overflow or undefined behavior becomes realistic

### 6. Optional graph building is expensive for dense datasets

Current files: `src/Halo.cpp`, `src/GraphNodes.h`, `src/Vector.h`

Current behavior:

- graph edges use `pushBackUnique()`
- `pushBackUnique()` calls `contains()`
- `contains()` is a linear scan

Why this matters:

- when a node connects to many neighbors, graph build cost can degrade badly
- for very large imports this can dominate runtime if graph mode is enabled

---

## Highest-Priority Optimizations

## 1. Fix Full-Snapshot Checkpointing

Priority: critical
Difficulty: medium
Risk: medium

Recommended short-term behavior:

- make checkpoint snapshots optional
- default them off for one-shot local imports
- expose the checkpoint row interval instead of hard-coding `2M`
- measure snapshot time and bytes written

Recommended long-term behavior:

- persist imported records incrementally in append-only chunks
- persist enough ID-table state to restore internal integer IDs correctly
- finalize sorting, deduplication, and index construction only after all chunks are available

Expected impact:

- removes the most obvious superlinear work from the CSV import path
- reduces SSD writes and temporary disk usage
- makes scaling from `2M` to `20M` much closer to the cost of parsing plus `O(N log N)` sorting

---

## 2. Add `reserve()` Everywhere It Matters

Priority: very high
Difficulty: low to medium
Risk: low

Add:

- convert the existing `Vector::reserve(int)` and related capacity APIs to `size_t`
- `HashTable::reserve(size_t expectedItems)`
- `IdTable::reserve(size_t expectedUnique)`
- `RecordStorage::reserve(size_t expectedRecords)`
- pre-sizing for record/index arrays when estimated sizes are known

Why this remains high priority:

- it reduces reallocations
- it reduces rehash frequency
- it is low-risk
- it helps both runtime and memory behavior

Expected impact:

- large improvement during initial CSV load
- especially important when the importer receives expected record/cardinality hints or can estimate them from generated dataset metadata

Recommended usage:

```cpp
users.reserve(expectedUsers);
devices.reserve(expectedDevices);
apps.reserve(expectedApps);
resources.reserve(expectedResources);
records.reserve(expectedRecords);
```

The current importer does not yet pass these estimates into `Halo`, so add an explicit API or import option before relying on generator-side knowledge.

After fixing full-snapshot checkpointing, this is the next safest high-impact optimization.

---

## 3. Move Hot-Path Sizes and Indexes Away From `int`

Priority: very high
Difficulty: medium
Risk: medium

Recommended direction:

- use `size_t` for container sizes and capacities
- use `uint64_t` for counters that can exceed `2^31 - 1`
- keep compact field types such as `int32_t` only where the upper bound is truly known and checked
- avoid a blind migration where every internal ID becomes `uint64_t`; it can double index memory without solving the first bottlenecks

Good candidates:

- `Vector.count`
- `Vector.capacity`
- `HashTable::Node.value` only if internal IDs are allowed to exceed the chosen compact ID limit
- record index arrays in `Halo`
- binary search bounds that currently assume `int`
- binary file count fields if the cache format is versioned again

Important note:

This is not just a performance change. It is also a scalability and correctness change, but it should be done deliberately because it affects APIs and the binary cache format.

---

## 4. Reduce Duplicate String Storage

Priority: very high
Difficulty: medium
Risk: medium

Current issue:

- `IdTable` keeps each unique string in `names`
- `HashTable` keeps another owned copy in `Node.key`

Better options:

1. Move string ownership into a stable string pool, then let the hash table store stable references or indexes.
2. Make the hash table store only `id` plus cached hash, and compare a lookup key against `names[id]` through `IdTable`.
3. Redesign `IdTable` as the owner of both canonical strings and lookup metadata so each unique string is stored once.

Avoid this unsafe shortcut:

- do not store raw pointers/references into the current `Vector<string> names` while that vector can resize and move `string` objects

Why this matters:

- with many unique users/devices, RAM usage becomes a real blocker
- removing the second copy often gives a larger win than fancy hash tricks

Expected impact:

- meaningful memory reduction
- better cache behavior

---

## 5. Keep Graph Building Disabled For Huge Loads

Priority: high
Difficulty: low
Risk: low

Recommendation:

- keep the current `buildGraph = false` default
- do not enable graph construction while importing very large datasets unless a graph feature is immediately required

Why:

- graph edges are built using linear duplicate checks
- cost rises quickly with high-degree nodes

Practical rule:

- for datasets above `10M` records, leave `buildGraph = false`
- only rebuild graph later if a specific feature depends on it

---

## 6. Benchmark Before Any Major Redesign

Priority: high
Difficulty: low
Risk: low

Track at least:

- CSV parse time
- checkpoint snapshot time
- checkpoint bytes written
- sort time
- deduplication time
- finalize time
- index build time
- peak RAM
- record count
- unique user/device/resource counts
- query latency

Add benchmark datasets like:

- `1M`
- `10M`
- `50M`
- `120M`

The goal is to know whether the bottleneck is:

- disk I/O
- CSV parsing
- repeated checkpoint serialization
- rehash/resize churn
- sort/deduplicate
- index building
- graph construction

---

## Medium-Priority Optimizations

## 7. Memory-Mapped I/O

Priority: medium to high
Difficulty: medium
Risk: low to medium

Use when:

- loading large `.dat` files
- random access is important
- the target environment is 64-bit

Do not assume it is automatically the best choice for sequential CSV ingestion. For pure streaming reads, the current buffered approach can still be competitive.

Recommended stance:

- good optimization after the hot-path container fixes above
- more valuable for binary cache loading than for first-pass CSV parsing

---

## 8. Multi-Threaded CSV Parsing

Priority: medium for production, low for the first coursework pass
Difficulty: high
Risk: medium to high

Potential upside:

- strong speedup on multi-core machines

Main blockers in the current code:

- `IdTable::getOrAdd()` is not thread-safe
- merge strategy for local ID maps is non-trivial
- determinism and debugging get harder

Recommended stance:

- do this after `reserve()`, checkpoint, and container fixes
- do not mix this with the first optimization pass
- use thread-local dictionaries first, then merge

---

## 9. Reusable Merge Buffer Or Radix Sort

Priority: medium
Difficulty: low to medium
Risk: low

This is still a good candidate because:

- sorting is on integer-like keys
- the dataset is large
- merge sort cost grows significantly with scale
- the current `merge()` creates fresh `L` and `R` vectors for every merge call
- merge sort performs approximately `N - 1` merge calls, so `20M` records can trigger almost `40M` temporary vector allocations across the two temporary arrays

Theoretical scaling:

- merge sort is `O(N log N)`
- going from `2M` to `20M` records increases merge-sort work by roughly `11.6x`, not exactly `10x`
- once the working set exceeds CPU cache, memory bandwidth and allocation overhead make the real ratio worse

Recommended stance:

- first rewrite merge sort to allocate one reusable temporary buffer for the whole sort
- then benchmark radix sort against the buffered merge sort
- preserve the full ordering used by `DataRecords::operator<=`; sorting only by timestamp is not sufficient for reliable duplicate adjacency when other fields differ

---

## 10. Lazy Or Parallel Index Building

Priority: medium
Difficulty: medium
Risk: medium

This is useful if:

- not every query type is used every time
- load latency matters more than first-query latency

But note:

- current index arrays are still `int`-based
- that foundation should be stabilized first

---

## Not Recommended Yet

## 11. Double Hashing

Priority: low for now
Difficulty: medium to high
Risk: medium

Question: will "hash 2 lan" speed things up?

Short answer: probably not enough to justify it yet.

Why:

- current `HashTable` uses separate chaining
- classic double hashing usually belongs to open addressing
- just adding a second hash without redesigning layout does not remove:
  - duplicate string storage
  - node heap allocation
  - pointer chasing
  - rehash cost

In this codebase, the better sequence is:

1. add `reserve()`
2. reduce duplicate string copies
3. fix `int` scaling risks
4. benchmark again
5. only then evaluate a new hash-table layout

If a hash-table rewrite ever happens, open addressing may be worth testing. But it should be treated as a deliberate redesign, not a quick patch.

---

## 12. Column-Oriented Storage

Priority: low to medium
Difficulty: very high
Risk: high

This can help analytical scans, but it is a major refactor and should not be the first answer to the current bottlenecks.

Recommended stance:

- only consider after the simpler memory and indexing issues are solved

---

## 13. SIMD

Priority: low to medium
Difficulty: high
Risk: medium

SIMD is attractive, but it is not the first place to look while:

- the containers still resize too much
- strings are duplicated
- large indexes still rely on `int`

Fix the algorithmic and memory-layout issues first.

---

## Dataset Design Guidance

These rules matter a lot when generating benchmark data.

### A large numeric suffix in an ID is fine

Examples:

- `U1000000000`
- `D500000000`
- `APP250000`

These do not break CSV by themselves.

### What hurts is extreme cardinality

Bad example:

- `1B` records
- almost every `user_id` is unique
- almost every `device_id` is unique

This produces:

- poor analytical realism
- huge memory pressure in `IdTable` and indexes
- weak value for anomaly detection and user-journey queries

Better benchmark shape:

- record count very large
- unique users/devices grow more slowly than total rows
- many repeated events per entity

Practical rule:

- `users << records`
- `devices <= users`
- `apps << devices`
- `resources << records`

---

## Recommended Roadmap

## Phase 1: Safe wins inside current architecture

1. Disable or redesign full-engine checkpoint snapshots during normal imports
2. Add checkpoint timing and byte counters
3. Upgrade `Vector::reserve()` to safe sizing and add `reserve()` to `HashTable`, `IdTable`, `RecordStorage`, and record/index paths
4. Widen dangerous `int` size/index usage
5. Benchmark again on `1M`, `2M`, `10M`, `20M`, and `50M`

Expected outcome:

- lower import time
- much lower checkpoint write amplification
- fewer reallocations
- fewer rehashes
- better safety for large datasets

## Phase 2: Memory reduction

1. Remove duplicate string ownership between `IdTable` and `HashTable`
2. Keep the existing graph-disabled default for huge imports
3. Measure peak RAM again

Expected outcome:

- lower memory use
- better scaling for high-cardinality datasets

## Phase 3: Faster ingest and query preparation

1. Replace per-merge temporary vectors with one reusable sort buffer
2. Evaluate radix sort while preserving full record ordering
3. Lazy or parallel index building
4. Memory-mapped `.dat` loading

Expected outcome:

- faster finalize/load path

## Phase 4: Bigger architectural changes

1. Multi-threaded CSV parsing
2. Partitioning
3. Column-oriented storage
4. Compression

Expected outcome:

- bigger long-term gains
- much higher implementation complexity

---

## Recommended Priority For This Project

If the target is a course project with limited time, the best order is:

1. remove full-snapshot checkpoint write amplification
2. safe `reserve()` / pre-size support
3. reusable merge-sort buffer
4. safer size/index types
5. reduce duplicate strings
6. benchmark radix sort
7. lazy or parallel index building

If the target is an aggressive production-scale system, then later add:

1. multi-threaded parsing
2. better on-disk format
3. partitioning
4. maybe a redesigned hash table

---

## Benchmark Template

Suggested metrics:

```cpp
struct Metrics {
    double csvParseTimeMs;
    double checkpointTimeMs;
    double sortTimeMs;
    double deduplicateTimeMs;
    double finalizeTimeMs;
    double indexBuildTimeMs;
    double queryTimeMs;

    uint64_t checkpointBytesWritten;
    uint64_t totalRecords;
    uint64_t uniqueUsers;
    uint64_t uniqueDevices;
    uint64_t uniqueResources;

    size_t peakMemoryBytes;
};
```

Run CSV benchmarks with no fresh `.dat` cache and record whether checkpointing is enabled. Otherwise a binary-cache load and a cold CSV import are not comparable.

Suggested benchmark matrix:

| Dataset | Records | Unique users | Unique devices | Notes |
|---------|---------|--------------|----------------|-------|
| Small | 1M | moderate | moderate | sanity check |
| Medium | 10M | moderate | moderate | daily dev benchmark |
| Large | 50M | realistic | realistic | stress ingest |
| XL | 120M | realistic | realistic | practical upper target |

Also include one pathological case:

| Dataset | Records | Unique users | Unique devices | Notes |
|---------|---------|--------------|----------------|-------|
| Worst-case | smaller sample | near-record-count | near-record-count | stress memory/index behavior |

---

## Common Mistakes

1. Optimizing for giant numeric ID values instead of unique-key count
2. Measuring only total load time and missing repeated checkpoint serialization
3. Keeping a fixed checkpoint interval while every checkpoint rewrites all accumulated records
4. Removing snapshot data but retaining only the CSV offset, which breaks resume correctness
5. Jumping into SIMD before fixing resize/rehash churn
6. Rewriting the hash table before adding `reserve()`
7. Enabling graph mode during every large import
8. Benchmarking only one dataset shape
9. Confusing "large file" with "large cardinality"

---

## Final Recommendation

For this repo, the fastest path to meaningful improvement is:

1. remove repeated full-engine checkpoint snapshots from the default import path
2. measure parse, checkpoint, sort, deduplicate, and index phases separately
3. stabilize container growth
4. reuse one merge-sort buffer
5. stabilize index sizes
6. reduce duplicated string memory
7. measure again

Only after that should we decide whether a heavier redesign like multi-thread parsing, open addressing, partitioning, or column storage is worth the cost.

---

**Version:** 2.2
**Last updated:** June 2026
**Focus:** current HALO implementation, large CSV workloads, realistic optimization order
