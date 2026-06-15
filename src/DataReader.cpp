#include "DataReader.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <thread>
#include <windows.h>

#include <string_view>
#include <sstream>

#include "ParseUtils.h"

namespace {
constexpr int IMPORT_PROGRESS_ROWS = 1000000;
constexpr uint64_t LARGE_FILE_PROGRESS_THRESHOLD = 1ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t PARALLEL_PARSE_FILE_THRESHOLD = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr int PARALLEL_PARSE_BATCH_ROWS = 65536;

void logImportProgress(const char* phase, int processedRows, int acceptedRows,
                       int maxRows, long long currentOffset, uint64_t fileSize,
                       std::chrono::steady_clock::time_point chunkStart) {
    using clock = std::chrono::steady_clock;
    double elapsedSec = std::chrono::duration<double>(clock::now() - chunkStart).count();
    double rowsPerSec = elapsedSec > 0.0 ? static_cast<double>(processedRows) / elapsedSec : 0.0;
    double percent = fileSize > 0 ? (static_cast<double>(currentOffset) * 100.0) / static_cast<double>(fileSize) : 0.0;

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out << "    > " << phase << ": " << processedRows << "/" << maxRows
        << " dong, hop le " << acceptedRows;
    if (fileSize > 0) {
        out << ", " << std::setprecision(1) << percent << "% file";
    }
    out << ", " << std::setprecision(0) << rowsPerSec << " dong/s\n";
    std::cout << out.str();
}
}  // namespace

class Win32LineReader {
   private:
    HANDLE hFile;
    char* buffer;
    DWORD bufferCapacity;
    DWORD bufferSize;
    DWORD bufferOffset;
    bool eof;
    long long currentFilePos;

   public:
    explicit Win32LineReader(const std::string& path) {
        buffer = nullptr;
        hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                            NULL);
        bufferCapacity = 8 * 1024 * 1024;
        bufferSize = 0;
        bufferOffset = 0;
        eof = (hFile == INVALID_HANDLE_VALUE);
        currentFilePos = 0;
        if (!eof) {
            try {
                buffer = new char[bufferCapacity];
            } catch (...) {
                CloseHandle(hFile);
                hFile = INVALID_HANDLE_VALUE;
                eof = true;
            }
        }
    }

    ~Win32LineReader() {
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
        }
        delete[] buffer;
    }

    bool isOpen() const {
        return hFile != INVALID_HANDLE_VALUE;
    }

    bool seek(long long offset) {
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        LARGE_INTEGER li;
        li.QuadPart = offset;
        LARGE_INTEGER newPos;
        if (!SetFilePointerEx(hFile, li, &newPos, FILE_BEGIN)) {
            return false;
        }

        bufferSize = 0;
        bufferOffset = 0;
        eof = false;
        currentFilePos = offset;
        return true;
    }

    long long tell() const {
        if (hFile == INVALID_HANDLE_VALUE) {
            return 0;
        }
        return currentFilePos - (bufferSize - bufferOffset);
    }

    bool readLine(std::string_view& line, std::string& scratch) {
        if (eof && bufferOffset >= bufferSize) {
            return false;
        }

        scratch.clear();
        while (true) {
            if (bufferOffset >= bufferSize) {
                DWORD read = 0;
                if (!ReadFile(hFile, buffer, bufferCapacity, &read, NULL) ||
                    read == 0) {
                    eof = true;
                    bufferSize = 0;
                    bufferOffset = 0;
                    break;
                }
                bufferSize = read;
                bufferOffset = 0;
                currentFilePos += read;
            }

            const char* start = buffer + bufferOffset;
            size_t available = static_cast<size_t>(bufferSize - bufferOffset);
            const char* newline = static_cast<const char*>(std::memchr(start, '\n', available));
            if (newline != nullptr) {
                size_t segmentLength = static_cast<size_t>(newline - start);
                bufferOffset += static_cast<DWORD>(segmentLength + 1);
                if (scratch.empty()) {
                    if (segmentLength > 0 && start[segmentLength - 1] == '\r') {
                        segmentLength--;
                    }
                    line = std::string_view(start, segmentLength);
                    return true;
                }

                scratch.append(start, segmentLength);
                if (!scratch.empty() && scratch.back() == '\r') {
                    scratch.pop_back();
                }
                line = scratch;
                return true;
            }

            scratch.append(start, available);
            bufferOffset = bufferSize;
        }

        if (!scratch.empty() && scratch.back() == '\r') {
            scratch.pop_back();
        }
        line = scratch;
        return !line.empty();
    }
};

event_Type DataReader::parseEvent(std::string_view evStr, bool& valid) const {
    valid = true;
    if (evStr == "LOGIN") {
        return event_Type::LOGIN;
    }
    if (evStr == "LOGOUT") {
        return event_Type::LOGOUT;
    }
    if (evStr == "TOKEN_REFRESH") {
        return event_Type::TOKEN_REFRESH;
    }
    if (evStr == "ACCESS") {
        return event_Type::ACCESS;
    }
    if (evStr == "FAILED_LOGIN") {
        return event_Type::FAILED_LOGIN;
    }
    if (evStr == "OPEN_APP") {
        return event_Type::OPEN_APP;
    }
    if (evStr == "DOWNLOAD") {
        return event_Type::DOWNLOAD;
    }
    if (evStr == "ADMIN_ACTION") {
        return event_Type::ADMIN_ACTION;
    }
    valid = false;
    return event_Type::UNKNOWN_EVENT;
}

location DataReader::parseLocation(std::string_view locStr, bool& valid) const {
    valid = true;
    if (locStr == "US") {
        return LOC_US;
    }
    if (locStr == "VN") {
        return LOC_VN;
    }
    if (locStr == "FR") {
        return LOC_FR;
    }
    if (locStr == "DE") {
        return LOC_DE;
    }
    if (locStr == "CN") {
        return LOC_CN;
    }
    if (locStr == "SG") {
        return LOC_SG;
    }
    if (locStr == "KR") {
        return LOC_KR;
    }
    if (locStr == "JP") {
        return LOC_JP;
    }
    if (locStr == "UK") {
        return LOC_UK;
    }
    if (locStr == "AU") {
        return LOC_AU;
    }
    if (locStr == "CA") {
        return LOC_CA;
    }
    if (locStr == "IN") {
        return LOC_IN;
    }
    if (locStr == "BR") {
        return LOC_BR;
    }
    if (locStr == "RU") {
        return LOC_RU;
    }
    if (locStr == "TH") {
        return LOC_TH;
    }
    valid = false;
    return location::UNKNOWN_LOCATION;
}

int DataReader::splitCSV(std::string_view line, std::string_view* fields,
                         int maxFields) const {
    int count = 0;
    size_t start = 0;
    size_t len = line.length();
    for (size_t i = 0; i <= len; i++) {
        if (i == len || line[i] == ',') {
            if (count < maxFields) {
                fields[count] = line.substr(start, i - start);
            }
            count++;
            start = i + 1;
        }
    }
    return count;
}

bool DataReader::isValidId(std::string_view value, std::string_view prefix) const {
    if (value.size() <= prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); i++) {
        if (value[i] != prefix[i]) {
            return false;
        }
    }
    uint64_t ignored = 0;
    return halo::parseUint64Strict(value.substr(prefix.size()), ignored);
}

void DataReader::parseLineToRow(std::string_view line, ParsedCsvRow& parsed) const {
    parsed = ParsedCsvRow();
    if (line.empty()) {
        return;
    }

    std::string_view fields[7];
    parsed.countTotal = true;

    int fieldCount = splitCSV(line, fields, 7);
    if (fieldCount != 7) {
        parsed.skipped = true;
        return;
    }

    std::string_view userId = fields[0];
    std::string_view deviceId = fields[1];
    std::string_view appId = fields[2];
    std::string_view resourceId = fields[3];

    if (!isValidId(fields[0], "U")) {
        userId = "UNKNOWN_USER";
        parsed.replaced = true;
    }

    if (!isValidId(fields[1], "D")) {
        deviceId = "UNKNOWN_DEVICE";
        parsed.replaced = true;
    }

    if (!isValidId(fields[2], "APP")) {
        appId = "UNKNOWN_APP";
        parsed.replaced = true;
    }

    if (!isValidId(fields[3], "R")) {
        resourceId = "UNKNOWN_RESOURCE";
        parsed.replaced = true;
    }

    bool evValid = true;
    event_Type ev = parseEvent(fields[4], evValid);
    if (!evValid || fields[4].empty()) {
        parsed.replaced = true;
    }

    bool locValid = true;
    location loc = parseLocation(fields[5], locValid);
    if (!locValid || fields[5].empty()) {
        parsed.replaced = true;
    }

    uint64_t timestamp = 0;
    if (!halo::parseUint64Strict(fields[6], timestamp)) {
        parsed.skipped = true;
        return;
    }

    parsed.record.timestamp = timestamp;
    parsed.record.eventTypeTag = ev;
    parsed.record.locationTag = loc;
    parsed.userId.assign(userId.data(), userId.size());
    parsed.deviceId.assign(deviceId.data(), deviceId.size());
    parsed.appId.assign(appId.data(), appId.size());
    parsed.resourceId.assign(resourceId.data(), resourceId.size());
}

bool DataReader::commitParsedRow(Halo& engine, const ParsedCsvRow& parsed) {
    if (!parsed.countTotal) {
        return false;
    }

    engine.incrementTotalRead();
    if (parsed.skipped) {
        engine.incrementSkipped();
        return false;
    }
    if (parsed.replaced) {
        engine.incrementReplaced();
    }

    engine.processRecord(parsed.record, parsed.userId, parsed.deviceId, parsed.appId,
                         parsed.resourceId);
    return true;
}

bool DataReader::processLine(Halo& engine, std::string_view line) {
    if (line.empty()) {
        return false;
    }

    std::string_view fields[7];
    engine.incrementTotalRead();

    int fieldCount = splitCSV(line, fields, 7);
    if (fieldCount != 7) {
        engine.incrementSkipped();
        return false;
    }

    bool hasReplaced = false;
    std::string_view userId = fields[0];
    std::string_view deviceId = fields[1];
    std::string_view appId = fields[2];
    std::string_view resourceId = fields[3];

    if (!isValidId(fields[0], "U")) {
        userId = "UNKNOWN_USER";
        hasReplaced = true;
    }

    if (!isValidId(fields[1], "D")) {
        deviceId = "UNKNOWN_DEVICE";
        hasReplaced = true;
    }

    if (!isValidId(fields[2], "APP")) {
        appId = "UNKNOWN_APP";
        hasReplaced = true;
    }

    if (!isValidId(fields[3], "R")) {
        resourceId = "UNKNOWN_RESOURCE";
        hasReplaced = true;
    }

    bool evValid = true;
    event_Type ev = parseEvent(fields[4], evValid);
    if (!evValid || fields[4].empty()) {
        hasReplaced = true;
    }

    bool locValid = true;
    location loc = parseLocation(fields[5], locValid);
    if (!locValid || fields[5].empty()) {
        hasReplaced = true;
    }

    uint64_t timestamp = 0;
    if (!halo::parseUint64Strict(fields[6], timestamp)) {
        engine.incrementSkipped();
        return false;
    }

    if (hasReplaced) {
        engine.incrementReplaced();
    }

    DataRecords newRecord;
    newRecord.timestamp = timestamp;
    newRecord.eventTypeTag = ev;
    newRecord.locationTag = loc;

    engine.processRecord(newRecord, userId, deviceId, appId, resourceId);
    return true;
}

void DataReader::readAll(Halo& engine) {
    Win32LineReader file(filePath);
    if (!file.isOpen()) {
        std::cout << "Khong the mo file dataset!" << std::endl;
        return;
    }

    std::string scratch;
    std::string_view line;
    scratch.reserve(512);
    file.readLine(line, scratch);

    int acceptedRows = 0;
    while (file.readLine(line, scratch)) {
        if (processLine(engine, line)) {
            acceptedRows++;
        }
    }

    std::cout << "Da doc xong CSV mot pass. Ban ghi hop le: " << acceptedRows
              << "." << std::endl;
}

bool DataReader::readSomeParallel(Halo& engine, Win32LineReader& file, int maxRows,
                                  uint64_t fileSize, long long& nextOffset,
                                  int& acceptedRows) {
    std::string scratch;
    std::string_view line;
    scratch.reserve(512);

    int processedRows = 0;
    int nextProgressMark =
        fileSize > LARGE_FILE_PROGRESS_THRESHOLD ? maxRows + 1 : IMPORT_PROGRESS_ROWS;
    auto chunkStart = std::chrono::steady_clock::now();
    bool eof = false;

    unsigned int hw = std::thread::hardware_concurrency();
    size_t maxWorkers = hw > 1 ? static_cast<size_t>(hw - 1) : 1;
    if (maxWorkers > 8) {
        maxWorkers = 8;
    }

    Vector<std::string> lines;
    Vector<ParsedCsvRow> parsedRows;
    lines.reserve(PARALLEL_PARSE_BATCH_ROWS);

    while (processedRows < maxRows) {
        lines.setSize(0);
        int batchLimit = maxRows - processedRows;
        if (batchLimit > PARALLEL_PARSE_BATCH_ROWS) {
            batchLimit = PARALLEL_PARSE_BATCH_ROWS;
        }

        for (int i = 0; i < batchLimit; i++) {
            if (!file.readLine(line, scratch)) {
                eof = true;
                break;
            }
            std::string copied(line.data(), line.size());
            lines.pushBack(std::move(copied));
        }

        if (lines.empty()) {
            break;
        }

        parsedRows.setSize(lines.size());

        size_t workers = maxWorkers;
        if (workers > lines.size()) {
            workers = lines.size();
        }
        if (workers <= 1) {
            for (size_t i = 0; i < lines.size(); i++) {
                parseLineToRow(lines[i], parsedRows[i]);
            }
        } else {
            Vector<std::thread> threads;
            threads.reserve(workers);
            for (size_t worker = 0; worker < workers; worker++) {
                size_t begin = (lines.size() * worker) / workers;
                size_t end = (lines.size() * (worker + 1)) / workers;
                threads.pushBack(std::thread([this, &lines, &parsedRows, begin, end]() {
                    for (size_t i = begin; i < end; i++) {
                        parseLineToRow(lines[i], parsedRows[i]);
                    }
                }));
            }
            for (size_t i = 0; i < threads.size(); i++) {
                if (threads[i].joinable()) {
                    threads[i].join();
                }
            }
        }

        for (size_t i = 0; i < parsedRows.size(); i++) {
            processedRows++;
            if (commitParsedRow(engine, parsedRows[i])) {
                acceptedRows++;
            }
        }

        if (IMPORT_PROGRESS_ROWS > 0 && processedRows >= nextProgressMark) {
            logImportProgress("Dang doc CSV song song", processedRows, acceptedRows,
                              maxRows, file.tell(), fileSize, chunkStart);
            nextProgressMark += IMPORT_PROGRESS_ROWS;
        }
    }

    nextOffset = file.tell();
    return eof || processedRows < maxRows;
}

bool DataReader::readSome(Halo& engine, long long startOffset, int maxRows,
                          long long& nextOffset, int& acceptedRows) {
    Win32LineReader file(filePath);
    acceptedRows = 0;
    nextOffset = startOffset;

    uint64_t fileSize = 0;
    try {
        fileSize = static_cast<uint64_t>(std::filesystem::file_size(filePath));
    } catch (...) {
        fileSize = 0;
    }

    if (!file.isOpen()) {
        std::cout << "Khong the mo file dataset!" << std::endl;
        return true;
    }

    if (startOffset > 0) {
        if (!file.seek(startOffset)) {
            std::cout << "Khong the khoi phuc vi tri checkpoint CSV." << std::endl;
            return true;
        }
    } else {
        std::string headerScratch;
        std::string_view header;
        file.readLine(header, headerScratch);
    }

    unsigned int hw = std::thread::hardware_concurrency();
    if ((forceParallelParse || fileSize >= PARALLEL_PARSE_FILE_THRESHOLD) && hw >= 4) {
        return readSomeParallel(engine, file, maxRows, fileSize, nextOffset,
                                acceptedRows);
    }

    std::string scratch;
    std::string_view line;
    scratch.reserve(512);
    int processedRows = 0;
    int nextProgressMark =
        fileSize > LARGE_FILE_PROGRESS_THRESHOLD ? maxRows + 1 : IMPORT_PROGRESS_ROWS;
    auto chunkStart = std::chrono::steady_clock::now();

    while (processedRows < maxRows && file.readLine(line, scratch)) {
        processedRows++;
        if (processLine(engine, line)) {
            acceptedRows++;
        }

        if (IMPORT_PROGRESS_ROWS > 0 && processedRows >= nextProgressMark) {
            logImportProgress("Dang doc CSV", processedRows, acceptedRows, maxRows,
                              file.tell(), fileSize, chunkStart);
            nextProgressMark += IMPORT_PROGRESS_ROWS;
        }
    }

    nextOffset = file.tell();
    return processedRows < maxRows;
}
