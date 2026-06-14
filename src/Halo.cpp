#include "Halo.h"

#include <windows.h>

#include <utility>

Halo::Halo() {}

Halo::~Halo() {
    clearGraphNodes();
}

void Halo::clearGraphNodes() {
    for (int i = 0; i < userNodes.size(); i++) {
        delete userNodes[i];
    }
    for (int i = 0; i < deviceNodes.size(); i++) {
        delete deviceNodes[i];
    }
    for (int i = 0; i < appNodes.size(); i++) {
        delete appNodes[i];
    }
    for (int i = 0; i < resourceNodes.size(); i++) {
        delete resourceNodes[i];
    }
    userNodes.clear();
    deviceNodes.clear();
    appNodes.clear();
    resourceNodes.clear();
}

void Halo::clearIndexes() {
    userOffsets.clear();
    userRecordIndices.clear();
    deviceOffsets.clear();
    deviceRecordIndices.clear();
    resourceOffsets.clear();
    resourceRecordIndices.clear();
    indexesBuilt = false;
}

string Halo::eventToString(event_Type ev) const {
    switch (ev) {
        case LOGIN:
            return "LOGIN";
        case LOGOUT:
            return "LOGOUT";
        case TOKEN_REFRESH:
            return "TOKEN_REFRESH";
        case ACCESS:
            return "ACCESS";
        case FAILED_LOGIN:
            return "FAILED_LOGIN";
        case OPEN_APP:
            return "OPEN_APP";
        case DOWNLOAD:
            return "DOWNLOAD";
        case ADMIN_ACTION:
            return "ADMIN_ACTION";
        default:
            return "UNKNOWN";
    }
}

string Halo::locationToString(location loc) const {
    switch (loc) {
        case LOC_US:
            return "US";
        case LOC_VN:
            return "VN";
        case LOC_JP:
            return "JP";
        case LOC_KR:
            return "KR";
        case LOC_SG:
            return "SG";
        case LOC_CN:
            return "CN";
        case LOC_DE:
            return "DE";
        case LOC_FR:
            return "FR";
        case LOC_UK:
            return "UK";
        case LOC_AU:
            return "AU";
        case LOC_CA:
            return "CA";
        case LOC_IN:
            return "IN";
        case LOC_BR:
            return "BR";
        case LOC_RU:
            return "RU";
        case LOC_TH:
            return "TH";
        default:
            return "UNKNOWN";
    }
}

void Halo::sortQueryResults(Vector<QueryRow>& results) const {
    if (results.size() > 1) {
        mergeSort(results, 0, results.size() - 1);
    }
}

void Halo::buildIndexes() {
    clearIndexes();

    int recCount = records.size();
    int userCount = users.size();
    int deviceCount = devices.size();
    int resourceCount = resources.size();

    for (int i = 0; i <= userCount; i++) {
        userOffsets.pushBack(0);
    }
    for (int i = 0; i <= deviceCount; i++) {
        deviceOffsets.pushBack(0);
    }
    for (int i = 0; i <= resourceCount; i++) {
        resourceOffsets.pushBack(0);
    }

    const Vector<DataRecords>& recs = records.getRecords();
    for (int i = 0; i < recCount; i++) {
        userOffsets[recs[i].userID + 1]++;
        deviceOffsets[recs[i].deviceID + 1]++;
        resourceOffsets[recs[i].resourceID + 1]++;
    }

    for (int i = 1; i <= userCount; i++) {
        userOffsets[i] += userOffsets[i - 1];
    }
    for (int i = 1; i <= deviceCount; i++) {
        deviceOffsets[i] += deviceOffsets[i - 1];
    }
    for (int i = 1; i <= resourceCount; i++) {
        resourceOffsets[i] += resourceOffsets[i - 1];
    }

    userRecordIndices.setSize(recCount);
    deviceRecordIndices.setSize(recCount);
    resourceRecordIndices.setSize(recCount);

    Vector<int> userCursor = userOffsets;
    Vector<int> deviceCursor = deviceOffsets;
    Vector<int> resourceCursor = resourceOffsets;

    for (int i = 0; i < recCount; i++) {
        const DataRecords& rec = recs[i];
        userRecordIndices[userCursor[rec.userID]++] = i;
        deviceRecordIndices[deviceCursor[rec.deviceID]++] = i;
        resourceRecordIndices[resourceCursor[rec.resourceID]++] = i;
    }

    indexesBuilt = true;
}

void Halo::processRecord(const DataRecords& newRecord, const string& userId,
                         const string& deviceId, const string& appId,
                         const string& resourceId) {
    int uIdx = users.getOrAdd(userId);
    int dIdx = devices.getOrAdd(deviceId);
    int aIdx = apps.getOrAdd(appId);
    int rIdx = resources.getOrAdd(resourceId);

    if (buildGraph) {
        while (userNodes.size() <= uIdx) {
            userNodes.pushBack(new User(userNodes.size()));
        }
        while (deviceNodes.size() <= dIdx) {
            deviceNodes.pushBack(new Device(deviceNodes.size()));
        }
        while (appNodes.size() <= aIdx) {
            appNodes.pushBack(new App(appNodes.size()));
        }
        while (resourceNodes.size() <= rIdx) {
            resourceNodes.pushBack(new Resource(resourceNodes.size()));
        }

        userNodes[uIdx]->connectedDevices.pushBackUnique(dIdx);
        deviceNodes[dIdx]->connectedUsers.pushBackUnique(uIdx);
        deviceNodes[dIdx]->connectedApps.pushBackUnique(aIdx);
        appNodes[aIdx]->connectedDevices.pushBackUnique(dIdx);
        appNodes[aIdx]->connectedResources.pushBackUnique(rIdx);
        resourceNodes[rIdx]->connectedApps.pushBackUnique(aIdx);
    }

    DataRecords rec = newRecord;
    rec.userID = uIdx;
    rec.deviceID = dIdx;
    rec.appID = aIdx;
    rec.resourceID = rIdx;
    records.addRecord(rec);
    indexesBuilt = false;
}

void Halo::clear() {
    users.clear();
    devices.clear();
    apps.clear();
    resources.clear();
    records.clear();
    clearGraphNodes();
    clearIndexes();
    totalReadCount = 0;
    skippedRowCount = 0;
    replacedFieldRowCount = 0;
    duplicateMergedCount = 0;
}

void Halo::finalizeLoading() {
    records.sortRecords();
    uint64_t merged = records.removeDuplicates();
    users.shrinkToFit();
    devices.shrinkToFit();
    apps.shrinkToFit();
    resources.shrinkToFit();
    setDuplicateMerged(merged);
    buildIndexes();
}

static bool writeAll(HANDLE hFile, const void* data, unsigned long long bytes) {
    const char* ptr = reinterpret_cast<const char*>(data);
    DWORD written = 0;
    while (bytes > 0) {
        DWORD chunk = bytes > 64ULL * 1024ULL * 1024ULL ? 64U * 1024U * 1024U : (DWORD)bytes;
        if (!WriteFile(hFile, ptr, chunk, &written, NULL) || written != chunk) {
            return false;
        }
        ptr += written;
        bytes -= written;
    }
    return true;
}

static bool readAll(HANDLE hFile, void* data, unsigned long long bytes) {
    char* ptr = reinterpret_cast<char*>(data);
    DWORD read = 0;
    while (bytes > 0) {
        DWORD chunk = bytes > 64ULL * 1024ULL * 1024ULL ? 64U * 1024U * 1024U : (DWORD)bytes;
        if (!ReadFile(hFile, ptr, chunk, &read, NULL) || read != chunk) {
            return false;
        }
        ptr += read;
        bytes -= read;
    }
    return true;
}

bool Halo::saveToBinary(const string& filename) const {
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    uint32_t magic = 0x48414C4F;
    uint32_t version = 2;
    if (!writeAll(hFile, &magic, sizeof(magic)) ||
        !writeAll(hFile, &version, sizeof(version))) {
        CloseHandle(hFile);
        return false;
    }

    auto writeNames = [&](const IdTable& table) -> bool {
        const Vector<string>& names = table.getNames();
        int size = names.size();
        if (!writeAll(hFile, &size, sizeof(size))) {
            return false;
        }
        for (int i = 0; i < size; i++) {
            const string& name = names[i];
            int len = (int)name.length();
            if (!writeAll(hFile, &len, sizeof(len))) {
                return false;
            }
            if (len > 0 && !writeAll(hFile, name.c_str(), len)) {
                return false;
            }
        }
        return true;
    };

    bool ok = writeNames(users) && writeNames(devices) && writeNames(apps) && writeNames(resources) &&
              writeAll(hFile, &totalReadCount, sizeof(totalReadCount)) &&
              writeAll(hFile, &skippedRowCount, sizeof(skippedRowCount)) &&
              writeAll(hFile, &replacedFieldRowCount, sizeof(replacedFieldRowCount)) &&
              writeAll(hFile, &duplicateMergedCount, sizeof(duplicateMergedCount));

    int recSize = records.size();
    ok = ok && writeAll(hFile, &recSize, sizeof(recSize));
    if (ok && recSize > 0) {
        const Vector<DataRecords>& recs = records.getRecords();
        ok = writeAll(hFile, recs.rawData(), (unsigned long long)recSize * sizeof(DataRecords));
    }

    CloseHandle(hFile);
    return ok;
}

bool Halo::loadFromBinary(const string& filename, bool rebuildGraph) {
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!readAll(hFile, &magic, sizeof(magic)) || magic != 0x48414C4F ||
        !readAll(hFile, &version, sizeof(version)) || version != 2) {
        CloseHandle(hFile);
        return false;
    }

    clear();
    auto failAfterClear = [&]() {
        CloseHandle(hFile);
        clear();
        return false;
    };

    auto readNames = [&](IdTable& table) -> bool {
        int size = 0;
        if (!readAll(hFile, &size, sizeof(size)) || size < 0) {
            return false;
        }
        Vector<string> names;
        names.reserve(size);
        for (int i = 0; i < size; i++) {
            int len = 0;
            if (!readAll(hFile, &len, sizeof(len)) || len < 0) {
                return false;
            }
            string name;
            if (len > 0) {
                name.resize(len);
                if (!readAll(hFile, &name[0], len)) {
                    return false;
                }
            }
            names.pushBack(name);
        }
        table.setNames(std::move(names));
        return true;
    };

    if (!readNames(users) || !readNames(devices) || !readNames(apps) || !readNames(resources) ||
        !readAll(hFile, &totalReadCount, sizeof(totalReadCount)) ||
        !readAll(hFile, &skippedRowCount, sizeof(skippedRowCount)) ||
        !readAll(hFile, &replacedFieldRowCount, sizeof(replacedFieldRowCount)) ||
        !readAll(hFile, &duplicateMergedCount, sizeof(duplicateMergedCount))) {
        return failAfterClear();
    }

    int recSize = 0;
    if (!readAll(hFile, &recSize, sizeof(recSize)) || recSize < 0) {
        return failAfterClear();
    }

    Vector<DataRecords> recs;
    recs.setSize(recSize);
    if (recSize > 0 && !readAll(hFile, recs.rawData(), (unsigned long long)recSize * sizeof(DataRecords))) {
        return failAfterClear();
    }
    records.setRecords(std::move(recs));
    CloseHandle(hFile);
    buildIndexes();

    if (!rebuildGraph) {
        return true;
    }

    int uSize = users.size();
    int dSize = devices.size();
    int aSize = apps.size();
    int rSize = resources.size();
    userNodes.reserve(uSize);
    deviceNodes.reserve(dSize);
    appNodes.reserve(aSize);
    resourceNodes.reserve(rSize);
    for (int i = 0; i < uSize; i++) {
        userNodes.pushBack(new User(i));
    }
    for (int i = 0; i < dSize; i++) {
        deviceNodes.pushBack(new Device(i));
    }
    for (int i = 0; i < aSize; i++) {
        appNodes.pushBack(new App(i));
    }
    for (int i = 0; i < rSize; i++) {
        resourceNodes.pushBack(new Resource(i));
    }

    const Vector<DataRecords>& recsList = records.getRecords();
    for (int i = 0; i < recsList.size(); i++) {
        const DataRecords& rec = recsList[i];
        userNodes[rec.userID]->connectedDevices.pushBackUnique(rec.deviceID);
        deviceNodes[rec.deviceID]->connectedUsers.pushBackUnique(rec.userID);
        deviceNodes[rec.deviceID]->connectedApps.pushBackUnique(rec.appID);
        appNodes[rec.appID]->connectedDevices.pushBackUnique(rec.deviceID);
        appNodes[rec.appID]->connectedResources.pushBackUnique(rec.resourceID);
        resourceNodes[rec.resourceID]->connectedApps.pushBackUnique(rec.appID);
    }

    return true;
}

void Halo::queryUserJourney(const string& userId, uint64_t startTime,
                            uint64_t endTime, Vector<QueryRow>& results) const {
    int uIdx = users.findId(userId);
    if (uIdx < 0 || !indexesBuilt) {
        return;
    }

    for (int p = userOffsets[uIdx]; p < userOffsets[uIdx + 1]; p++) {
        const DataRecords& rec = records.getRecordRef(userRecordIndices[p]);
        if (rec.timestamp < startTime) {
            continue;
        }
        if (rec.timestamp > endTime) {
            break;
        }
        QueryRow row;
        row.timestamp = rec.timestamp;
        row.user = userId;
        row.device = devices.getName(rec.deviceID);
        row.app = apps.getName(rec.appID);
        row.resource = resources.getName(rec.resourceID);
        row.eventType = eventToString(rec.eventTypeTag);
        row.location = locationToString(rec.locationTag);
        row.count = rec.Count;
        results.pushBack(row);
    }
}

void Halo::queryResourceAccess(const string& resourceId, uint64_t startTime,
                               uint64_t endTime, Vector<QueryRow>& results) const {
    int rIdx = resources.findId(resourceId);
    if (rIdx < 0 || !indexesBuilt) {
        return;
    }

    for (int p = resourceOffsets[rIdx]; p < resourceOffsets[rIdx + 1]; p++) {
        const DataRecords& rec = records.getRecordRef(resourceRecordIndices[p]);
        if (rec.timestamp < startTime) {
            continue;
        }
        if (rec.timestamp > endTime) {
            break;
        }
        QueryRow row;
        row.timestamp = rec.timestamp;
        row.user = users.getName(rec.userID);
        row.device = devices.getName(rec.deviceID);
        row.app = apps.getName(rec.appID);
        row.resource = resourceId;
        row.eventType = eventToString(rec.eventTypeTag);
        row.location = locationToString(rec.locationTag);
        row.count = rec.Count;
        results.pushBack(row);
    }
}

void Halo::queryTopResources(uint64_t startTime, uint64_t endTime,
                             Vector<string>& resourceNames, Vector<uint64_t>& accessCounts) const {
    int totalResources = resources.size();
    if (totalResources == 0) {
        return;
    }

    Vector<uint64_t> counts(totalResources);
    Vector<uint64_t> lastTimestamps(totalResources);
    for (int i = 0; i < totalResources; i++) {
        counts.pushBack(0);
        lastTimestamps.pushBack(0);
    }

    int startIdx = records.findStartIndex(startTime);
    int endIdx = records.findEndIndex(endTime);
    if (startIdx != -1 && endIdx != -1) {
        for (int i = startIdx; i <= endIdx; i++) {
            const DataRecords& rec = records.getRecordRef(i);
            if (rec.eventTypeTag != ACCESS && rec.eventTypeTag != DOWNLOAD &&
                rec.eventTypeTag != OPEN_APP && rec.eventTypeTag != ADMIN_ACTION) {
                continue;
            }
            counts[rec.resourceID] += rec.Count;
            if (rec.timestamp > lastTimestamps[rec.resourceID]) {
                lastTimestamps[rec.resourceID] = rec.timestamp;
            }
        }
    }

    int topK = totalResources < 10 ? totalResources : 10;
    Vector<bool> used(totalResources);
    for (int i = 0; i < totalResources; i++) {
        used.pushBack(false);
    }

    for (int t = 0; t < topK; t++) {
        int maxIdx = -1;
        uint64_t maxCount = 0;
        uint64_t maxLastTimestamp = 0;
        for (int i = 0; i < totalResources; i++) {
            if (used[i]) {
                continue;
            }
            if (counts[i] > maxCount ||
                (counts[i] == maxCount && counts[i] > 0 && lastTimestamps[i] > maxLastTimestamp)) {
                maxCount = counts[i];
                maxIdx = i;
                maxLastTimestamp = lastTimestamps[i];
            }
        }
        if (maxIdx < 0 || maxCount == 0) {
            break;
        }
        used[maxIdx] = true;
        resourceNames.pushBack(resources.getName(maxIdx));
        accessCounts.pushBack(maxCount);
    }
}

void Halo::appendAnomaly(Vector<AnomalyResult>& results, int maxResults, uint64_t& total,
                         const string& type, const string& severity, const DataRecords& rec,
                         uint64_t count, const string& detail,
                         std::ostream* fullOutput) const {
    total++;
    if (fullOutput != nullptr) {
        *fullOutput << "[" << type << "] "
                    << "severity=" << severity
                    << " user=" << users.getName(rec.userID)
                    << " device=" << devices.getName(rec.deviceID)
                    << " app=" << apps.getName(rec.appID)
                    << " resource=" << resources.getName(rec.resourceID)
                    << " location=" << locationToString(rec.locationTag)
                    << " time=" << rec.timestamp
                    << " count=" << count
                    << " detail=" << detail << "\n";
    }
    if (maxResults >= 0 && results.size() >= maxResults) {
        return;
    }
    AnomalyResult row;
    row.type = type;
    row.severity = severity;
    row.user = users.getName(rec.userID);
    row.device = devices.getName(rec.deviceID);
    row.app = apps.getName(rec.appID);
    row.resource = resources.getName(rec.resourceID);
    row.location = locationToString(rec.locationTag);
    row.timestamp = rec.timestamp;
    row.count = count;
    row.detail = detail;
    results.pushBack(row);
}

int Halo::timezoneOffsetMinutes(location loc) const {
    switch (loc) {
        case LOC_VN:
            return 420;
        case LOC_US:
            return -300;
        case LOC_JP:
            return 540;
        case LOC_KR:
            return 540;
        case LOC_SG:
            return 480;
        case LOC_CN:
            return 480;
        case LOC_DE:
            return 60;
        case LOC_FR:
            return 60;
        case LOC_UK:
            return 0;
        case LOC_AU:
            return 600;
        case LOC_CA:
            return -300;
        case LOC_IN:
            return 330;
        case LOC_BR:
            return -180;
        case LOC_RU:
            return 180;
        case LOC_TH:
            return 420;
        default:
            return 0;
    }
}

static bool countryPair(location a, location b, location x, location y) {
    return (a == x && b == y) || (a == y && b == x);
}

uint64_t Halo::minTravelSeconds(location from, location to) const {
    if (from == to) {
        return 0;
    }
    if (countryPair(from, to, LOC_VN, LOC_TH) || countryPair(from, to, LOC_DE, LOC_FR) ||
        countryPair(from, to, LOC_CN, LOC_VN) || countryPair(from, to, LOC_CN, LOC_KR) ||
        countryPair(from, to, LOC_DE, LOC_UK)) {
        return 3ULL * 3600ULL;
    }
    if (countryPair(from, to, LOC_VN, LOC_SG) || countryPair(from, to, LOC_VN, LOC_KR) ||
        countryPair(from, to, LOC_SG, LOC_AU) || countryPair(from, to, LOC_JP, LOC_KR)) {
        return 6ULL * 3600ULL;
    }
    return 12ULL * 3600ULL;
}

uint64_t Halo::detectAnomalies(const AnomalyParams& params, Vector<AnomalyResult>& results,
                               int maxResults, std::ostream* fullOutput) const {
    uint64_t total = 0;
    if (!indexesBuilt) {
        return 0;
    }
    const string& type = params.type;
    const Vector<DataRecords>& recs = records.getRecords();

    if (type == "A1") {
        Vector<int> deviceCounts(devices.size());
        for (int i = 0; i < devices.size(); i++) {
            deviceCounts.pushBack(0);
        }
        Vector<int> touched;
        for (int u = 0; u < users.size(); u++) {
            int left = userOffsets[u];
            uint64_t distinct = 0;
            touched.clear();
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& cur = recs[userRecordIndices[p]];
                if (cur.eventTypeTag != LOGIN) {
                    continue;
                }
                if (deviceCounts[cur.deviceID] == 0) {
                    distinct++;
                    touched.pushBack(cur.deviceID);
                }
                deviceCounts[cur.deviceID]++;
                while (left <= p) {
                    const DataRecords& old = recs[userRecordIndices[left]];
                    if (old.timestamp + params.windowSec >= cur.timestamp) {
                        break;
                    }
                    if (old.eventTypeTag == LOGIN) {
                        deviceCounts[old.deviceID]--;
                        if (deviceCounts[old.deviceID] == 0) {
                            distinct--;
                        }
                    }
                    left++;
                }
                if (distinct > params.n) {
                    appendAnomaly(results, maxResults, total, "A1", "high", cur, distinct,
                                  "User logged in from too many distinct devices in window", fullOutput);
                }
            }
            for (int i = 0; i < touched.size(); i++) {
                deviceCounts[touched[i]] = 0;
            }
        }
        return total;
    }

    if (type == "A2" || type == "B1") {
        for (int u = 0; u < users.size(); u++) {
            uint64_t failed = 0;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                if (rec.eventTypeTag == FAILED_LOGIN) {
                    failed += rec.Count;
                    if (type == "A2" && failed > params.n) {
                        appendAnomaly(results, maxResults, total, "A2", "medium", rec, failed,
                                      "Consecutive failed login threshold exceeded", fullOutput);
                    }
                } else if (rec.eventTypeTag == LOGIN) {
                    if (type == "B1" && failed >= params.n) {
                        appendAnomaly(results, maxResults, total, "B1", "critical", rec, failed,
                                      "Failed login chain ended with successful login", fullOutput);
                    }
                    failed = 0;
                } else if (rec.eventTypeTag != TOKEN_REFRESH) {
                    failed = 0;
                }
            }
        }
        return total;
    }

    if (type == "A3") {
        Vector<int> resourceCounts(resources.size());
        for (int i = 0; i < resources.size(); i++) {
            resourceCounts.pushBack(0);
        }
        Vector<int> touched;
        for (int d = 0; d < devices.size(); d++) {
            int left = deviceOffsets[d];
            uint64_t distinct = 0;
            touched.clear();
            for (int p = deviceOffsets[d]; p < deviceOffsets[d + 1]; p++) {
                const DataRecords& cur = recs[deviceRecordIndices[p]];
                bool accessEvent = cur.eventTypeTag == ACCESS || cur.eventTypeTag == DOWNLOAD ||
                                   cur.eventTypeTag == OPEN_APP || cur.eventTypeTag == ADMIN_ACTION;
                if (!accessEvent) {
                    continue;
                }
                if (resourceCounts[cur.resourceID] == 0) {
                    distinct++;
                    touched.pushBack(cur.resourceID);
                }
                resourceCounts[cur.resourceID]++;
                while (left <= p) {
                    const DataRecords& old = recs[deviceRecordIndices[left]];
                    if (old.timestamp + params.windowSec >= cur.timestamp) {
                        break;
                    }
                    bool oldAccess = old.eventTypeTag == ACCESS || old.eventTypeTag == DOWNLOAD ||
                                     old.eventTypeTag == OPEN_APP || old.eventTypeTag == ADMIN_ACTION;
                    if (oldAccess) {
                        resourceCounts[old.resourceID]--;
                        if (resourceCounts[old.resourceID] == 0) {
                            distinct--;
                        }
                    }
                    left++;
                }
                if (distinct > params.n) {
                    appendAnomaly(results, maxResults, total, "A3", "high", cur, distinct,
                                  "Device accessed too many distinct resources in window", fullOutput);
                }
            }
            for (int i = 0; i < touched.size(); i++) {
                resourceCounts[touched[i]] = 0;
            }
        }
        return total;
    }

    if (type == "A4") {
        for (int i = 0; i < recs.size(); i++) {
            const DataRecords& rec = recs[i];
            int64_t local = static_cast<int64_t>(rec.timestamp) +
                            static_cast<int64_t>(timezoneOffsetMinutes(rec.locationTag)) * 60;
            int64_t sec = local % 86400;
            if (sec < 0) {
                sec += 86400;
            }
            int hour = static_cast<int>(sec / 3600);
            bool outside = params.startHour <= params.endHour
                               ? (hour < params.startHour || hour >= params.endHour)
                               : (hour < params.startHour && hour >= params.endHour);
            if (outside) {
                appendAnomaly(results, maxResults, total, "A4", "low", rec, hour,
                              "Access outside configured working hours", fullOutput);
            }
        }
        return total;
    }

    if (type == "A5") {
        for (int u = 0; u < users.size(); u++) {
            bool hasPrev = false;
            DataRecords prev;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& cur = recs[userRecordIndices[p]];
                if (hasPrev && prev.locationTag != cur.locationTag) {
                    uint64_t elapsed = cur.timestamp >= prev.timestamp ? cur.timestamp - prev.timestamp : 0;
                    uint64_t required = minTravelSeconds(prev.locationTag, cur.locationTag);
                    if (elapsed < required) {
                        appendAnomaly(results, maxResults, total, "A5", "critical", cur, elapsed,
                                      "Country change faster than minimum travel time", fullOutput);
                    }
                }
                prev = cur;
                hasPrev = true;
            }
        }
        return total;
    }

    if (type == "A6") {
        for (int u = 0; u < users.size(); u++) {
            uint64_t currentDay = 0;
            uint64_t changes = 0;
            location prevLoc = UNKNOWN_LOCATION;
            bool started = false;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                uint64_t day = rec.timestamp / 86400ULL;
                if (!started || day != currentDay) {
                    currentDay = day;
                    changes = 0;
                    prevLoc = rec.locationTag;
                    started = true;
                    continue;
                }
                if (rec.locationTag != prevLoc) {
                    changes++;
                    prevLoc = rec.locationTag;
                    if (changes > params.n) {
                        appendAnomaly(results, maxResults, total, "A6", "medium", rec, changes,
                                      "Location changed too many times in one day", fullOutput);
                    }
                }
            }
        }
        return total;
    }

    if (type == "A7") {
        for (int u = 0; u < users.size(); u++) {
            bool active = false;
            DataRecords loginRec;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                if (rec.eventTypeTag == LOGIN) {
                    active = true;
                    loginRec = rec;
                } else if (rec.eventTypeTag == LOGOUT && active) {
                    uint64_t duration = rec.timestamp >= loginRec.timestamp ? rec.timestamp - loginRec.timestamp : 0;
                    if (duration > params.sessionSec) {
                        appendAnomaly(results, maxResults, total, "A7", "medium", rec, duration,
                                      "Session duration exceeded threshold", fullOutput);
                    }
                    active = false;
                }
            }
        }
        return total;
    }

    if (type == "A8") {
        for (int u = 0; u < users.size(); u++) {
            int left = userOffsets[u];
            uint64_t loginCount = 0;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& cur = recs[userRecordIndices[p]];
                if (cur.eventTypeTag != LOGIN) {
                    continue;
                }
                loginCount += cur.Count;
                while (left <= p) {
                    const DataRecords& old = recs[userRecordIndices[left]];
                    if (old.timestamp + params.windowSec >= cur.timestamp) {
                        break;
                    }
                    if (old.eventTypeTag == LOGIN && loginCount >= old.Count) {
                        loginCount -= old.Count;
                    }
                    left++;
                }
                if (loginCount > params.n) {
                    appendAnomaly(results, maxResults, total, "A8", "medium", cur, loginCount,
                                  "Too many login sessions in time window", fullOutput);
                }
            }
        }
        return total;
    }

    if (type == "A9") {
        for (int u = 0; u < users.size(); u++) {
            bool inSession = false;
            bool afterAdmin = false;
            uint64_t downloads = 0;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                if (rec.eventTypeTag == LOGIN) {
                    inSession = true;
                    afterAdmin = false;
                    downloads = 0;
                } else if (rec.eventTypeTag == LOGOUT) {
                    inSession = false;
                    afterAdmin = false;
                    downloads = 0;
                } else if (inSession && rec.eventTypeTag == ADMIN_ACTION) {
                    afterAdmin = true;
                    downloads = 0;
                } else if (inSession && afterAdmin && rec.eventTypeTag == DOWNLOAD) {
                    downloads += rec.Count;
                    if (downloads >= params.n) {
                        appendAnomaly(results, maxResults, total, "A9", "critical", rec, downloads,
                                      "Admin action followed by consecutive downloads", fullOutput);
                    }
                } else if (afterAdmin && rec.eventTypeTag != TOKEN_REFRESH) {
                    downloads = 0;
                }
            }
        }
        return total;
    }

    if (type == "B2") {
        for (int u = 0; u < users.size(); u++) {
            int begin = userOffsets[u];
            int end = userOffsets[u + 1];
            for (int p = begin + 1; p < end; p++) {
                const DataRecords& prev = recs[userRecordIndices[p - 1]];
                const DataRecords& cur = recs[userRecordIndices[p]];
                if (cur.timestamp < prev.timestamp + params.silenceSec) {
                    continue;
                }
                uint64_t count = 0;
                for (int q = p; q < end; q++) {
                    const DataRecords& burst = recs[userRecordIndices[q]];
                    if (burst.timestamp > cur.timestamp + params.burstSec) {
                        break;
                    }
                    count += burst.Count;
                    if (count > params.n) {
                        appendAnomaly(results, maxResults, total, "B2", "high", burst, count,
                                      "Long silence followed by burst activity", fullOutput);
                        break;
                    }
                }
            }
        }
        return total;
    }

    return total;
}
