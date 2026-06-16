# HALO Optimization Guide

**Goal:** make the current HALO codebase handle large datasets more safely and more predictably, especially in the `10M` to `500M` record range.

Current implementation status:

- done: opt-in checkpoints, adaptive large-import checkpoint spacing, buffered `.dat` writing, atomic cache-file replacement, `size_t` reserves, duplicate-string removal, packed 32-byte `DataRecords`, memcpy fast path for trivially copyable `Vector` elements, open-addressed `HashTable`, radix-hybrid sort, direct numeric ID fast path, parallel index build, memory-mapped `.dat` loading, external/partitioned sort fallback, heuristic reserve hints from CSV estimates, `1M+` record pre-sizing, opt-in batched multi-thread CSV parsing
- still open: no required coursework optimization items; future-only storage/layout redesign may be revisited if the data shape changes
- practical note: `500M+` records needs a different storage/sort architecture than this coursework pass

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

If we only have time for a few changes, these are the ones that matter most and are now implemented in code:

1. Stop writing a full engine snapshot after every `2M` imported rows; make checkpoints opt-in and less frequent
2. Upgrade size/capacity/reserve paths to `size_t`, then add explicit pre-size support to `HashTable`, `IdTable`, `RecordStorage`, and index arrays
3. Replace risky `int` size/index fields on the hot path with `size_t` or explicit fixed-width types, while keeping compact internal IDs guarded
4. Remove duplicate string storage between `IdTable` and `HashTable`
5. Keep the graph default off for very large imports unless it is truly needed
6. Benchmark parse, checkpoint, sort, deduplicate, index build, and memory separately

The current default sort path for large imports is now radix-hybrid, with merge still available for comparison and fallback.

These are much more relevant to this repo than jumping directly to SIMD or column storage.

---

## Code Audit Notes

This document was checked against the current source tree. Most of the direction is valid, but a few details need tighter wording before implementation.

### Accurate and worth doing

- Full import checkpoints still call `Halo::saveToBinary()` on the accumulated engine when resume is enabled, but now the schedule is geometric instead of fixed every `2M` rows. For `100M+` estimated rows, the default spacing is raised to at least `32M` rows.
- `HashTable` no longer uses heap-allocated linked nodes. `IdTable` owns the canonical strings, while the hash table keeps cached hashes and internal IDs in open-addressed slots, plus a direct numeric fast path for the CSV ID shapes used here.
- `DataRecords` is packed to 32 bytes, which reduces record RAM and binary-cache size for very large datasets.
- `buildGraph` is already `false` by default, and graph edges still use linear duplicate checks through `pushBackUnique()`.
- `RecordStorage::sortRecords()` now supports merge, intro, and radix-hybrid. Merge uses one reusable buffer, and radix-hybrid is the current best default on large imports.
- `.dat` writing uses a buffered binary writer and writes cache output through a temporary `.writing` file before replacing the visible cache.
- The binary format still writes name counts and string lengths as `int`, so widening those fields further still has to be paired with another format-version decision.

### Corrected or needs care

- `Vector` already uses `size_t` for size and capacity and now uses `memcpy` for trivially copyable element types.
- Do not make `HashTable` store raw pointers or references into `Vector<string> names` unless the string storage has stable addresses. The current `Vector` moves `string` objects when it resizes, so those references would become unsafe.
- The remaining checkpoint cost depends on the geometric snapshot schedule, but it is still full-engine write work and should be counted separately from CSV parsing.
- Widening types does not mean every ID field must become `uint64_t` immediately. For the current `500M` target, container sizes and record-index arrays are more urgent; compact internal IDs can stay `int32_t` if guarded by clear maximums.
- Graph mode is not a new feature to disable; it is already disabled. The action item is to keep it that way for huge imports and avoid enabling it implicitly from UI/load paths.
- Batched multi-threaded CSV parsing now exists as an opt-in path (`parallelCsv=1`) and an auto path for very large files, but it is intentionally not the default for mid-size datasets because line-copy and thread overhead can dominate.

## Must-Do Checklist

If the target is "optimize hard but do not break the current system", these are the changes that should be done first.

### Must do now

1. Keep checkpointing opt-in and measured; do not reintroduce full snapshots on the normal import path
2. Keep `Vector`/reserve APIs on `size_t`, and preserve pre-size hooks for `HashTable`, `IdTable`, `RecordStorage`, and index arrays
3. Keep hot-path `int` usage only where the upper bound is explicit and guarded
4. Keep canonical string ownership in `IdTable`, not duplicated in `HashTable`
5. Preserve the current `buildGraph = false` default for very large imports and document when graph rebuilding is allowed
6. Benchmark parse, checkpoint, sort, deduplicate, index build, and memory separately

### Do after the items above are stable

1. Reuse one merge-sort buffer, then evaluate radix sort for timestamp-heavy ordering
2. Add lazy index build or parallel index build
3. Keep multi-threaded CSV parsing opt-in or very-large-file-only unless benchmarks show it wins on the current machine and dataset shape

### Do not prioritize yet

1. SIMD
2. Column-oriented storage
3. Making multi-threaded CSV parsing the default for all dataset sizes
4. Large architectural rewrites

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
6. On very large files, the importer becomes memory-bandwidth bound, so throughput drops once the working set outgrows CPU cache

In other words:

- `U1000000000` is fine as a string
- `1,000,000,000` unique users is not fine for this architecture

---

## Current Codebase Bottlenecks

### 1. Import checkpoints still serialize the full accumulated engine when enabled

Current files: `src/main.cpp`, `src/Halo.cpp`

Current behavior:

- checkpointing is opt-in from the load API/UI
- the importer uses a geometric schedule instead of blindly snapshotting every fixed chunk
- `saveImportCheckpoint()` still calls `Halo::saveToBinary()` on the accumulated engine
- the snapshot is overwritten, but the write work has already been performed
- when resume support is enabled, each checkpoint is still a full engine snapshot, so it remains expensive at very large scale

Why this matters:

- checkpoint cost still grows with the amount of data already imported
- geometric scheduling lowers the number of snapshots, but each one is still a full copy of the engine state
- the remaining cost is still extra I/O in addition to reading the CSV and performing final sort/index work

Complexity:

Geometric full snapshots reduce the number of writes, but they are still bounded by full-engine state size rather than just row count. That is why this is still a checkpoint cost, not a cheap progress marker.

Recommended fixes, in priority order:

1. For normal uninterrupted imports, keep full-engine checkpoint snapshots disabled.
2. If resumability is required, keep the interval configurable and conservative.
3. Store only lightweight progress metadata when possible; an offset alone is not sufficient unless the already-imported engine can be reconstructed or persisted incrementally.
4. For robust resumability at much larger scale, write append-only binary chunks or partition files, then merge/finalize once at EOF.
5. Report checkpoint time and checkpoint bytes separately from CSV parse time.

Important correctness note:

Removing the `.dat` snapshot while keeping only the CSV offset would be incorrect. On resume, the program would skip earlier CSV rows without restoring their in-memory records and ID mappings.

### 2. `Vector` now uses `size_t` for size and capacity

Current file: `src/Vector.h`

Current state:

- size/capacity paths now use `size_t`
- `reserve()` and `setSize()` still keep `int` overloads for compatibility
- many loops have already been widened where they sit on the hot path

Why this matters:

- large imports are less likely to overflow `int`
- repeated growth still matters, but the new reserve paths reduce churn
- anything close to billions of elements still needs explicit guards elsewhere

Impact:

- correctness risk is reduced
- ingest slowdown from repeated resize/copy is much lower than before

### 3. `HashTable` now uses open addressing instead of linked nodes

Current files: `src/HashTable.h`, `src/HashTable.cpp`

Current state:

- public `reserve(expectedItems)` exists
- each slot stores a cached hash and internal ID
- collisions are resolved with double-hashing probes inside one contiguous slot array
- rehashing moves compact slots instead of relinking heap-allocated nodes
- the load factor is kept around 70% to balance memory use and lookup speed

Why this matters:

- with many unique IDs, import cost still matters
- RAM usage is lower now that keys are not stored twice
- cache locality is better because lookups probe contiguous table storage
- the behavior is more balanced than relying on direct numeric IDs only

### 4. `IdTable` now owns the canonical strings once

Current files: `src/idTable.h`, `src/idTable.cpp`

Current behavior:

- `names` stores every unique string once
- the hash table references `names[id]` instead of storing another string copy

Why this matters:

- this removes a major duplicate-memory cost for large cardinality
- user/device-heavy datasets benefit the most

### 5. `Halo` indexing still uses `int` in a few internal arrays

Current files: `src/Halo.h`, `src/Halo.cpp`, `src/RecordStorage.h`, `src/RecordStorage.cpp`

Current state:

- `userOffsets`, `deviceOffsets`, `resourceOffsets` still use `Vector<int>`
- `userRecordIndices`, `deviceRecordIndices`, `resourceRecordIndices` still use `Vector<int>`
- `records.size()` now returns `size_t`
- the hot path now guards overflow before new internal IDs are created

Why this matters:

- very large record counts and index arrays are still the next place to watch
- the current guards make the `int` choice intentional instead of accidental

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

## 1. Fix Full-Snapshot Checkpointing (done in current branch)

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

## 2. Add `reserve()` Everywhere It Matters (done in current branch)

Priority: very high
Difficulty: low to medium
Risk: low

Add:

- convert the existing `Vector::reserve(int)` and related capacity APIs to `size_t`
- `HashTable::reserve(size_t expectedItems)`
- `IdTable::reserve(size_t expectedUnique)` and reserve the canonical-name array when an estimate exists
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

The current importer passes estimated record counts into `Halo` for `1M+` row datasets, and the ID tables now reserve by those estimates while only shrinking when the slack is meaningfully large.

After fixing full-snapshot checkpointing, this is the next safest high-impact optimization.

---

## 3. Move Hot-Path Sizes and Indexes Away From `int` (partially done)

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
- `HashTable::Slot.value` only if internal IDs are allowed to exceed the chosen compact ID limit
- record index arrays in `Halo`
- binary search bounds that currently assume `int`
- binary file count fields if the cache format is versioned again

Important note:

This is not just a performance change. It is also a scalability and correctness change, but it should be done deliberately because it affects APIs and the binary cache format.

---

## 4. Reduce Duplicate String Storage (done in current branch)

Priority: very high
Difficulty: medium
Risk: medium

Current issue:

- `IdTable` keeps each unique string in `names`
- `HashTable` now keeps only cached hashes and internal IDs, so the same string is not owned twice anymore

Better options:

1. Keep `IdTable` as the canonical string owner and let lookup metadata stay separate.
2. If the data shape changes, move to a stable string pool or index-based lookup without raw pointers into `Vector<string>`.
3. Keep the direct numeric fast path for CSV IDs that already follow the expected prefix-plus-number pattern.

Avoid this unsafe shortcut:

- do not store raw pointers/references into the current `Vector<string> names` while that vector can resize and move `string` objects

Why this matters:

- with many unique users/devices, RAM usage becomes a real blocker
- removing the second copy often gives a larger win than fancy hash tricks

Expected impact:

- meaningful memory reduction
- better cache behavior

---

## 5. Keep Graph Building Disabled For Huge Loads (still correct)

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

## 6. Benchmark Before Any Major Redesign (ongoing)

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

## 9. Reusable Merge Buffer Or Radix Sort (done in current branch)

Priority: medium
Difficulty: low to medium
Risk: low

This is still a good candidate because:

- sorting is on integer-like keys
- the dataset is large
- merge sort cost grows significantly with scale
- merge sort now uses one reusable buffer, which removes the old per-merge `L`/`R` allocation pattern
- merge sort still performs `O(N log N)` work, so it loses to radix-hybrid on large timestamp-heavy datasets

Theoretical scaling:

- merge sort is `O(N log N)`
- going from `2M` to `20M` records increases merge-sort work by roughly `11.6x`, not exactly `10x`
- once the working set exceeds CPU cache, memory bandwidth and allocation overhead make the real ratio worse

Recommended stance:

- keep the reusable-buffer merge path as the comparison baseline
- benchmark radix-hybrid against the buffered merge sort on the real dataset sizes
- preserve the full ordering used by `DataRecords::operator<=`; sorting only by timestamp is not sufficient for reliable duplicate adjacency when other fields differ

---

## 10. Lazy Or Parallel Index Building (parallel variant done)

Priority: medium
Difficulty: medium
Risk: medium

This is useful if:

- not every query type is used every time
- load latency matters more than first-query latency

But note:

- the current code now uses `size_t` for container sizes, but some internal index arrays still use `int`
- keep those bounds explicit when extending the query layer

---

## Not Recommended Yet

## 11. Further Hash-Table Tuning

Priority: low for now
Difficulty: medium to high
Risk: medium

Question: should we keep tuning the hash table further?

Short answer: not before profiling a dataset where non-numeric IDs dominate.

Why:

- the current `HashTable` already uses open addressing with double-hashing probes
- the standard generated HALO IDs mostly use the direct numeric fast path, so the hash table is not always the main bottleneck
- further tuning can trade memory for speed quickly, especially if the load factor is lowered too much

In this codebase, the better sequence is:

1. add `reserve()`
2. reduce duplicate string copies
3. fix `int` scaling risks
4. benchmark again
5. only then evaluate load factor, probe strategy, or a stable string-pool redesign

The current implementation is the balanced middle ground: no STL maps, no per-key node allocation, and no assumption that every ID is numeric and nicely ordered.

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

## Completed in this branch

1. Opt-in checkpoint snapshots with measured checkpoint time/bytes
2. Adaptive large-import checkpoint spacing
3. Buffered `.dat` writer and atomic cache-file replacement
4. `Vector`/reserve sizing on `size_t`
5. `HashTable`, `IdTable`, and `RecordStorage` pre-sizing hooks
6. Duplicate string ownership removed from `HashTable`
7. Open-addressed `HashTable` with double-hashing probes
8. Packed 32-byte `DataRecords`
9. `Vector` memcpy fast path for trivially copyable types
10. `1M+` record pre-sizing from CSV estimates
11. Reusable merge buffer
12. Radix-hybrid sort with an explicit merge fallback
13. Guarded hot-path sizing and `size_t` loops in the importer and index builder
14. Direct numeric fast path for the CSV ID shapes used by HALO
15. Parallel index building for large datasets
16. Benchmarking on `1M` and `20M` cold imports
17. Batched multi-thread CSV parsing with sequential commit for deterministic ID mappings

## Still open for a later pass

1. Any deeper storage/layout redesign if the data shape changes

Expected outcome:

- the completed branch should already be the safe baseline for coursework
- the remaining future-only item is conditional on a different data shape

## Latest Benchmarks

On the current branch and machine, cold CSV import with `saveCache=0` and `resumeCheckpoint=0` produced. The newest `1M` measurements include packed records and the buffered `.dat` writer:

- `1M`, `radix-hybrid`: about `664 ms` total, `570 ms` CSV import, `45 ms` sort, `23 ms` index build
- `1M`, `.dat` cache save: about `36 ms`; loading the saved `.dat` afterward took about `66 ms`
- `2M`, `radix-hybrid`: about `2.1 s` total on a cold run, with all counts matching
- `20M`, `radix-hybrid`: about `13.5 s` total, `10.3 s` CSV import, `2.0 s` sort, `0.48 s` index build
- `20M`, `merge`: still much slower than radix-hybrid on the same data shape, so merge is a baseline for comparison, not the default
- `20M` checkpoint resume: interrupted after the first `2M` snapshot, restarted with `resumedCheckpoint=true`, and completed with all record/unique counts matching
- `20M` `.dat` cache load via memory map: about `1.8 s` binary load, `0.48 s` index build, with all counts matching
- `1M`, forced `parallelCsv=1`: counts matched, but CSV import was slower than the default path on this machine, so parallel CSV parsing remains opt-in/very-large-file-only

---

## Recommended Priority For This Project

If the target is a course project with limited time, the best order is:

1. keep the current completed branch as the baseline
2. use the current memory-mapped `.dat` loader as the baseline for cache loads
3. use the external/partitioned sort fallback when radix-hybrid would overrun memory headroom
4. enable `parallelCsv=1` only for benchmarks or very large files where it beats the single-thread path
5. tune hash-table load factor/probing only if profiling shows non-numeric ID lookup dominates

If the target is an aggressive production-scale system, then later add:

1. better on-disk format
2. broader partitioning
3. maybe stable string-pool or column-style storage

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
6. Lowering hash-table load factor blindly without measuring memory cost
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

Only after that should we decide whether heavier redesigns like broader partitioning or column storage are worth the cost. The current multi-thread parser should stay opt-in/very-large-file-only unless benchmarks prove it wins.

---

**Version:** 2.2
**Last updated:** June 2026
**Focus:** current HALO implementation, large CSV workloads, realistic optimization order
