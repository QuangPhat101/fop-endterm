#include "RecordStorage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <windows.h>

#include "Sort.h"

namespace {
constexpr size_t RADIX_BUCKETS = 1ULL << 16;
constexpr size_t INSERTION_SORT_THRESHOLD = 32;
constexpr size_t RADIX_TIE_GROUP_THRESHOLD = 8192;
constexpr size_t AUTO_RADIX_THRESHOLD = 200000;
constexpr size_t EXTERNAL_SORT_CHUNK_RECORDS = 2000000ULL;
constexpr size_t AUTO_EXTERNAL_MIN_RECORDS = 30000000ULL;

static_assert(std::is_trivially_copyable_v<DataRecords>,
              "DataRecords must stay trivially copyable for fast block copies");

bool recordLess(const DataRecords& a, const DataRecords& b) {
    if (a.timestamp != b.timestamp) {
        return a.timestamp < b.timestamp;
    }
    if (a.userID != b.userID) {
        return a.userID < b.userID;
    }
    if (a.deviceID != b.deviceID) {
        return a.deviceID < b.deviceID;
    }
    if (a.appID != b.appID) {
        return a.appID < b.appID;
    }
    if (a.resourceID != b.resourceID) {
        return a.resourceID < b.resourceID;
    }
    if (a.eventTypeTag != b.eventTypeTag) {
        return a.eventTypeTag < b.eventTypeTag;
    }
    return a.locationTag < b.locationTag;
}

bool recordTieLess(const DataRecords& a, const DataRecords& b) {
    if (a.userID != b.userID) {
        return a.userID < b.userID;
    }
    if (a.deviceID != b.deviceID) {
        return a.deviceID < b.deviceID;
    }
    if (a.appID != b.appID) {
        return a.appID < b.appID;
    }
    if (a.resourceID != b.resourceID) {
        return a.resourceID < b.resourceID;
    }
    if (a.eventTypeTag != b.eventTypeTag) {
        return a.eventTypeTag < b.eventTypeTag;
    }
    return a.locationTag < b.locationTag;
}

uint64_t availablePhysicalMemoryBytes() {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return 0;
    }
    return status.ullAvailPhys;
}

bool shouldUseExternalPartitionedSort(size_t recordCount) {
    if (recordCount < AUTO_EXTERNAL_MIN_RECORDS) {
        return false;
    }

    uint64_t recordBytes = 0;
    if (recordCount > std::numeric_limits<uint64_t>::max() / sizeof(DataRecords)) {
        return true;
    }
    recordBytes = static_cast<uint64_t>(recordCount) * sizeof(DataRecords);

    uint64_t available = availablePhysicalMemoryBytes();
    if (available == 0) {
        return true;
    }

    uint64_t estimatedSortBytes = recordBytes * 2ULL;
    return estimatedSortBytes > (available * 7ULL) / 10ULL;
}

std::filesystem::path makeExternalSortRunPath(size_t runIndex) {
    std::filesystem::path tempDir;
    try {
        tempDir = std::filesystem::temp_directory_path();
    } catch (...) {
        return {};
    }

    std::string name = "halo_extsort_" + std::to_string(GetCurrentProcessId()) + "_" +
                       std::to_string(runIndex) + ".bin";
    return tempDir / name;
}

bool writeRunFile(const std::filesystem::path& path, const DataRecords* data,
                  size_t count) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    uint64_t storedCount = static_cast<uint64_t>(count);
    out.write(reinterpret_cast<const char*>(&storedCount), sizeof(storedCount));
    if (count > 0) {
        out.write(reinterpret_cast<const char*>(data),
                  static_cast<std::streamsize>(count * sizeof(DataRecords)));
    }
    return static_cast<bool>(out);
}

bool cleanupRunFiles(const Vector<std::filesystem::path>& runPaths) {
    bool ok = true;
    for (size_t i = 0; i < runPaths.size(); i++) {
        const auto& path = runPaths[i];
        if (std::remove(path.string().c_str()) != 0) {
            ok = false;
        }
    }
    return ok;
}

struct ExternalRunReader {
    std::ifstream stream;
    uint64_t remaining = 0;
    DataRecords current{};
    std::filesystem::path path;

    bool open(const std::filesystem::path& runPath) {
        path = runPath;
        stream.open(runPath, std::ios::binary);
        if (!stream.is_open()) {
            return false;
        }

        stream.read(reinterpret_cast<char*>(&remaining), sizeof(remaining));
        if (!stream || remaining == 0) {
            return false;
        }

        return readNext();
    }

    bool readNext() {
        if (remaining == 0) {
            return false;
        }
        stream.read(reinterpret_cast<char*>(&current), sizeof(DataRecords));
        if (!stream) {
            return false;
        }
        --remaining;
        return true;
    }
};

struct ExternalHeapNode {
    DataRecords record;
    size_t runIndex;
};

bool externalHeapLess(const ExternalHeapNode& a, const ExternalHeapNode& b) {
    return recordLess(a.record, b.record);
}

void externalHeapSiftUp(Vector<ExternalHeapNode>& heap, size_t index) {
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (!externalHeapLess(heap[index], heap[parent])) {
            break;
        }
        using std::swap;
        swap(heap[index], heap[parent]);
        index = parent;
    }
}

void externalHeapSiftDown(Vector<ExternalHeapNode>& heap, size_t index) {
    size_t count = heap.size();
    while (true) {
        size_t left = index * 2 + 1;
        size_t right = left + 1;
        size_t smallest = index;

        if (left < count && externalHeapLess(heap[left], heap[smallest])) {
            smallest = left;
        }
        if (right < count && externalHeapLess(heap[right], heap[smallest])) {
            smallest = right;
        }
        if (smallest == index) {
            break;
        }
        using std::swap;
        swap(heap[index], heap[smallest]);
        index = smallest;
    }
}

void externalHeapPush(Vector<ExternalHeapNode>& heap, const ExternalHeapNode& node) {
    heap.pushBack(node);
    externalHeapSiftUp(heap, heap.size() - 1);
}

ExternalHeapNode externalHeapPop(Vector<ExternalHeapNode>& heap) {
    ExternalHeapNode top = heap[0];
    size_t last = heap.size() - 1;
    if (last == 0) {
        heap.setSize(0);
        return top;
    }
    heap[0] = heap[last];
    heap.setSize(last);
    externalHeapSiftDown(heap, 0);
    return top;
}

bool externalPartitionedSort(Vector<DataRecords>& records) {
    const size_t total = records.size();
    if (total <= 1) {
        return true;
    }

    Vector<DataRecords> source = std::move(records);

    Vector<std::filesystem::path> runPaths;
    runPaths.reserve((total + EXTERNAL_SORT_CHUNK_RECORDS - 1) /
                     EXTERNAL_SORT_CHUNK_RECORDS);

    size_t runIndex = 0;
    for (size_t start = 0; start < total; start += EXTERNAL_SORT_CHUNK_RECORDS) {
        size_t end = std::min(total, start + EXTERNAL_SORT_CHUNK_RECORDS);
        std::sort(source.rawData() + start, source.rawData() + end, recordLess);
        std::filesystem::path runPath = makeExternalSortRunPath(runIndex++);
        if (runPath.empty() ||
            !writeRunFile(runPath, source.rawData() + start, end - start)) {
            cleanupRunFiles(runPaths);
            records = std::move(source);
            return false;
        }
        runPaths.pushBack(runPath);
    }

    records.clear();
    records.setSize(total);

    Vector<ExternalRunReader> runs;
    runs.reserve(runPaths.size());
    for (size_t i = 0; i < runPaths.size(); i++) {
        const auto& runPath = runPaths[i];
        ExternalRunReader run;
        if (!run.open(runPath)) {
            cleanupRunFiles(runPaths);
            records = std::move(source);
            return false;
        }
        runs.pushBack(std::move(run));
    }

    Vector<ExternalHeapNode> heap;
    heap.reserve(runs.size());
    size_t outIndex = 0;
    for (size_t i = 0; i < runs.size(); i++) {
        externalHeapPush(heap, ExternalHeapNode{runs[i].current, i});
    }

    while (heap.size() > 0) {
        ExternalHeapNode node = externalHeapPop(heap);
        records[outIndex++] = node.record;

        ExternalRunReader& run = runs[node.runIndex];
        if (run.readNext()) {
            externalHeapPush(heap, ExternalHeapNode{run.current, node.runIndex});
        }
    }

    cleanupRunFiles(runPaths);
    if (outIndex != total) {
        records = std::move(source);
        return false;
    }
    return true;
}

uint16_t radixKey(const DataRecords& record, int keyId) {
    switch (keyId) {
        case 0:
        case 1:
        case 2:
        case 3:
            return static_cast<uint16_t>((record.timestamp >> (keyId * 16)) & 0xFFFFULL);
        case 4:
            return static_cast<uint16_t>(
                (static_cast<unsigned int>(record.eventTypeTag) << 8) |
                static_cast<unsigned int>(record.locationTag));
        case 5:
            return static_cast<uint16_t>(static_cast<uint32_t>(record.resourceID) & 0xFFFFU);
        case 6:
            return static_cast<uint16_t>((static_cast<uint32_t>(record.resourceID) >> 16) & 0xFFFFU);
        case 7:
            return static_cast<uint16_t>(static_cast<uint32_t>(record.appID) & 0xFFFFU);
        case 8:
            return static_cast<uint16_t>((static_cast<uint32_t>(record.appID) >> 16) & 0xFFFFU);
        case 9:
            return static_cast<uint16_t>(static_cast<uint32_t>(record.deviceID) & 0xFFFFU);
        case 10:
            return static_cast<uint16_t>((static_cast<uint32_t>(record.deviceID) >> 16) & 0xFFFFU);
        case 11:
            return static_cast<uint16_t>(static_cast<uint32_t>(record.userID) & 0xFFFFU);
        case 12:
            return static_cast<uint16_t>((static_cast<uint32_t>(record.userID) >> 16) & 0xFFFFU);
        default:
            return 0;
    }
}

void copyRange(DataRecords* dst, const DataRecords* src, size_t start, size_t end) {
    size_t count = end - start;
    if (count == 0) {
        return;
    }
    std::memcpy(dst + start, src + start, count * sizeof(DataRecords));
}

void radixPassRange(const DataRecords* src, DataRecords* dst, size_t start,
                    size_t end, int keyId, Vector<size_t>& counts) {
    for (size_t i = 0; i < counts.size(); i++) {
        counts[i] = 0;
    }

    for (size_t i = start; i < end; i++) {
        counts[radixKey(src[i], keyId)]++;
    }

    size_t sum = start;
    for (size_t i = 0; i < counts.size(); i++) {
        size_t bucketSize = counts[i];
        counts[i] = sum;
        sum += bucketSize;
    }

    for (size_t i = start; i < end; i++) {
        uint16_t key = radixKey(src[i], keyId);
        dst[counts[key]++] = src[i];
    }
}

void insertionSortTies(Vector<DataRecords>& records, size_t start, size_t end) {
    if (end <= start + 1) {
        return;
    }
    for (size_t i = start + 1; i < end; i++) {
        DataRecords value = records[i];
        size_t j = i;
        while (j > start && recordTieLess(value, records[j - 1])) {
            records[j] = records[j - 1];
            j--;
        }
        records[j] = value;
    }
}

void radixSortTieGroup(Vector<DataRecords>& records, Vector<DataRecords>& buffer,
                       size_t start, size_t end,
                       Vector<size_t>& counts) {
    DataRecords* recordsData = records.rawData();
    DataRecords* src = recordsData;
    DataRecords* dst = buffer.rawData();

    for (int keyId = 4; keyId <= 12; keyId++) {
        radixPassRange(src, dst, start, end, keyId, counts);
        std::swap(src, dst);
    }

    if (src != recordsData) {
        copyRange(recordsData, src, start, end);
    }
}

void radixTimestampHybridSort(Vector<DataRecords>& records) {
    const size_t n = records.size();
    if (n <= 1) {
        return;
    }

    Vector<DataRecords> buffer;
    buffer.setSize(n);
    Vector<size_t> counts;
    counts.setSize(RADIX_BUCKETS);

    DataRecords* recordsData = records.rawData();
    DataRecords* src = recordsData;
    DataRecords* dst = buffer.rawData();

    for (int keyId = 0; keyId <= 3; keyId++) {
        radixPassRange(src, dst, 0, n, keyId, counts);
        std::swap(src, dst);
    }

    if (src != recordsData) {
        copyRange(recordsData, src, 0, n);
    }

    size_t start = 0;
    while (start < n) {
        size_t end = start + 1;
        uint64_t timestamp = records[start].timestamp;
        while (end < n && records[end].timestamp == timestamp) {
            end++;
        }

        size_t groupSize = end - start;
        if (groupSize > 1) {
            if (groupSize <= INSERTION_SORT_THRESHOLD) {
                insertionSortTies(records, start, end);
            } else if (groupSize >= RADIX_TIE_GROUP_THRESHOLD) {
                radixSortTieGroup(records, buffer, start, end, counts);
            } else {
                hybridSort(records, static_cast<int>(start),
                           static_cast<int>(end - 1), recordTieLess);
            }
        }

        start = end;
    }
}
}  // namespace

RecordStorage::RecordStorage() {}
RecordStorage::~RecordStorage() {}

int RecordStorage::addRecord(const DataRecords& Record) {
    if (Records.size() >= static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error("Qua nhieu ban ghi cho chi so int noi bo");
    }
    int index = static_cast<int>(Records.size());
    Records.pushBack(Record);
    return index;
}

const Vector<DataRecords>& RecordStorage::getRecords() const {
    return Records;
}

void RecordStorage::setRecords(const Vector<DataRecords>& newRecs) {
    Records = newRecs;
}

void RecordStorage::setRecords(Vector<DataRecords>&& newRecs) {
    Records = std::move(newRecs);
}

DataRecords RecordStorage::getRecord(size_t index) const {
    return Records[index];
}

const DataRecords& RecordStorage::getRecordRef(size_t index) const {
    return Records[index];
}

size_t RecordStorage::size() const {
    return Records.size();
}

uint64_t RecordStorage::count64() const {
    return static_cast<uint64_t>(Records.size());
}

void RecordStorage::reserve(size_t expectedRecords) {
    Records.reserve(expectedRecords);
}

void RecordStorage::clear() {
    Records.clear();
}

void RecordStorage::sortRecords(SortMode mode, std::string* usedAlgorithm) {
    SortMode actualMode = mode;
    if (actualMode == SortMode::Auto) {
        if (shouldUseExternalPartitionedSort(Records.size())) {
            actualMode = SortMode::ExternalPartitioned;
        } else {
            actualMode = Records.size() >= AUTO_RADIX_THRESHOLD ? SortMode::RadixHybrid
                                                                : SortMode::Intro;
        }
    }

    switch (actualMode) {
        case SortMode::Merge:
            if (usedAlgorithm != nullptr) {
                *usedAlgorithm = "merge";
            }
            mergeSort(Records);
            break;
        case SortMode::Intro:
            if (usedAlgorithm != nullptr) {
                *usedAlgorithm = "hybrid";
            }
            hybridSort(Records, recordLess);
            break;
        case SortMode::RadixHybrid:
            if (usedAlgorithm != nullptr) {
                *usedAlgorithm = "radix-hybrid";
            }
            radixTimestampHybridSort(Records);
            break;
        case SortMode::ExternalPartitioned:
            if (usedAlgorithm != nullptr) {
                *usedAlgorithm = "external-partitioned";
            }
            if (!externalPartitionedSort(Records)) {
                if (usedAlgorithm != nullptr) {
                    *usedAlgorithm = "radix-hybrid";
                }
                radixTimestampHybridSort(Records);
            }
            break;
        case SortMode::Auto:
            break;
    }
}

uint64_t RecordStorage::removeDuplicates() {
    if (Records.size() <= 1) {
        return 0;
    }
    uint64_t mergedCount = 0;
    size_t writeIndex = 0;
    for (size_t i = 1; i < Records.size(); i++) {
        if (Records[i] == Records[writeIndex]) {
            Records[writeIndex].Count += Records[i].Count;
            mergedCount++;
        } else {
            writeIndex++;
            if (writeIndex != i) {
                Records[writeIndex] = Records[i];
            }
        }
    }
    size_t newSize = writeIndex + 1;
    size_t oldCapacity = Records.getCapacity();
    Records.setSize(newSize);
    size_t slack = oldCapacity > newSize ? oldCapacity - newSize : 0;
    size_t minSlack = newSize / 8 + 1024;
    if (slack > minSlack) {
        Records.shrinkToFit();
    }
    return mergedCount;
}

int RecordStorage::findStartIndex(uint64_t startTime) const {
    if (Records.size() == 0) {
        return -1;
    }
    size_t left = 0;
    size_t right = Records.size() - 1;
    int result = -1;
    while (left <= right) {
        size_t mid = left + (right - left) / 2;
        if (Records[mid].timestamp >= startTime) {
            result = static_cast<int>(mid);
            if (mid == 0) {
                break;
            }
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    return result;
}

int RecordStorage::findEndIndex(uint64_t endTime) const {
    if (Records.size() == 0) {
        return -1;
    }
    size_t left = 0;
    size_t right = Records.size() - 1;
    int result = -1;
    while (left <= right) {
        size_t mid = left + (right - left) / 2;
        if (Records[mid].timestamp <= endTime) {
            result = static_cast<int>(mid);
            left = mid + 1;
        } else {
            if (mid == 0) {
                break;
            }
            right = mid - 1;
        }
    }
    return result;
}
