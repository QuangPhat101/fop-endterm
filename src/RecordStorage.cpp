#include "RecordStorage.h"

#include <utility>

#include "Sort.h"

RecordStorage::RecordStorage() {}
RecordStorage::~RecordStorage() {}

int RecordStorage::addRecord(const DataRecords& Record) {
    int index = Records.size();
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

DataRecords RecordStorage::getRecord(int index) const {
    return Records[index];
}

const DataRecords& RecordStorage::getRecordRef(int index) const {
    return Records[index];
}

int RecordStorage::size() const {
    return Records.size();
}

uint64_t RecordStorage::count64() const {
    return static_cast<uint64_t>(Records.size());
}

void RecordStorage::clear() {
    Records.clear();
}

void RecordStorage::sortRecords() {
    if (Records.size() > 1) {
        mergeSort(Records, 0, Records.size() - 1);
    }
}

uint64_t RecordStorage::removeDuplicates() {
    if (Records.size() <= 1) {
        return 0;
    }
    uint64_t mergedCount = 0;
    int writeIndex = 0;
    for (int i = 1; i < Records.size(); i++) {
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
    Records.setSize(writeIndex + 1);
    Records.shrinkToFit();
    return mergedCount;
}

int RecordStorage::findStartIndex(uint64_t startTime) const {
    int left = 0;
    int right = Records.size() - 1;
    int result = -1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (Records[mid].timestamp >= startTime) {
            result = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    return result;
}

int RecordStorage::findEndIndex(uint64_t endTime) const {
    int left = 0;
    int right = Records.size() - 1;
    int result = -1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (Records[mid].timestamp <= endTime) {
            result = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return result;
}
