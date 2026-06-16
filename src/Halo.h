#pragma once
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>

#include "GraphNodes.h"
#include "RecordStorage.h"
#include "Sort.h"
#include "Vector.h"
#include "idTable.h"
using std::string;

// Kết quả truy vấn cho mỗi hàng (row) dữ liệu
struct QueryRow {
    string user;
    string device;
    string app;
    string resource;
    string eventType;
    string location;
    uint64_t timestamp;
    uint64_t count = 1;

    bool operator<=(const QueryRow& other) const {
        return timestamp <= other.timestamp;
    }

    bool operator<(const QueryRow& other) const {
        return timestamp < other.timestamp;
    }
};

struct AnomalyParams {
    string type;
    uint64_t n = 3;
    uint64_t windowSec = 3600;
    uint64_t minGapSec = 2;
    uint64_t minSpacingSec = 43200;
    uint64_t minEvents = 6;
    uint64_t maxCvPercent = 10;
    uint64_t failureRatioPercent = 90;
    int startHour = 8;
    int endHour = 18;
    uint64_t sessionSec = 43200;
    uint64_t maxDurationSec = 5;
    uint64_t silenceSec = 86400;
    uint64_t burstSec = 3600;
    uint64_t coveragePercent = 80;
};

struct AnomalyEvidence {
    string eventType;
    string user;
    string device;
    string app;
    string resource;
    string location;
    uint64_t timestamp = 0;
    uint64_t count = 1;
};

struct AnomalyResult {
    string type;
    string severity;
    string user;
    string device;
    string app;
    string resource;
    string location;
    uint64_t timestamp = 0;
    uint64_t endTimestamp = 0;
    uint64_t count = 0;
    string detail;
    Vector<AnomalyEvidence> records;
};

struct TravelEstimate {
    bool valid = false;
    bool sharesBorder = false;
    double distanceKm = 0.0;
    uint64_t minimumSeconds = 0;
};

struct FinalizeMetrics {
    double sortTimeMs = 0.0;
    double deduplicateTimeMs = 0.0;
    double indexBuildTimeMs = 0.0;
    double finalizeTimeMs = 0.0;
    string sortAlgorithm;
};

class Halo {
   private:
    IdTable users;
    IdTable devices;
    IdTable apps;
    IdTable resources;

    RecordStorage records;

    Vector<User*> userNodes;
    Vector<Device*> deviceNodes;
    Vector<App*> appNodes;
    Vector<Resource*> resourceNodes;
    bool buildGraph = false;
    SortMode sortMode = SortMode::Auto;

    uint64_t totalReadCount = 0;
    uint64_t skippedRowCount = 0;
    uint64_t replacedFieldRowCount = 0;
    uint64_t duplicateMergedCount = 0;
    FinalizeMetrics finalizeMetrics;

    Vector<int> userOffsets;
    Vector<int> userRecordIndices;
    Vector<int> deviceOffsets;
    Vector<int> deviceRecordIndices;
    Vector<int> resourceOffsets;
    Vector<int> resourceRecordIndices;
    bool indexesBuilt = false;

    // Helper: chuyển enum sang chuỗi
    string eventToString(event_Type ev) const;
    string locationToString(location loc) const;

    // Helper: sắp xếp kết quả theo timestamp
    void sortQueryResults(Vector<QueryRow>& results) const;
    void clearGraphNodes();
    void clearIndexes();
    void buildIndexes();
    void appendAnomaly(Vector<AnomalyResult>& results, int maxResults,
                       uint64_t& total, const string& type,
                       const string& severity, const DataRecords& rec,
                       uint64_t count, const string& detail,
                       std::ostream* fullOutput) const;
    void appendAnomalyGroup(Vector<AnomalyResult>& results, int maxResults,
                            uint64_t& total, const string& type,
                            const string& severity, const DataRecords& anchor,
                            uint64_t count, const string& detail,
                            const Vector<DataRecords>& evidence,
                            std::ostream* fullOutput) const;
    AnomalyEvidence makeEvidence(const DataRecords& rec) const;
    int timezoneOffsetMinutes(location loc) const;
    TravelEstimate estimateTravel(location from, location to) const;

   public:
    Halo();
    ~Halo();

    void processRecord(const DataRecords& newRecord, std::string_view userId,
                       std::string_view deviceId, std::string_view appId,
                       std::string_view resourceId);
    void setBuildGraph(bool enabled) {
        buildGraph = enabled;
    }
    void setSortMode(SortMode mode) {
        sortMode = mode;
    }

    void clear();
    void reserveForImport(size_t expectedRecords,
                          size_t expectedUsers = 0,
                          size_t expectedDevices = 0,
                          size_t expectedApps = 0,
                          size_t expectedResources = 0);
    void finalizeLoading();
    const FinalizeMetrics& getFinalizeMetrics() const {
        return finalizeMetrics;
    }

    bool saveToBinary(const string& filename) const;
    bool loadFromBinary(const string& filename, bool rebuildGraph = false);

    // Các hàm khai thác hệ thống

    // Query 1: Với 1 người dùng, liệt kê hành trình Device -> App -> Resource trong 1 khoảng thời gian
    void queryUserJourney(const string& userId, uint64_t startTime,
                          uint64_t endTime, Vector<QueryRow>& results) const;

    // Query 2: Với 1 tài nguyên, liệt kê hành trình User -> Device -> App trong 1 khoảng thời gian
    void queryResourceAccess(const string& resourceId, uint64_t startTime,
                             uint64_t endTime, Vector<QueryRow>& results) const;

    // Query 3: Thống kê top 10 tài nguyên được truy xuất nhiều nhất trong 1 khoảng thời gian
    void queryTopResources(uint64_t startTime, uint64_t endTime,
                           Vector<string>& resourceNames,
                           Vector<uint64_t>& accessCounts) const;

    uint64_t detectAnomalies(const AnomalyParams& params,
                             Vector<AnomalyResult>& results,
                             int maxResults,
                             std::ostream* fullOutput = nullptr) const;

    // Getter cho thống kê
    size_t getUserCount() const {
        return users.size();
    }
    size_t getDeviceCount() const {
        return devices.size();
    }
    size_t getAppCount() const {
        return apps.size();
    }
    size_t getResourceCount() const {
        return resources.size();
    }
    uint64_t getRecordCount() const {
        return records.count64();
    }

    // Getter và Setter cho thống kê chất lượng dữ liệu
    void incrementTotalRead() {
        totalReadCount++;
    }
    void incrementSkipped() {
        skippedRowCount++;
    }
    void incrementReplaced() {
        replacedFieldRowCount++;
    }
    void setDuplicateMerged(uint64_t count) {
        duplicateMergedCount = count;
    }

    uint64_t getTotalReadCount() const {
        return totalReadCount;
    }
    uint64_t getSkippedRowCount() const {
        return skippedRowCount;
    }
    uint64_t getReplacedFieldRowCount() const {
        return replacedFieldRowCount;
    }
    uint64_t getDuplicateMergedCount() const {
        return duplicateMergedCount;
    }
};
