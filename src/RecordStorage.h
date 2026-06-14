#pragma once
#include <cstdint>

#include "DataRecords.h"
#include "Vector.h"

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
    DataRecords getRecord(int index) const;
    const DataRecords& getRecordRef(int index) const;
    int size() const;
    uint64_t count64() const;
    void clear();

    void sortRecords();
    uint64_t removeDuplicates();

    int findStartIndex(uint64_t startTime) const;
    int findEndIndex(uint64_t endTime) const;
};
