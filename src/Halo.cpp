#include "Halo.h"

#include <windows.h>

#include <chrono>
#include <array>
#include <cmath>
#include <ctime>
#include <cstring>
#include <deque>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

namespace {
constexpr uint32_t HALO_BINARY_MAGIC = 0x48414C4F;
constexpr uint32_t HALO_BINARY_VERSION = 3;
constexpr double PI = 3.14159265358979323846;

struct LegacyDataRecordsV2 {
    int userID = -1;
    int deviceID = -1;
    int appID = -1;
    int resourceID = -1;
    int eventTypeTag = 0;
    int locationTag = 0;
    uint64_t timestamp = 0;
    uint64_t Count = 1;
};

static_assert(sizeof(LegacyDataRecordsV2) == 40,
              "LegacyDataRecordsV2 layout must match version 2 cache");

string jsonEscapeText(const string& value) {
    string escaped;
    escaped.reserve(value.size() + 16);
    for (unsigned char c : value) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c < 0x20) {
                    const char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[(c >> 4) & 0x0F];
                    escaped += hex[c & 0x0F];
                } else {
                    escaped += static_cast<char>(c);
                }
        }
    }
    return escaped;
}

string formatUtcEpochText(uint64_t epochSeconds) {
    std::time_t tt = static_cast<std::time_t>(epochSeconds);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

class BinaryMappedReader {
   private:
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    HANDLE mappingHandle = NULL;
    const unsigned char* begin = nullptr;
    const unsigned char* cursor = nullptr;
    const unsigned char* end = nullptr;

   public:
    explicit BinaryMappedReader(const string& filename) {
        fileHandle = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fileHandle == INVALID_HANDLE_VALUE) {
            return;
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(fileHandle, &size) || size.QuadPart < 0 ||
            static_cast<unsigned long long>(size.QuadPart) >
                static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
            CloseHandle(fileHandle);
            fileHandle = INVALID_HANDLE_VALUE;
            return;
        }

        mappingHandle = CreateFileMappingA(fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
        if (mappingHandle == NULL) {
            CloseHandle(fileHandle);
            fileHandle = INVALID_HANDLE_VALUE;
            return;
        }

        begin = static_cast<const unsigned char*>(
            MapViewOfFile(mappingHandle, FILE_MAP_READ, 0, 0, 0));
        if (begin == nullptr) {
            CloseHandle(mappingHandle);
            mappingHandle = NULL;
            CloseHandle(fileHandle);
            fileHandle = INVALID_HANDLE_VALUE;
            return;
        }

        cursor = begin;
        end = begin + static_cast<size_t>(size.QuadPart);
    }

    ~BinaryMappedReader() {
        if (begin != nullptr) {
            UnmapViewOfFile(begin);
        }
        if (mappingHandle != NULL) {
            CloseHandle(mappingHandle);
        }
        if (fileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(fileHandle);
        }
    }

    bool isOpen() const {
        return begin != nullptr;
    }

    template <typename T>
    bool readPod(T& out) {
        return readBytes(&out, sizeof(T));
    }

    bool readBytes(void* dst, size_t bytes) {
        if (bytes == 0) {
            return true;
        }
        if (static_cast<size_t>(end - cursor) < bytes) {
            return false;
        }
        std::memcpy(dst, cursor, bytes);
        cursor += bytes;
        return true;
    }
};

class BinaryFileReader {
   private:
    HANDLE fileHandle = INVALID_HANDLE_VALUE;

   public:
    explicit BinaryFileReader(const string& filename) {
        fileHandle = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    ~BinaryFileReader() {
        if (fileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(fileHandle);
        }
    }

    bool isOpen() const {
        return fileHandle != INVALID_HANDLE_VALUE;
    }

    template <typename T>
    bool readPod(T& out) {
        DWORD read = 0;
        return ReadFile(fileHandle, &out, sizeof(T), &read, NULL) &&
               read == sizeof(T);
    }

    bool readBytes(void* dst, size_t bytes) {
        char* ptr = reinterpret_cast<char*>(dst);
        DWORD read = 0;
        while (bytes > 0) {
            DWORD chunk = bytes > 64ULL * 1024ULL * 1024ULL ? 64U * 1024U * 1024U
                                                            : static_cast<DWORD>(bytes);
            if (!ReadFile(fileHandle, ptr, chunk, &read, NULL) || read != chunk) {
                return false;
            }
            ptr += read;
            bytes -= read;
        }
        return true;
    }
};

template <typename Getter>
void buildSingleIndex(const Vector<DataRecords>& recs, size_t recCount,
                      size_t bucketCount, Vector<int>& offsets,
                      Vector<int>& recordIndices, Getter getter) {
    offsets.setSize(bucketCount + 1);
    for (size_t i = 0; i <= bucketCount; i++) {
        offsets[i] = 0;
    }

    for (size_t i = 0; i < recCount; i++) {
        offsets[static_cast<size_t>(getter(recs[i])) + 1]++;
    }

    for (size_t i = 1; i <= bucketCount; i++) {
        offsets[i] += offsets[i - 1];
    }

    recordIndices.setSize(recCount);
    Vector<int> cursor;
    cursor.setSize(bucketCount + 1);
    std::memcpy(cursor.rawData(), offsets.rawData(),
                (bucketCount + 1) * sizeof(int));
    for (size_t i = 0; i < recCount; i++) {
        const DataRecords& rec = recs[i];
        recordIndices[cursor[static_cast<size_t>(getter(rec))]++] =
            static_cast<int>(i);
    }
}
}  // namespace

Halo::Halo() : users("U"), devices("D"), apps("APP"), resources("R") {}

Halo::~Halo() {
    clearGraphNodes();
}

void Halo::clearGraphNodes() {
    for (size_t i = 0; i < userNodes.size(); i++) {
        delete userNodes[i];
    }
    for (size_t i = 0; i < deviceNodes.size(); i++) {
        delete deviceNodes[i];
    }
    for (size_t i = 0; i < appNodes.size(); i++) {
        delete appNodes[i];
    }
    for (size_t i = 0; i < resourceNodes.size(); i++) {
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
    hybridSort(results, [](const QueryRow& a, const QueryRow& b) {
        return a.timestamp < b.timestamp;
    });
}

void Halo::buildIndexes() {
    clearIndexes();

    size_t recCount = records.size();
    size_t userCount = users.size();
    size_t deviceCount = devices.size();
    size_t resourceCount = resources.size();

    const Vector<DataRecords>& recs = records.getRecords();
    constexpr size_t PARALLEL_INDEX_MIN = 2000000;
    unsigned int hw = std::thread::hardware_concurrency();
    bool useParallel = recCount >= PARALLEL_INDEX_MIN && hw >= 4;

    auto buildUser = [&]() {
        buildSingleIndex(recs, recCount, userCount, userOffsets,
                         userRecordIndices,
                         [](const DataRecords& rec) { return rec.userID; });
    };
    auto buildDevice = [&]() {
        buildSingleIndex(recs, recCount, deviceCount, deviceOffsets,
                         deviceRecordIndices,
                         [](const DataRecords& rec) { return rec.deviceID; });
    };
    auto buildResource = [&]() {
        buildSingleIndex(recs, recCount, resourceCount, resourceOffsets,
                         resourceRecordIndices,
                         [](const DataRecords& rec) { return rec.resourceID; });
    };

    if (useParallel) {
        std::thread t1(buildUser);
        std::thread t2(buildDevice);
        std::thread t3(buildResource);
        t1.join();
        t2.join();
        t3.join();
    } else {
        buildUser();
        buildDevice();
        buildResource();
    }

    indexesBuilt = true;
}

void Halo::processRecord(const DataRecords& newRecord, std::string_view userId,
                         std::string_view deviceId, std::string_view appId,
                         std::string_view resourceId) {
    int uIdx = users.getOrAdd(userId);
    int dIdx = devices.getOrAdd(deviceId);
    int aIdx = apps.getOrAdd(appId);
    int rIdx = resources.getOrAdd(resourceId);

    if (buildGraph) {
        while (userNodes.size() <= static_cast<size_t>(uIdx)) {
            userNodes.pushBack(new User(static_cast<int>(userNodes.size())));
        }
        while (deviceNodes.size() <= static_cast<size_t>(dIdx)) {
            deviceNodes.pushBack(new Device(static_cast<int>(deviceNodes.size())));
        }
        while (appNodes.size() <= static_cast<size_t>(aIdx)) {
            appNodes.pushBack(new App(static_cast<int>(appNodes.size())));
        }
        while (resourceNodes.size() <= static_cast<size_t>(rIdx)) {
            resourceNodes.pushBack(new Resource(static_cast<int>(resourceNodes.size())));
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
    finalizeMetrics = FinalizeMetrics();
}

void Halo::reserveForImport(size_t expectedRecords, size_t expectedUsers,
                            size_t expectedDevices, size_t expectedApps,
                            size_t expectedResources) {
    records.reserve(expectedRecords);
    if (expectedUsers > 0) {
        users.reserve(expectedUsers);
        if (buildGraph) {
            userNodes.reserve(expectedUsers);
        }
    }
    if (expectedDevices > 0) {
        devices.reserve(expectedDevices);
        if (buildGraph) {
            deviceNodes.reserve(expectedDevices);
        }
    }
    if (expectedApps > 0) {
        apps.reserve(expectedApps);
        if (buildGraph) {
            appNodes.reserve(expectedApps);
        }
    }
    if (expectedResources > 0) {
        resources.reserve(expectedResources);
        if (buildGraph) {
            resourceNodes.reserve(expectedResources);
        }
    }
}

void Halo::finalizeLoading() {
    using clock = std::chrono::steady_clock;
    finalizeMetrics = FinalizeMetrics();
    auto finalizeStart = clock::now();

    auto sortStart = clock::now();
    records.sortRecords(sortMode, &finalizeMetrics.sortAlgorithm);
    finalizeMetrics.sortTimeMs =
        std::chrono::duration<double, std::milli>(clock::now() - sortStart).count();

    auto dedupStart = clock::now();
    uint64_t merged = records.removeDuplicates();
    finalizeMetrics.deduplicateTimeMs =
        std::chrono::duration<double, std::milli>(clock::now() - dedupStart).count();

    auto shrinkIfWasteful = [](IdTable& table) {
        const Vector<string>& names = table.getNames();
        size_t size = names.size();
        size_t capacity = names.getCapacity();
        size_t slack = capacity > size ? capacity - size : 0;
        size_t minSlack = size / 8 + 1024;
        if (slack > minSlack) {
            table.shrinkToFit();
        }
    };

    shrinkIfWasteful(users);
    shrinkIfWasteful(devices);
    shrinkIfWasteful(apps);
    shrinkIfWasteful(resources);
    setDuplicateMerged(merged);

    auto indexStart = clock::now();
    buildIndexes();
    finalizeMetrics.indexBuildTimeMs =
        std::chrono::duration<double, std::milli>(clock::now() - indexStart).count();
    finalizeMetrics.finalizeTimeMs =
        std::chrono::duration<double, std::milli>(clock::now() - finalizeStart).count();
}

static bool writeRawAll(HANDLE hFile, const void* data, unsigned long long bytes) {
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

class BufferedBinaryWriter {
   private:
    HANDLE hFile;
    char* buffer;
    size_t capacity;
    size_t used;
    bool ok;

   public:
    explicit BufferedBinaryWriter(HANDLE fileHandle, size_t bufferBytes = 4ULL * 1024ULL * 1024ULL) {
        hFile = fileHandle;
        capacity = bufferBytes;
        used = 0;
        ok = true;
        buffer = capacity > 0 ? new char[capacity] : nullptr;
    }

    ~BufferedBinaryWriter() {
        delete[] buffer;
    }

    bool flush() {
        if (!ok) {
            return false;
        }
        if (used == 0) {
            return true;
        }
        ok = writeRawAll(hFile, buffer, static_cast<unsigned long long>(used));
        used = 0;
        return ok;
    }

    bool writeBytes(const void* data, size_t bytes) {
        if (!ok) {
            return false;
        }
        if (bytes == 0) {
            return true;
        }
        if (bytes > capacity) {
            if (!flush()) {
                return false;
            }
            ok = writeRawAll(hFile, data, static_cast<unsigned long long>(bytes));
            return ok;
        }
        if (used + bytes > capacity) {
            if (!flush()) {
                return false;
            }
        }
        std::memcpy(buffer + used, data, bytes);
        used += bytes;
        return true;
    }

    template <typename T>
    bool writePod(const T& value) {
        return writeBytes(&value, sizeof(value));
    }
};

bool Halo::saveToBinary(const string& filename) const {
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    BufferedBinaryWriter writer(hFile);
    uint32_t magic = HALO_BINARY_MAGIC;
    uint32_t version = HALO_BINARY_VERSION;
    if (!writer.writePod(magic) || !writer.writePod(version)) {
        CloseHandle(hFile);
        return false;
    }

    auto writeNames = [&](const IdTable& table) -> bool {
        const Vector<string>& names = table.getNames();
        if (names.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        int size = static_cast<int>(names.size());
        if (!writer.writePod(size)) {
            return false;
        }
        for (int i = 0; i < size; i++) {
            const string& name = names[i];
            if (name.length() > static_cast<size_t>(std::numeric_limits<int>::max())) {
                return false;
            }
            int len = static_cast<int>(name.length());
            if (!writer.writePod(len)) {
                return false;
            }
            if (len > 0 && !writer.writeBytes(name.c_str(), static_cast<size_t>(len))) {
                return false;
            }
        }
        return true;
    };

    bool ok = writeNames(users) && writeNames(devices) && writeNames(apps) && writeNames(resources) &&
              writer.writePod(totalReadCount) &&
              writer.writePod(skippedRowCount) &&
              writer.writePod(replacedFieldRowCount) &&
              writer.writePod(duplicateMergedCount);

    if (records.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        CloseHandle(hFile);
        return false;
    }
    int recSize = static_cast<int>(records.size());
    ok = ok && writer.writePod(recSize);
    if (ok && recSize > 0) {
        const Vector<DataRecords>& recs = records.getRecords();
        size_t recordBytes = static_cast<size_t>(recSize) * sizeof(DataRecords);
        ok = writer.writeBytes(recs.rawData(), recordBytes);
    }
    ok = ok && writer.flush();

    CloseHandle(hFile);
    return ok;
}

bool Halo::loadFromBinary(const string& filename, bool rebuildGraph) {
    auto loadFromReader = [&](auto& reader) -> bool {
        uint32_t magic = 0;
        uint32_t version = 0;
        if (!reader.readPod(magic) || magic != HALO_BINARY_MAGIC ||
            !reader.readPod(version) ||
            (version != 2 && version != HALO_BINARY_VERSION)) {
            return false;
        }
        const bool legacyRecords = (version == 2);

        clear();
        auto failAfterClear = [&]() {
            clear();
            return false;
        };

        auto readNames = [&](IdTable& table) -> bool {
            int size = 0;
            if (!reader.readPod(size) || size < 0) {
                return false;
            }
            Vector<string> names;
            names.reserve(static_cast<size_t>(size));
            for (int i = 0; i < size; i++) {
                int len = 0;
                if (!reader.readPod(len) || len < 0) {
                    return false;
                }
                string name;
                if (len > 0) {
                    name.resize(static_cast<size_t>(len));
                    if (!reader.readBytes(name.data(), static_cast<size_t>(len))) {
                        return false;
                    }
                }
                names.pushBack(std::move(name));
            }
            table.setNames(std::move(names));
            return true;
        };

        if (!readNames(users) || !readNames(devices) || !readNames(apps) ||
            !readNames(resources) || !reader.readPod(totalReadCount) ||
            !reader.readPod(skippedRowCount) ||
            !reader.readPod(replacedFieldRowCount) ||
            !reader.readPod(duplicateMergedCount)) {
            return failAfterClear();
        }

        int recSize = 0;
        if (!reader.readPod(recSize) || recSize < 0) {
            return failAfterClear();
        }

        Vector<DataRecords> recs;
        recs.setSize(static_cast<size_t>(recSize));
        if (legacyRecords) {
            for (int i = 0; i < recSize; i++) {
                LegacyDataRecordsV2 legacy;
                if (!reader.readPod(legacy)) {
                    return failAfterClear();
                }
                DataRecords rec;
                rec.userID = legacy.userID;
                rec.deviceID = legacy.deviceID;
                rec.appID = legacy.appID;
                rec.resourceID = legacy.resourceID;
                rec.Count = legacy.Count > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
                                ? std::numeric_limits<uint32_t>::max()
                                : static_cast<uint32_t>(legacy.Count);
                rec.eventTypeTag = static_cast<event_Type>(legacy.eventTypeTag);
                rec.locationTag = static_cast<location>(legacy.locationTag);
                rec.timestamp = legacy.timestamp;
                recs[static_cast<size_t>(i)] = rec;
            }
        } else if (recSize > 0 &&
                   !reader.readBytes(recs.rawData(),
                                     static_cast<size_t>(recSize) * sizeof(DataRecords))) {
            return failAfterClear();
        }
        records.setRecords(std::move(recs));

        finalizeMetrics = FinalizeMetrics();
        auto indexStart = std::chrono::steady_clock::now();
        buildIndexes();
        finalizeMetrics.indexBuildTimeMs = std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - indexStart)
                                              .count();
        finalizeMetrics.finalizeTimeMs = finalizeMetrics.indexBuildTimeMs;

        if (!rebuildGraph) {
            return true;
        }

        size_t uSize = users.size();
        size_t dSize = devices.size();
        size_t aSize = apps.size();
        size_t rSize = resources.size();
        userNodes.reserve(uSize);
        deviceNodes.reserve(dSize);
        appNodes.reserve(aSize);
        resourceNodes.reserve(rSize);
        for (size_t i = 0; i < uSize; i++) {
            userNodes.pushBack(new User(static_cast<int>(i)));
        }
        for (size_t i = 0; i < dSize; i++) {
            deviceNodes.pushBack(new Device(static_cast<int>(i)));
        }
        for (size_t i = 0; i < aSize; i++) {
            appNodes.pushBack(new App(static_cast<int>(i)));
        }
        for (size_t i = 0; i < rSize; i++) {
            resourceNodes.pushBack(new Resource(static_cast<int>(i)));
        }

        const Vector<DataRecords>& recsList = records.getRecords();
        for (size_t i = 0; i < recsList.size(); i++) {
            const DataRecords& rec = recsList[i];
            userNodes[rec.userID]->connectedDevices.pushBackUnique(rec.deviceID);
            deviceNodes[rec.deviceID]->connectedUsers.pushBackUnique(rec.userID);
            deviceNodes[rec.deviceID]->connectedApps.pushBackUnique(rec.appID);
            appNodes[rec.appID]->connectedDevices.pushBackUnique(rec.deviceID);
            appNodes[rec.appID]->connectedResources.pushBackUnique(rec.resourceID);
            resourceNodes[rec.resourceID]->connectedApps.pushBackUnique(rec.appID);
        }

        return true;
    };

    {
        BinaryMappedReader mappedReader(filename);
        if (mappedReader.isOpen()) {
            if (loadFromReader(mappedReader)) {
                return true;
            }
            return false;
        }
    }

    BinaryFileReader fileReader(filename);
    if (!fileReader.isOpen()) {
        return false;
    }
    return loadFromReader(fileReader);
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
    size_t totalResources = resources.size();
    if (totalResources == 0) {
        return;
    }

    Vector<uint64_t> counts(totalResources);
    Vector<uint64_t> lastTimestamps(totalResources);
    for (size_t i = 0; i < totalResources; i++) {
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

    size_t topK = totalResources < 10 ? totalResources : 10;
    Vector<bool> used(totalResources);
    for (size_t i = 0; i < totalResources; i++) {
        used.pushBack(false);
    }

    for (size_t t = 0; t < topK; t++) {
        size_t maxIdx = 0;
        bool found = false;
        uint64_t maxCount = 0;
        uint64_t maxLastTimestamp = 0;
        for (size_t i = 0; i < totalResources; i++) {
            if (used[i]) {
                continue;
            }
            if (counts[i] > maxCount ||
                (counts[i] == maxCount && counts[i] > 0 && lastTimestamps[i] > maxLastTimestamp)) {
                maxCount = counts[i];
                maxIdx = i;
                maxLastTimestamp = lastTimestamps[i];
                found = true;
            }
        }
        if (!found || maxCount == 0) {
            break;
        }
        used[maxIdx] = true;
        resourceNames.pushBack(resources.getName(static_cast<int>(maxIdx)));
        accessCounts.pushBack(maxCount);
    }
}

void Halo::appendAnomaly(Vector<AnomalyResult>& results, int maxResults, uint64_t& total,
                         const string& type, const string& severity, const DataRecords& rec,
                         uint64_t count, const string& detail,
                         std::ostream* fullOutput) const {
    Vector<DataRecords> evidence;
    evidence.pushBack(rec);
    appendAnomalyGroup(results, maxResults, total, type, severity, rec, count,
                       detail, evidence, fullOutput);
}

AnomalyEvidence Halo::makeEvidence(const DataRecords& rec) const {
    AnomalyEvidence evidence;
    evidence.eventType = eventToString(rec.eventTypeTag);
    evidence.user = users.getName(rec.userID);
    evidence.device = devices.getName(rec.deviceID);
    evidence.app = apps.getName(rec.appID);
    evidence.resource = resources.getName(rec.resourceID);
    evidence.location = locationToString(rec.locationTag);
    evidence.timestamp = rec.timestamp;
    evidence.count = rec.Count;
    return evidence;
}

void Halo::appendAnomalyGroup(Vector<AnomalyResult>& results, int maxResults, uint64_t& total,
                              const string& type, const string& severity,
                              const DataRecords& anchor, uint64_t count,
                              const string& detail, const Vector<DataRecords>& evidence,
                              std::ostream* fullOutput) const {
    total++;
    uint64_t startTimestamp = anchor.timestamp;
    uint64_t endTimestamp = anchor.timestamp;
    for (size_t i = 0; i < evidence.size(); i++) {
        if (evidence[i].timestamp < startTimestamp) {
            startTimestamp = evidence[i].timestamp;
        }
        if (evidence[i].timestamp > endTimestamp) {
            endTimestamp = evidence[i].timestamp;
        }
    }

    if (fullOutput != nullptr) {
        *fullOutput << "{\"type\":\"" << jsonEscapeText(type)
                    << "\",\"severity\":\"" << jsonEscapeText(severity)
                    << "\",\"user\":\"" << jsonEscapeText(users.getName(anchor.userID))
                    << "\",\"timestamp\":" << startTimestamp
                    << ",\"timestampUtc\":\"" << formatUtcEpochText(startTimestamp) << "\""
                    << ",\"endTimestamp\":" << endTimestamp
                    << ",\"endTimestampUtc\":\"" << formatUtcEpochText(endTimestamp) << "\""
                    << ",\"count\":" << count
                    << ",\"reason\":\"" << jsonEscapeText(detail)
                    << "\",\"records\":[";
        for (size_t i = 0; i < evidence.size(); i++) {
            const AnomalyEvidence item = makeEvidence(evidence[i]);
            if (i > 0) {
                *fullOutput << ",";
            }
            *fullOutput << "{\"eventType\":\"" << jsonEscapeText(item.eventType)
                        << "\",\"user\":\"" << jsonEscapeText(item.user)
                        << "\",\"device\":\"" << jsonEscapeText(item.device)
                        << "\",\"app\":\"" << jsonEscapeText(item.app)
                        << "\",\"resource\":\"" << jsonEscapeText(item.resource)
                        << "\",\"location\":\"" << jsonEscapeText(item.location)
                        << "\",\"timestamp\":" << item.timestamp
                        << ",\"timestampUtc\":\"" << formatUtcEpochText(item.timestamp) << "\""
                        << ",\"count\":" << item.count << "}";
        }
        *fullOutput << "]}\n";
    }
    if (maxResults >= 0 && results.size() >= static_cast<size_t>(maxResults)) {
        return;
    }
    AnomalyResult row;
    row.type = type;
    row.severity = severity;
    row.user = users.getName(anchor.userID);
    row.device = devices.getName(anchor.deviceID);
    row.app = apps.getName(anchor.appID);
    row.resource = resources.getName(anchor.resourceID);
    row.location = locationToString(anchor.locationTag);
    row.timestamp = startTimestamp;
    row.endTimestamp = endTimestamp;
    row.count = count;
    row.detail = detail;
    for (size_t i = 0; i < evidence.size(); i++) {
        row.records.pushBack(makeEvidence(evidence[i]));
    }
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

TravelEstimate Halo::estimateTravel(location from, location to) const {
    TravelEstimate estimate;
    if (from == to) {
        estimate.valid = true;
        return estimate;
    }

    const bool sharesBorder =
        countryPair(from, to, LOC_US, LOC_CA) ||
        countryPair(from, to, LOC_VN, LOC_CN) ||
        countryPair(from, to, LOC_CN, LOC_IN) ||
        countryPair(from, to, LOC_CN, LOC_RU) ||
        countryPair(from, to, LOC_DE, LOC_FR);
    if (sharesBorder) {
        estimate.valid = true;
        estimate.sharesBorder = true;
        return estimate;
    }

    struct Coordinate {
        double latitude;
        double longitude;
        bool valid;
    };
    static constexpr std::array<Coordinate, static_cast<size_t>(UNKNOWN_LOCATION) + 1> coordinates = {{
        {39.8283, -98.5795, true}, {14.0583, 108.2772, true},
        {36.2048, 138.2529, true}, {35.9078, 127.7669, true},
        {1.3521, 103.8198, true}, {35.8617, 104.1954, true},
        {51.1657, 10.4515, true}, {46.2276, 2.2137, true},
        {55.3781, -3.4360, true}, {-25.2744, 133.7751, true},
        {56.1304, -106.3468, true}, {20.5937, 78.9629, true},
        {-14.2350, -51.9253, true}, {61.5240, 105.3188, true},
        {15.8700, 100.9925, true}, {0.0, 0.0, false}
    }};

    const size_t fromIndex = static_cast<size_t>(from);
    const size_t toIndex = static_cast<size_t>(to);
    if (fromIndex >= coordinates.size() || toIndex >= coordinates.size() ||
        !coordinates[fromIndex].valid || !coordinates[toIndex].valid) {
        return estimate;
    }

    const auto radians = [](double degrees) { return degrees * PI / 180.0; };
    const double lat1 = radians(coordinates[fromIndex].latitude);
    const double lat2 = radians(coordinates[toIndex].latitude);
    const double deltaLat = lat2 - lat1;
    const double deltaLon = radians(coordinates[toIndex].longitude -
                                    coordinates[fromIndex].longitude);
    const double a = std::sin(deltaLat / 2.0) * std::sin(deltaLat / 2.0) +
                     std::cos(lat1) * std::cos(lat2) *
                     std::sin(deltaLon / 2.0) * std::sin(deltaLon / 2.0);
    const double centralAngle = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    estimate.valid = true;
    estimate.distanceKm = 6371.0 * centralAngle;
    const double flightHours = estimate.distanceKm / 900.0;
    const double airportOverheadHours = 2.0;
    estimate.minimumSeconds = static_cast<uint64_t>(
        std::ceil((flightHours + airportOverheadHours) * 3600.0));
    return estimate;
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
        for (size_t i = 0; i < devices.size(); i++) {
            deviceCounts.pushBack(0);
        }
        Vector<int> touched;
        for (size_t u = 0; u < users.size(); u++) {
            int left = userOffsets[u];
            uint64_t distinct = 0;
            bool incidentReported = false;
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
                if (distinct <= params.n) {
                    incidentReported = false;
                }
                if (!incidentReported && distinct > params.n) {
                    Vector<DataRecords> evidence;
                    for (int q = left; q <= p; q++) {
                        const DataRecords& item = recs[userRecordIndices[q]];
                        if (item.eventTypeTag == LOGIN) {
                            evidence.pushBack(item);
                        }
                    }
                    std::ostringstream detail;
                    detail << distinct << " thiết bị đăng nhập phân biệt trong "
                           << params.windowSec << " giây, vượt ngưỡng N=" << params.n;
                    appendAnomalyGroup(results, maxResults, total, "A1", "high", cur,
                                       distinct, detail.str(), evidence, fullOutput);
                    incidentReported = true;
                }
            }
            for (size_t i = 0; i < touched.size(); i++) {
                deviceCounts[touched[i]] = 0;
            }
        }
        return total;
    }

    if (type == "A2" || type == "B1") {
        for (size_t u = 0; u < users.size(); u++) {
            uint64_t failed = 0;
            bool incidentReported = false;
            Vector<DataRecords> failedRecords;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                if (rec.eventTypeTag == FAILED_LOGIN) {
                    failed += rec.Count;
                    failedRecords.pushBack(rec);
                    if (type == "A2" && !incidentReported && failed >= params.n) {
                        std::ostringstream detail;
                        detail << failed << " lần đăng nhập thất bại liên tục (ngưỡng N="
                               << params.n << ") nên bất thường";
                        appendAnomalyGroup(results, maxResults, total, "A2", "medium", rec,
                                           failed, detail.str(), failedRecords, fullOutput);
                        incidentReported = true;
                    }
                } else if (rec.eventTypeTag == LOGIN) {
                    if (type == "B1" && failed >= params.n) {
                        Vector<DataRecords> evidence = failedRecords;
                        evidence.pushBack(rec);
                        std::ostringstream detail;
                        detail << failed << " lần đăng nhập thất bại liên tục, sau đó đăng nhập "
                               << "thành công (ngưỡng N=" << params.n << ")";
                        appendAnomalyGroup(results, maxResults, total, "B1", "critical", rec,
                                           failed, detail.str(), evidence, fullOutput);
                    }
                    failed = 0;
                    incidentReported = false;
                    failedRecords.clear();
                } else if (rec.eventTypeTag != TOKEN_REFRESH) {
                    failed = 0;
                    incidentReported = false;
                    failedRecords.clear();
                }
            }
        }
        return total;
    }

    if (type == "A3") {
        Vector<int> resourceCounts(resources.size());
        for (size_t i = 0; i < resources.size(); i++) {
            resourceCounts.pushBack(0);
        }
        Vector<int> touched;
        for (size_t d = 0; d < devices.size(); d++) {
            int left = deviceOffsets[d];
            uint64_t distinct = 0;
            bool incidentReported = false;
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
                if (distinct <= params.n) {
                    incidentReported = false;
                }
                if (!incidentReported && distinct > params.n) {
                    Vector<DataRecords> evidence;
                    for (int q = left; q <= p; q++) {
                        const DataRecords& item = recs[deviceRecordIndices[q]];
                        const bool relevant = item.eventTypeTag == ACCESS ||
                            item.eventTypeTag == DOWNLOAD || item.eventTypeTag == OPEN_APP ||
                            item.eventTypeTag == ADMIN_ACTION;
                        if (relevant) {
                            evidence.pushBack(item);
                        }
                    }
                    std::ostringstream detail;
                    detail << distinct << " tài nguyên phân biệt được truy cập trong "
                           << params.windowSec << " giây, vượt ngưỡng N=" << params.n;
                    appendAnomalyGroup(results, maxResults, total, "A3", "high", cur,
                                       distinct, detail.str(), evidence, fullOutput);
                    incidentReported = true;
                }
            }
            for (size_t i = 0; i < touched.size(); i++) {
                resourceCounts[touched[i]] = 0;
            }
        }
        return total;
    }

    if (type == "A4") {
        for (size_t i = 0; i < recs.size(); i++) {
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
                std::ostringstream detail;
                detail << "Hoạt động lúc " << hour << ":00 giờ địa phương, ngoài khung "
                       << params.startHour << ":00–" << params.endHour << ":00";
                appendAnomaly(results, maxResults, total, "A4", "low", rec, hour,
                              detail.str(), fullOutput);
            }
        }
        return total;
    }

    if (type == "A5") {
        for (size_t u = 0; u < users.size(); u++) {
            bool hasPrev = false;
            DataRecords prev;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& cur = recs[userRecordIndices[p]];
                if (cur.locationTag == UNKNOWN_LOCATION) {
                    continue;
                }
                if (hasPrev && prev.locationTag != cur.locationTag) {
                    uint64_t elapsed = cur.timestamp >= prev.timestamp ? cur.timestamp - prev.timestamp : 0;
                    const TravelEstimate estimate = estimateTravel(prev.locationTag, cur.locationTag);
                    if (estimate.valid && !estimate.sharesBorder &&
                        elapsed < estimate.minimumSeconds) {
                        Vector<DataRecords> evidence;
                        evidence.pushBack(prev);
                        evidence.pushBack(cur);
                        std::ostringstream detail;
                        detail << std::fixed << std::setprecision(0)
                               << "Không khả thi: di chuyển "
                               << locationToString(prev.locationTag) << " → "
                               << locationToString(cur.locationTag) << " khoảng "
                               << estimate.distanceKm << " km đường chim bay trong "
                               << elapsed << " giây; cần tối thiểu "
                               << estimate.minimumSeconds << " giây nếu bay 900 km/h "
                               << "và cộng 2 giờ thủ tục sân bay";
                        appendAnomalyGroup(results, maxResults, total, "A5", "critical", cur,
                                           elapsed, detail.str(), evidence, fullOutput);
                    }
                }
                prev = cur;
                hasPrev = true;
            }
        }
        return total;
    }

    if (type == "A6") {
        for (size_t u = 0; u < users.size(); u++) {
            uint64_t currentDay = 0;
            uint64_t changes = 0;
            location prevLoc = UNKNOWN_LOCATION;
            bool started = false;
            bool incidentReported = false;
            Vector<DataRecords> dayRecords;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                uint64_t day = rec.timestamp / 86400ULL;
                if (!started || day != currentDay) {
                    currentDay = day;
                    changes = 0;
                    prevLoc = rec.locationTag;
                    started = true;
                    incidentReported = false;
                    dayRecords.clear();
                    dayRecords.pushBack(rec);
                    continue;
                }
                dayRecords.pushBack(rec);
                if (rec.locationTag != UNKNOWN_LOCATION) {
                    if (prevLoc != UNKNOWN_LOCATION && rec.locationTag != prevLoc) {
                        changes++;
                        if (!incidentReported && changes > params.n) {
                        std::ostringstream detail;
                        detail << changes << " lần đổi quốc gia trong cùng ngày UTC, vượt ngưỡng N="
                               << params.n;
                        appendAnomalyGroup(results, maxResults, total, "A6", "medium", rec,
                                           changes, detail.str(), dayRecords, fullOutput);
                        incidentReported = true;
                        }
                    }
                    prevLoc = rec.locationTag;
                }
            }
        }
        return total;
    }

    if (type == "A7") {
        for (size_t u = 0; u < users.size(); u++) {
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
                        Vector<DataRecords> evidence;
                        evidence.pushBack(loginRec);
                        evidence.pushBack(rec);
                        std::ostringstream detail;
                        detail << "Phiên LOGIN → LOGOUT kéo dài " << duration
                               << " giây, vượt ngưỡng " << params.sessionSec << " giây";
                        appendAnomalyGroup(results, maxResults, total, "A7", "medium", rec,
                                           duration, detail.str(), evidence, fullOutput);
                    }
                    active = false;
                }
            }
        }
        return total;
    }

    if (type == "A8") {
        for (size_t u = 0; u < users.size(); u++) {
            std::deque<DataRecords> loginWindow;
            uint64_t loginCount = 0;
            bool incidentReported = false;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& cur = recs[userRecordIndices[p]];
                if (cur.eventTypeTag != LOGIN) {
                    continue;
                }
                loginWindow.push_back(cur);
                loginCount += cur.Count;
                while (!loginWindow.empty()) {
                    const DataRecords& old = loginWindow.front();
                    if (old.timestamp + params.windowSec >= cur.timestamp) {
                        break;
                    }
                    if (loginCount >= old.Count) {
                        loginCount -= old.Count;
                    }
                    loginWindow.pop_front();
                }
                if (loginCount < params.n) {
                    incidentReported = false;
                }
                if (!incidentReported && loginCount >= params.n) {
                    Vector<DataRecords> evidence;
                    for (size_t i = 0; i < loginWindow.size(); i++) {
                        evidence.pushBack(loginWindow[i]);
                    }
                    std::ostringstream detail;
                    detail << loginCount << " lần LOGIN trong " << params.windowSec
                           << " giây (ngưỡng N=" << params.n << ") nên bất thường";
                    appendAnomalyGroup(results, maxResults, total, "A8", "medium", cur,
                                       loginCount, detail.str(), evidence, fullOutput);
                    incidentReported = true;
                }
            }
        }
        return total;
    }

    if (type == "A9") {
        for (size_t u = 0; u < users.size(); u++) {
            bool inSession = false;
            bool afterAdmin = false;
            bool incidentReported = false;
            uint64_t downloads = 0;
            Vector<DataRecords> evidence;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                if (rec.eventTypeTag == LOGIN) {
                    inSession = true;
                    afterAdmin = false;
                    incidentReported = false;
                    downloads = 0;
                    evidence.clear();
                } else if (rec.eventTypeTag == LOGOUT) {
                    inSession = false;
                    afterAdmin = false;
                    incidentReported = false;
                    downloads = 0;
                    evidence.clear();
                } else if (inSession && rec.eventTypeTag == ADMIN_ACTION) {
                    afterAdmin = true;
                    incidentReported = false;
                    downloads = 0;
                    evidence.clear();
                    evidence.pushBack(rec);
                } else if (inSession && afterAdmin && rec.eventTypeTag == DOWNLOAD) {
                    evidence.pushBack(rec);
                    downloads += rec.Count;
                    if (!incidentReported && downloads >= params.n) {
                        std::ostringstream detail;
                        detail << "ADMIN_ACTION tiếp theo bởi " << downloads
                               << " DOWNLOAD liên tục (ngưỡng N=" << params.n << ")";
                        appendAnomalyGroup(results, maxResults, total, "A9", "critical", rec,
                                           downloads, detail.str(), evidence, fullOutput);
                        incidentReported = true;
                    }
                } else if (afterAdmin && rec.eventTypeTag != TOKEN_REFRESH) {
                    downloads = 0;
                    incidentReported = false;
                    evidence.clear();
                }
            }
        }
        return total;
    }

    if (type == "B2") {
        for (size_t u = 0; u < users.size(); u++) {
            int begin = userOffsets[u];
            int end = userOffsets[u + 1];
            for (int p = begin + 1; p < end; p++) {
                const DataRecords& prev = recs[userRecordIndices[p - 1]];
                const DataRecords& cur = recs[userRecordIndices[p]];
                if (cur.timestamp < prev.timestamp + params.silenceSec) {
                    continue;
                }
                uint64_t count = 0;
                Vector<DataRecords> evidence;
                evidence.pushBack(prev);
                for (int q = p; q < end; q++) {
                    const DataRecords& burst = recs[userRecordIndices[q]];
                    if (burst.timestamp > cur.timestamp + params.burstSec) {
                        break;
                    }
                    evidence.pushBack(burst);
                    count += burst.Count;
                    if (count > params.n) {
                        std::ostringstream detail;
                        detail << "Im lặng " << (cur.timestamp - prev.timestamp)
                               << " giây rồi phát sinh " << count << " hoạt động trong "
                               << params.burstSec << " giây (ngưỡng N=" << params.n << ")";
                        appendAnomalyGroup(results, maxResults, total, "B2", "high", burst,
                                           count, detail.str(), evidence, fullOutput);
                        break;
                    }
                }
            }
        }
        return total;
    }

    if (type == "C4") {
        for (size_t u = 0; u < users.size(); u++) {
            std::deque<DataRecords> spacedFailures;
            bool incidentReported = false;
            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                if (rec.eventTypeTag != FAILED_LOGIN) {
                    continue;
                }

                while (!spacedFailures.empty() &&
                       spacedFailures.front().timestamp + params.windowSec < rec.timestamp) {
                    spacedFailures.pop_front();
                }
                if (spacedFailures.size() < params.n) {
                    incidentReported = false;
                }

                if (spacedFailures.empty() ||
                    rec.timestamp >= spacedFailures.back().timestamp + params.minSpacingSec) {
                    spacedFailures.push_back(rec);
                }

                if (!incidentReported && spacedFailures.size() >= params.n) {
                    Vector<DataRecords> evidence;
                    for (size_t i = 0; i < spacedFailures.size(); i++) {
                        evidence.pushBack(spacedFailures[i]);
                    }
                    std::ostringstream detail;
                    detail << spacedFailures.size() << " lần FAILED_LOGIN rải rác trong "
                           << params.windowSec << " giây, cách nhau ít nhất "
                           << params.minSpacingSec << " giây nên bất thường";
                    appendAnomalyGroup(results, maxResults, total, "C4", "high", rec,
                                       spacedFailures.size(), detail.str(), evidence,
                                       fullOutput);
                    incidentReported = true;
                }
            }
        }
        return total;
    }

    if (type == "D2") {
        for (size_t u = 0; u < users.size(); u++) {
            bool hasPrevious = false;
            uint64_t previousTimestamp = 0;
            uint64_t rapidGaps = 0;
            bool incidentReported = false;
            Vector<DataRecords> chain;

            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                const uint64_t internalRapidGaps = rec.Count > 0 ? rec.Count - 1 : 0;

                if (hasPrevious && rec.timestamp >= previousTimestamp &&
                    rec.timestamp - previousTimestamp < params.minGapSec) {
                    if (chain.empty()) {
                        chain.pushBack(recs[userRecordIndices[p - 1]]);
                    }
                    chain.pushBack(rec);
                    rapidGaps += 1 + internalRapidGaps;
                } else {
                    rapidGaps = internalRapidGaps;
                    incidentReported = false;
                    chain.clear();
                    chain.pushBack(rec);
                }

                if (!incidentReported && rapidGaps >= params.n) {
                    std::ostringstream detail;
                    detail << rapidGaps << " khoảng cách liên tiếp nhỏ hơn " << params.minGapSec
                           << " giây nên bất thường";
                    appendAnomalyGroup(results, maxResults, total, "D2", "high", rec,
                                       rapidGaps, detail.str(), chain, fullOutput);
                    incidentReported = true;
                }

                previousTimestamp = rec.timestamp;
                hasPrevious = true;
            }
        }
        return total;
    }

    if (type == "D8") {
        for (size_t u = 0; u < users.size(); u++) {
            bool started = false;
            uint64_t currentDay = 0;
            uint64_t previousTimestamp = 0;
            uint64_t eventCount = 0;
            uint64_t gapCount = 0;
            double meanGap = 0.0;
            double gapM2 = 0.0;
            DataRecords lastRecord;
            Vector<DataRecords> dayRecords;

            auto addRepeatedGap = [&](double gap, uint64_t repeat) {
                if (repeat == 0) {
                    return;
                }
                const double oldCount = static_cast<double>(gapCount);
                const double newCount = oldCount + static_cast<double>(repeat);
                const double delta = gap - meanGap;
                const double newMean = meanGap + delta * static_cast<double>(repeat) / newCount;
                gapM2 += delta * (gap - newMean) * static_cast<double>(repeat);
                meanGap = newMean;
                gapCount += repeat;
            };

            auto reportDay = [&]() {
                if (!started || eventCount < params.minEvents || gapCount < 2 || meanGap <= 0.0) {
                    return;
                }
                const double stddev = std::sqrt(gapM2 / static_cast<double>(gapCount));
                const double cvPercent = stddev * 100.0 / meanGap;
                if (cvPercent > static_cast<double>(params.maxCvPercent)) {
                    return;
                }

                std::ostringstream detail;
                detail << std::fixed << std::setprecision(2)
                       << "Chu kỳ hoạt động quá đều trong ngày (CV=" << cvPercent
                       << "%, khoảng cách trung bình=" << meanGap << " giây)";
                appendAnomalyGroup(results, maxResults, total, "D8", "medium",
                                   lastRecord, eventCount, detail.str(), dayRecords,
                                   fullOutput);
            };

            for (int p = userOffsets[u]; p < userOffsets[u + 1]; p++) {
                const DataRecords& rec = recs[userRecordIndices[p]];
                const uint64_t day = rec.timestamp / 86400ULL;
                if (!started || day != currentDay) {
                    reportDay();
                    started = true;
                    currentDay = day;
                    previousTimestamp = rec.timestamp;
                    eventCount = rec.Count;
                    gapCount = 0;
                    meanGap = 0.0;
                    gapM2 = 0.0;
                    dayRecords.clear();
                    dayRecords.pushBack(rec);
                    addRepeatedGap(0.0, rec.Count > 0 ? rec.Count - 1 : 0);
                    lastRecord = rec;
                    continue;
                }

                addRepeatedGap(static_cast<double>(rec.timestamp - previousTimestamp), 1);
                addRepeatedGap(0.0, rec.Count > 0 ? rec.Count - 1 : 0);
                eventCount += rec.Count;
                previousTimestamp = rec.timestamp;
                dayRecords.pushBack(rec);
                lastRecord = rec;
            }
            reportDay();
        }
        return total;
    }

    if (type == "D10") {
        constexpr size_t LOCATION_COUNT = static_cast<size_t>(UNKNOWN_LOCATION) + 1;
        std::array<std::deque<size_t>, LOCATION_COUNT> windows;
        std::array<uint64_t, LOCATION_COUNT> totalCounts{};
        std::array<uint64_t, LOCATION_COUNT> failedCounts{};
        std::array<bool, LOCATION_COUNT> incidentReported{};

        for (size_t i = 0; i < recs.size(); i++) {
            const DataRecords& rec = recs[i];
            const size_t loc = static_cast<size_t>(rec.locationTag);
            if (loc >= LOCATION_COUNT || rec.locationTag == UNKNOWN_LOCATION) {
                continue;
            }

            std::deque<size_t>& window = windows[loc];
            window.push_back(i);
            totalCounts[loc] += rec.Count;
            if (rec.eventTypeTag == FAILED_LOGIN) {
                failedCounts[loc] += rec.Count;
            }

            while (!window.empty()) {
                const DataRecords& old = recs[window.front()];
                if (old.timestamp + params.windowSec >= rec.timestamp) {
                    break;
                }
                totalCounts[loc] -= old.Count;
                if (old.eventTypeTag == FAILED_LOGIN) {
                    failedCounts[loc] -= old.Count;
                }
                window.pop_front();
            }

            const bool enoughSamples = totalCounts[loc] >= params.minEvents;
            const bool ratioExceeded = enoughSamples &&
                failedCounts[loc] * 100ULL >= totalCounts[loc] * params.failureRatioPercent;
            if (!ratioExceeded) {
                incidentReported[loc] = false;
                continue;
            }
            if (incidentReported[loc]) {
                continue;
            }

            Vector<DataRecords> evidence;
            for (size_t k = 0; k < window.size(); k++) {
                evidence.pushBack(recs[window[k]]);
            }
            std::ostringstream detail;
            detail << "Location co " << failedCounts[loc] << "/" << totalCounts[loc]
                   << " FAILED_LOGIN, vuot nguong " << params.failureRatioPercent
                   << "% trong cua so thoi gian";
            appendAnomalyGroup(results, maxResults, total, "D10", "critical", rec,
                               failedCounts[loc], detail.str(), evidence, fullOutput);
            incidentReported[loc] = true;
        }
        return total;
    }

    return total;
}
