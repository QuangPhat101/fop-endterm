#include "DataReader.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <windows.h>

#include <string_view>
#include <sstream>

#include "ParseUtils.h"

namespace {
constexpr int IMPORT_PROGRESS_ROWS = 500000;

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
    out << ", " << std::setprecision(0) << rowsPerSec << " dong/s";
    std::cout << out.str() << std::endl;
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

    bool readLine(std::string& line) {
        if (eof && bufferOffset >= bufferSize) {
            return false;
        }

        line.clear();
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

            char c = buffer[bufferOffset++];
            if (c == '\n') {
                break;
            }
            if (c != '\r') {
                line.push_back(c);
            }
        }
        return !line.empty() || !eof || bufferOffset < bufferSize;
    }
};

event_Type DataReader::parseEvent(std::string_view evStr, bool& valid) {
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

location DataReader::parseLocation(std::string_view locStr, bool& valid) {
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
                         int maxFields) {
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

bool DataReader::isValidId(std::string_view value, std::string_view prefix) {
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

bool DataReader::processLine(Halo& engine, const std::string& line) {
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
    std::string userId;
    std::string deviceId;
    std::string appId;
    std::string resourceId;

    if (!isValidId(fields[0], "U")) {
        userId = "UNKNOWN_USER";
        hasReplaced = true;
    } else {
        userId.assign(fields[0].data(), fields[0].size());
    }

    if (!isValidId(fields[1], "D")) {
        deviceId = "UNKNOWN_DEVICE";
        hasReplaced = true;
    } else {
        deviceId.assign(fields[1].data(), fields[1].size());
    }

    if (!isValidId(fields[2], "APP")) {
        appId = "UNKNOWN_APP";
        hasReplaced = true;
    } else {
        appId.assign(fields[2].data(), fields[2].size());
    }

    if (!isValidId(fields[3], "R")) {
        resourceId = "UNKNOWN_RESOURCE";
        hasReplaced = true;
    } else {
        resourceId.assign(fields[3].data(), fields[3].size());
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

    std::string header;
    std::string line;
    file.readLine(header);
    line.reserve(512);

    int acceptedRows = 0;
    while (file.readLine(line)) {
        if (processLine(engine, line)) {
            acceptedRows++;
        }
    }

    std::cout << "Da doc xong CSV mot pass. Ban ghi hop le: " << acceptedRows
              << "." << std::endl;
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
        std::string header;
        file.readLine(header);
    }

    std::string line;
    line.reserve(512);
    int processedRows = 0;
    int nextProgressMark = IMPORT_PROGRESS_ROWS;
    auto chunkStart = std::chrono::steady_clock::now();

    while (processedRows < maxRows && file.readLine(line)) {
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
