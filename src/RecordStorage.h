#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "DataRecords.h"
#include "Vector.h"

enum class SortMode {
    Auto,
    Merge,
    Intro,
    RadixHybrid,
    ExternalPartitioned
};

class RecordStorage {
   private:
    Vector<DataRecords> Records;

   public:
    const Vector<DataRecords>& getRecords() const;
    void setRecords(const Vector<DataRecords>& newRecs);
    void setRecords(Vector<DataRecords>&& newRecs);

    RecordStorage();
    ~RecordStorage();

    int addRecord(const DataRecords& record);
    DataRecords getRecord(size_t index) const;
    const DataRecords& getRecordRef(size_t index) const;
    size_t size() const;
    uint64_t count64() const;
    void reserve(size_t expectedRecords);
    void clear();

    void sortRecords(SortMode mode = SortMode::Auto,
                     std::string* usedAlgorithm = nullptr);
    uint64_t removeDuplicates();

    int findStartIndex(uint64_t startTime) const;
    int findEndIndex(uint64_t endTime) const;
};
