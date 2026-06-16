#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include "DataReader.h"
#include "Halo.h"
#include "ParseUtils.h"
#include "Platform.h"

namespace fs = std::filesystem;

const int IMPORT_CHUNK_ROWS = 2000000;
const uint64_t DEFAULT_CHECKPOINT_ROWS = 2000000ULL;
const int DEFAULT_HALO_PORT = 24117;

struct DatasetLoadMetrics {
    double binaryLoadTimeMs = 0.0;
    double csvImportTimeMs = 0.0;
    double checkpointTimeMs = 0.0;
    double finalizeTimeMs = 0.0;
    double sortTimeMs = 0.0;
    double deduplicateTimeMs = 0.0;
    double indexBuildTimeMs = 0.0;
    std::string sortAlgorithm;
    uint64_t checkpointBytesWritten = 0;
    uint64_t checkpointCount = 0;
    uint64_t estimatedRecords = 0;
    bool resumedCheckpoint = false;
    bool parallelCsvParsing = false;
};

// Decode URL entities
std::string urlDecode(const std::string& src) {
    std::string dst = "";
    char a = '\0';
    char b = '\0';
    for (size_t i = 0; i < src.length(); i++) {
        if ((src[i] == '%') && (i + 2 < src.length()) &&
            isxdigit(static_cast<unsigned char>(src[i + 1])) &&
            isxdigit(static_cast<unsigned char>(src[i + 2]))) {
            a = src[i + 1];
            b = src[i + 2];
            dst += static_cast<char>(
                (tolower(static_cast<unsigned char>(a)) >= 'a' ? tolower(static_cast<unsigned char>(a)) - 'a' + 10 : a - '0') * 16 +
                (tolower(static_cast<unsigned char>(b)) >= 'a' ? tolower(static_cast<unsigned char>(b)) - 'a' + 10 : b - '0'));
            i += 2;
        } else if (src[i] == '+') {
            dst += ' ';
        } else {
            dst += src[i];
        }
    }
    return dst;
}

// Get query param by key
std::string getQueryParam(const std::string& query, const std::string& key) {
    size_t pos = query.find(key + "=");
    if (pos == std::string::npos) {
        return "";
    }
    pos += key.length() + 1;
    size_t endPos = query.find('&', pos);
    std::string val = (endPos == std::string::npos) ? query.substr(pos) : query.substr(pos, endPos - pos);
    return urlDecode(val);
}

uint64_t getUintParam(const std::string& query, const std::string& key,
                      uint64_t defaultValue) {
    std::string raw = getQueryParam(query, key);
    uint64_t parsed = 0;
    if (raw.empty() || !halo::parseUint64Strict(raw, parsed)) {
        return defaultValue;
    }
    return parsed;
}

int getIntParam(const std::string& query, const std::string& key,
                int defaultValue) {
    uint64_t parsed = getUintParam(query, key, static_cast<uint64_t>(defaultValue));
    if (parsed > 2147483647ULL) {
        return defaultValue;
    }
    return static_cast<int>(parsed);
}

bool parsePortText(const std::string& raw, int& port) {
    uint64_t parsed = 0;
    if (raw.empty() || !halo::parseUint64Strict(raw, parsed)) {
        return false;
    }
    if (parsed == 0 || parsed > 65535ULL) {
        return false;
    }
    port = static_cast<int>(parsed);
    return true;
}

int resolveHaloPort(int argc, char* argv[]) {
    int port = DEFAULT_HALO_PORT;

    for (int i = 1; i < argc; i++) {
        std::string raw = "";
        std::string arg = (argv[i] != nullptr) ? argv[i] : "";

        if (arg == "--port" && i + 1 < argc) {
            i++;
            raw = (argv[i] != nullptr) ? argv[i] : "";
        } else if (arg.rfind("--port=", 0) == 0) {
            raw = arg.substr(7);
        } else if (i == 1) {
            raw = arg;
        }

        if (!raw.empty()) {
            if (parsePortText(raw, port)) {
                return port;
            }
            std::cerr << "Canh bao: port tham so khong hop le, dung mac dinh "
                      << DEFAULT_HALO_PORT << ".\n";
            return DEFAULT_HALO_PORT;
        }
    }

    const char* envPort = std::getenv("HALO_PORT");
    if (envPort != nullptr) {
        std::string raw = envPort;
        if (parsePortText(raw, port)) {
            return port;
        }
        std::cerr << "Canh bao: HALO_PORT khong hop le, dung mac dinh "
                  << DEFAULT_HALO_PORT << ".\n";
    }

    return port;
}

SortMode parseSortModeParam(const std::string& query) {
    std::string raw = getQueryParam(query, "sortMode");
    if (raw == "merge") {
        return SortMode::Merge;
    }
    if (raw == "intro" || raw == "introsort" || raw == "quick") {
        return SortMode::Intro;
    }
    if (raw == "radix" || raw == "radix-hybrid") {
        return SortMode::RadixHybrid;
    }
    if (raw == "external" || raw == "external-partitioned" || raw == "partitioned") {
        return SortMode::ExternalPartitioned;
    }
    return SortMode::Auto;
}

size_t estimateCsvRecordCount(const std::string& csvFile, uint64_t csvSize) {
    if (csvSize == 0) {
        return 0;
    }

    std::ifstream input(csvFile, std::ios::binary);
    if (!input.is_open()) {
        return 0;
    }

    std::string line;
    if (!std::getline(input, line)) {
        return 0;
    }

    constexpr size_t SAMPLE_ROWS = 4096;
    uint64_t sampledBytes = 0;
    size_t sampledRows = 0;
    while (sampledRows < SAMPLE_ROWS && std::getline(input, line)) {
        sampledBytes += static_cast<uint64_t>(line.size()) + 1;
        sampledRows++;
    }

    if (sampledRows == 0 || sampledBytes == 0) {
        return 0;
    }

    double averageBytes =
        static_cast<double>(sampledBytes) / static_cast<double>(sampledRows);
    uint64_t estimate =
        static_cast<uint64_t>(static_cast<double>(csvSize) / averageBytes * 1.03);
    uint64_t maxRecords =
        static_cast<uint64_t>(std::numeric_limits<int>::max()) - 1ULL;
    if (estimate > maxRecords) {
        estimate = maxRecords;
    }
    return static_cast<size_t>(estimate);
}

struct ImportReserveHints {
    size_t records = 0;
    size_t users = 0;
    size_t devices = 0;
    size_t apps = 0;
    size_t resources = 0;
};

ImportReserveHints estimateImportReserveHints(size_t estimatedRecords) {
    ImportReserveHints hints;
    hints.records = estimatedRecords;
    if (estimatedRecords < 5000000ULL) {
        return hints;
    }

    hints.users = estimatedRecords / 50ULL;
    if (hints.users < 1000ULL) {
        hints.users = 1000ULL;
    }
    hints.devices = estimatedRecords / 100ULL;
    if (hints.devices < 1000ULL) {
        hints.devices = 1000ULL;
    }
    hints.apps = estimatedRecords / 1000ULL;
    if (hints.apps < 100ULL) {
        hints.apps = 100ULL;
    }
    hints.resources = estimatedRecords / 500ULL;
    if (hints.resources < 1000ULL) {
        hints.resources = 1000ULL;
    }
    return hints;
}

bool parseTimeRange(const std::string& query, uint64_t& startTime,
                    uint64_t& endTime, std::string& error) {
    startTime = 0;
    endTime = UINT64_MAX;
    std::string startStr = getQueryParam(query, "startTime");
    std::string endStr = getQueryParam(query, "endTime");
    if (!startStr.empty() && !halo::parseUint64Strict(startStr, startTime)) {
        error = "startTime khong hop le.";
        return false;
    }
    if (!endStr.empty() && !halo::parseUint64Strict(endStr, endTime)) {
        error = "endTime khong hop le.";
        return false;
    }
    if (startTime > endTime) {
        error = "Khoang thoi gian khong hop le: startTime > endTime.";
        return false;
    }
    return true;
}

// Escape strings for clean JSON serialization
std::string jsonEscape(const std::string& input) {
    std::string output = "";
    for (char c : input) {
        if (c == '"') {
            output += "\\\"";
        } else if (c == '\\') {
            output += "\\\\";
        } else if (c == '\n') {
            output += "\\n";
        } else if (c == '\r') {
            output += "\\r";
        } else if (c == '\t') {
            output += "\\t";
        } else {
            output += c;
        }
    }
    return output;
}

std::string jsonError(const std::string& error) {
    return "{\"success\": false, \"error\": \"" + jsonEscape(error) + "\"}";
}

std::string formatUtcEpochText(uint64_t epochSeconds) {
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

bool isBinaryCacheFresh(const std::string& csvFile, const std::string& binPath) {
    try {
        if (!fs::exists(csvFile) || !fs::exists(binPath)) {
            return false;
        }
        if (!fs::is_regular_file(csvFile) || !fs::is_regular_file(binPath)) {
            return false;
        }
        if (fs::file_size(binPath) < sizeof(uint32_t)) {
            return false;
        }

        std::ifstream cacheFile(binPath, std::ios::binary);
        uint32_t magic = 0;
        cacheFile.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (!cacheFile || magic != 0x48414C4F) {
            return false;
        }

        return fs::last_write_time(binPath) >= fs::last_write_time(csvFile);
    } catch (const std::exception& e) {
        std::cerr << "Loi kiem tra cache .dat: " << e.what() << "\n";
        return false;
    }
}

bool cachedDatasetNameFromPath(const fs::path& cachePath, std::string& datasetName) {
    std::string filename = cachePath.filename().string();
    const std::string suffix = ".csv.dat";
    if (filename.length() <= suffix.length()) {
        return false;
    }
    if (filename.substr(filename.length() - suffix.length()) != suffix) {
        return false;
    }

    datasetName = filename.substr(0, filename.length() - 4);
    return true;
}

long long fileTimeKey(const std::string& path) {
    return (long long)fs::last_write_time(path).time_since_epoch().count();
}

bool replaceFile(const std::string& from, const std::string& to) {
    return platform::replaceFileAtomic(from, to);
}

void deleteImportCheckpoint(const std::string& checkpointFile) {
    std::remove(checkpointFile.c_str());
    std::remove((checkpointFile + ".dat").c_str());
    std::remove((checkpointFile + ".writing").c_str());
    std::remove((checkpointFile + ".dat.writing").c_str());
}

bool loadImportCheckpoint(Halo& engine, const std::string& csvFile,
                          const std::string& checkpointFile,
                          long long& startOffset) {
    if (!fs::exists(checkpointFile)) {
        return false;
    }

    std::ifstream in(checkpointFile);
    std::string magic;
    long long csvSize = 0;
    long long csvTime = 0;
    long long savedOffset = 0;

    std::getline(in, magic);
    in >> csvSize >> csvTime >> savedOffset;
    if (!in || magic != "HALO_IMPORT_CHECKPOINT_V1") {
        deleteImportCheckpoint(checkpointFile);
        return false;
    }

    try {
        if (!fs::exists(csvFile) || !fs::is_regular_file(csvFile)) {
            deleteImportCheckpoint(checkpointFile);
            return false;
        }
        if ((long long)fs::file_size(csvFile) != csvSize ||
            fileTimeKey(csvFile) != csvTime) {
            deleteImportCheckpoint(checkpointFile);
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Loi kiem tra checkpoint: " << e.what() << "\n";
        deleteImportCheckpoint(checkpointFile);
        return false;
    }

    std::string snapshotPath = checkpointFile + ".dat";
    if (!engine.loadFromBinary(snapshotPath)) {
        engine.clear();
        deleteImportCheckpoint(checkpointFile);
        return false;
    }

    startOffset = savedOffset;
    return true;
}

bool saveImportCheckpoint(const Halo& engine, const std::string& csvFile,
                           const std::string& checkpointFile,
                           long long offset,
                           uint64_t* bytesWritten = nullptr) {
    std::string snapshotPath = checkpointFile + ".dat";
    std::string snapshotTmp = snapshotPath + ".writing";
    std::string metaTmp = checkpointFile + ".writing";
    if (bytesWritten != nullptr) {
        *bytesWritten = 0;
    }

    if (!engine.saveToBinary(snapshotTmp)) {
        return false;
    }
    if (!replaceFile(snapshotTmp, snapshotPath)) {
        std::remove(snapshotTmp.c_str());
        return false;
    }

    std::ofstream out(metaTmp, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "HALO_IMPORT_CHECKPOINT_V1\n"
        << (long long)fs::file_size(csvFile) << "\n"
        << fileTimeKey(csvFile) << "\n"
        << offset << "\n";
    out.close();

    if (!replaceFile(metaTmp, checkpointFile)) {
        std::remove(metaTmp.c_str());
        return false;
    }

    if (bytesWritten != nullptr) {
        try {
            *bytesWritten =
                static_cast<uint64_t>(fs::file_size(snapshotPath)) +
                static_cast<uint64_t>(fs::file_size(checkpointFile));
        } catch (...) {
            *bytesWritten = 0;
        }
    }

    return true;
}

// Fully load dataset, returns true if loaded from .dat
bool loadDatasetCompletely(Halo& engine, const std::string& csvFile,
                            const std::string& checkpointFile,
                            bool enableImportCheckpoint,
                            bool enableParallelCsv,
                            uint64_t checkpointBaseRows,
                            DatasetLoadMetrics& metrics) {
    metrics = DatasetLoadMetrics();
    std::string binPath = csvFile + ".dat";
    std::cout << "Dang kiem tra bo dem nhi phan: " << binPath << "...\n";
    if (isBinaryCacheFresh(csvFile, binPath)) {
        auto binaryStart = std::chrono::high_resolution_clock::now();
        if (engine.loadFromBinary(binPath)) {
            auto binaryEnd = std::chrono::high_resolution_clock::now();
            metrics.binaryLoadTimeMs =
                std::chrono::duration<double, std::milli>(binaryEnd - binaryStart).count();
            const FinalizeMetrics& finalizeMetrics = engine.getFinalizeMetrics();
            metrics.indexBuildTimeMs = finalizeMetrics.indexBuildTimeMs;
            std::cout << ">> DA NAP THANH CONG tu bo dem nhi phan (.dat) trong nhay mat!\n";
            std::cout << "Hoan tat nap du lieu! Ban ghi sach: " << engine.getRecordCount() << "\n";
            return true;
        }
    }

    std::cout << "Dang nap tap du lieu tu: " << csvFile << "...\n";
    std::cout << "Che do checkpoint import: "
              << (enableImportCheckpoint ? "bat" : "tat") << ".\n";
    std::cout << "Che do parse CSV song song: "
              << (enableParallelCsv ? "bat" : "tu dong") << ".\n";
    metrics.parallelCsvParsing = enableParallelCsv;
    engine.clear();

    if (!enableImportCheckpoint) {
        deleteImportCheckpoint(checkpointFile);
    }

    long long startOffset = 0;
    if (enableImportCheckpoint && loadImportCheckpoint(engine, csvFile, checkpointFile, startOffset)) {
        metrics.resumedCheckpoint = true;
        std::cout << ">> Khoi phuc checkpoint CSV tai byte offset "
                  << startOffset << ".\n";
    }

    uint64_t csvSize = 0;
    try {
        csvSize = static_cast<uint64_t>(fs::file_size(csvFile));
    } catch (...) {
        csvSize = 0;
    }

    size_t estimatedRecords = estimateCsvRecordCount(csvFile, csvSize);
    metrics.estimatedRecords = static_cast<uint64_t>(estimatedRecords);
    constexpr size_t PRE_SIZE_MIN_RECORDS = 5000000;
    if (estimatedRecords >= PRE_SIZE_MIN_RECORDS &&
        estimatedRecords > engine.getRecordCount()) {
        ImportReserveHints hints = estimateImportReserveHints(estimatedRecords);
        engine.reserveForImport(hints.records, hints.users, hints.devices, hints.apps,
                                hints.resources);
    }

    if (checkpointBaseRows == 0) {
        checkpointBaseRows = DEFAULT_CHECKPOINT_ROWS;
    }
    uint64_t nextCheckpointRows = checkpointBaseRows;
    uint64_t currentRows = engine.getTotalReadCount();
    while (nextCheckpointRows <= currentRows &&
           nextCheckpointRows <= std::numeric_limits<uint64_t>::max() / 2) {
        nextCheckpointRows *= 2;
    }

    DataReader reader(csvFile, enableParallelCsv);
    int chunkIdx = 1;
    auto importStart = std::chrono::high_resolution_clock::now();
    while (true) {
        long long nextOffset = startOffset;
        int acceptedRows = 0;
        bool eof = reader.readSome(engine, startOffset, IMPORT_CHUNK_ROWS,
                                   nextOffset, acceptedRows);

        std::cout << "  - Import part " << chunkIdx++
                  << ": them " << acceptedRows
                  << " ban ghi hop le, offset " << nextOffset;
        if (csvSize > 0) {
            double percent = (static_cast<double>(nextOffset) * 100.0) / static_cast<double>(csvSize);
            std::ostringstream percentText;
            percentText << std::fixed << std::setprecision(1) << percent;
            std::cout << " (" << percentText.str() << "%)";
        }
        std::cout << ".\n";

        if (eof) {
            break;
        }
        if (nextOffset <= startOffset) {
            std::cout << ">> Dung import vi checkpoint khong tien len.\n";
            break;
        }

        if (enableImportCheckpoint &&
            engine.getTotalReadCount() >= nextCheckpointRows) {
            uint64_t checkpointBytes = 0;
            auto checkpointStart = std::chrono::high_resolution_clock::now();
            bool checkpointOk =
                saveImportCheckpoint(engine, csvFile, checkpointFile, nextOffset,
                                     &checkpointBytes);
            auto checkpointEnd = std::chrono::high_resolution_clock::now();
            metrics.checkpointTimeMs +=
                std::chrono::duration<double, std::milli>(checkpointEnd - checkpointStart).count();
            if (checkpointOk) {
                metrics.checkpointBytesWritten += checkpointBytes;
                metrics.checkpointCount++;
            } else {
                std::cout << ">> Canh bao: khong ghi duoc checkpoint import.\n";
            }
            do {
                if (nextCheckpointRows >
                    std::numeric_limits<uint64_t>::max() / 2) {
                    nextCheckpointRows = std::numeric_limits<uint64_t>::max();
                    break;
                }
                nextCheckpointRows *= 2;
            } while (nextCheckpointRows <= engine.getTotalReadCount());
        }
        startOffset = nextOffset;
    }
    auto importEnd = std::chrono::high_resolution_clock::now();
    metrics.csvImportTimeMs =
        std::chrono::duration<double, std::milli>(importEnd - importStart).count();

    std::cout << "Dang sap xep va loai bo ban ghi trung lap...\n";
    engine.finalizeLoading();
    const FinalizeMetrics& finalizeMetrics = engine.getFinalizeMetrics();
    metrics.finalizeTimeMs = finalizeMetrics.finalizeTimeMs;
    metrics.sortTimeMs = finalizeMetrics.sortTimeMs;
    metrics.sortAlgorithm = finalizeMetrics.sortAlgorithm;
    metrics.deduplicateTimeMs = finalizeMetrics.deduplicateTimeMs;
    metrics.indexBuildTimeMs = finalizeMetrics.indexBuildTimeMs;
    std::cout << "Hoan tat nap du lieu! Ban ghi sach: " << engine.getRecordCount() << "\n";

    if (enableImportCheckpoint) {
        deleteImportCheckpoint(checkpointFile);
    }
    return false;
}

int main(int argc, char* argv[]) {
    // Set console code page to UTF-8 for clean Vietnamese logs
    platform::setConsoleUtf8();

    int haloPort = resolveHaloPort(argc, argv);

    std::cout << "==================================================\n";

    struct Workspace {
        std::string name;
        Halo* engine;
        Workspace(std::string n, Halo* e) : name(n), engine(e) {}
        ~Workspace() {
            delete engine;
        }
    };

    Vector<Workspace*> workspaces;

    auto getWorkspace = [&](const std::string& name) -> Halo* {
        for (size_t i = 0; i < workspaces.size(); ++i) {
            if (workspaces[i]->name == name) {
                return workspaces[i]->engine;
            }
        }
        return nullptr;
    };

    auto ensureWorkspaceLoaded = [&](const std::string& name) -> Halo* {
        Halo* engine = getWorkspace(name);
        if (engine != nullptr) {
            return engine;
        }

        std::string csvPath = "./data/" + name;
        std::string binPath = csvPath + ".dat";
        if (!isBinaryCacheFresh(csvPath, binPath)) {
            return nullptr;
        }

        engine = new Halo();
        if (!engine->loadFromBinary(binPath)) {
            delete engine;
            return nullptr;
        }

        Workspace* workspace = new Workspace(name, engine);
        try {
            workspaces.pushBack(workspace);
        } catch (...) {
            delete workspace;
            throw;
        }
        return engine;
    };

    platform::HttpServer server;
    std::string serverError;
    if (!server.listenOn(haloPort, serverError)) {
        std::cerr << serverError << "\n";
        return 1;
    }

    std::cout << "\n[OK] Web Server dang chay tai: http://localhost:"
              << haloPort << "\n";
    std::cout << "[!] Vui long mo trinh duyet va truy cap dia chi tren de dung UI.\n";
    std::cout << "--------------------------------------------------\n";

    // 5. Server Loop
    while (true) {
        std::string acceptError;
        platform::TcpClient clientSocket = server.accept(acceptError);
        if (!clientSocket.isValid()) {
            std::cerr << acceptError << "\n";
            continue;
        }

        // Set a 1-second receive timeout to prevent hanging on keep-alive connections
        clientSocket.setReceiveTimeoutMs(1000);

        char recvbuf[4096];
        int bytesReceived = clientSocket.receive(recvbuf, sizeof(recvbuf) - 1);
        if (bytesReceived > 0) {
            recvbuf[bytesReceived] = '\0';
            std::string requestStr(recvbuf);

            std::stringstream ss(requestStr);
            std::string method = "";
            std::string fullUrl = "";
            ss >> method >> fullUrl;

            // Route matching
            size_t questionMark = fullUrl.find('?');
            std::string path = (questionMark == std::string::npos) ? fullUrl : fullUrl.substr(0, questionMark);
            std::string query = (questionMark == std::string::npos) ? "" : fullUrl.substr(questionMark + 1);

            std::string responseHeader = "";
            std::string responseBody = "";

            if (path == "/" || path == "index.html") {
                // Try serving local index.html
                std::ifstream htmlFile("index.html");
                if (!htmlFile.is_open()) {
                    htmlFile.open("src/index.html");
                }
                if (htmlFile.is_open()) {
                    std::stringstream buffer;
                    buffer << htmlFile.rdbuf();
                    responseBody = buffer.str();
                    htmlFile.close();
                } else {
                    // Fallback to minimal placeholder with notice if HTML is missing
                    responseBody =
                        "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>HALO Error</title></head>"
                        "<body style='font-family:sans-serif; background:#0f172a; color:#f1f5f9; text-align:center; padding:50px;'>"
                        "<h1>Không tìm thấy tệp index.html!</h1>"
                        "<p>Vui lòng kiểm tra xem tệp index.html có nằm cùng thư mục chạy không.</p>"
                        "</body></html>";
                }
                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/list-loaded") {
                // List RAM workspaces and valid binary caches from previous runs.
                std::stringstream json;
                Vector<std::string> loadedNames;

                for (size_t i = 0; i < workspaces.size(); ++i) {
                    loadedNames.pushBack(workspaces[i]->name);
                }

                try {
                    if (fs::exists("./data") && fs::is_directory("./data")) {
                        for (const auto& entry : fs::directory_iterator("./data")) {
                            std::string datasetName;
                            if (!entry.is_regular_file() ||
                                !cachedDatasetNameFromPath(entry.path(), datasetName)) {
                                continue;
                            }

                            std::string csvPath = "./data/" + datasetName;
                            std::string binPath = entry.path().string();
                            if (!isBinaryCacheFresh(csvPath, binPath)) {
                                continue;
                            }

                            bool existsInList = false;
                            for (size_t i = 0; i < loadedNames.size(); i++) {
                                if (loadedNames[i] == datasetName) {
                                    existsInList = true;
                                    break;
                                }
                            }
                            if (!existsInList) {
                                loadedNames.pushBack(datasetName);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Loi khi list cache .dat: " << e.what() << "\n";
                }

                json << "[";
                bool first = true;
                for (size_t i = 0; i < loadedNames.size(); i++) {
                    if (!first) {
                        json << ",";
                    }
                    json << "\"" << jsonEscape(loadedNames[i]) << "\"";
                    first = false;
                }
                json << "]";

                responseBody = json.str();
                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/list-datasets") {
                // List files in data/ ending with .csv
                std::stringstream json;
                json << "[";
                bool first = true;
                try {
                    if (fs::exists("./data") && fs::is_directory("./data")) {
                        for (const auto& entry : fs::directory_iterator("./data")) {
                            if (entry.is_regular_file() && entry.path().extension() == ".csv") {
                                if (!first) {
                                    json << ",";
                                }
                                json << "\"" << jsonEscape(entry.path().filename().string()) << "\"";
                                first = false;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Loi khi list file: " << e.what() << "\n";
                }
                json << "]";

                responseBody = json.str();
                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/load-dataset") {
                std::string fileParam = getQueryParam(query, "file");
                std::string saveCacheParam = getQueryParam(query, "saveCache");
                bool saveBinaryCache = (saveCacheParam != "0" && saveCacheParam != "false");
                std::string resumeCheckpointParam = getQueryParam(query, "resumeCheckpoint");
                bool enableImportCheckpoint = (resumeCheckpointParam == "1" ||
                                               resumeCheckpointParam == "true");
                std::string parallelCsvParam = getQueryParam(query, "parallelCsv");
                bool enableParallelCsv = (parallelCsvParam == "1" ||
                                          parallelCsvParam == "true");
                uint64_t checkpointBaseRows =
                    getUintParam(query, "checkpointRows", DEFAULT_CHECKPOINT_ROWS);
                if (checkpointBaseRows < 100000ULL) {
                    checkpointBaseRows = 100000ULL;
                }
                SortMode requestedSortMode = parseSortModeParam(query);
                std::string filePath = "./data/" + fileParam;

                bool success = true;
                std::string errorMsg = "";
                bool loadedFromDat = false;
                double loadTimeMs = 0.0;
                double memoryMB = 0.0;
                double cacheTimeMs = 0.0;
                DatasetLoadMetrics loadMetrics;

                if (fileParam.empty()) {
                    success = false;
                    errorMsg = "Chua cung cap ten tap du lieu.";
                } else if (!fs::exists(filePath)) {
                    success = false;
                    errorMsg = "Tap du lieu khong ton tai.";
                } else {
                    Halo* engine = getWorkspace(fileParam);
                    if (engine == nullptr) {
                        engine = new Halo();
                        Workspace* workspace = new Workspace(fileParam, engine);
                        try {
                            workspaces.pushBack(workspace);
                        } catch (...) {
                            delete workspace;
                            throw;
                        }
                    }
                    engine->setSortMode(requestedSortMode);

                    std::string checkpointFile = "./data/Checkpoint_" + fileParam + ".tmp";

                    // Measure Memory Before
                    double memBefore = platform::currentProcessPrivateMemoryMB();

                    // Measure Time Before
                    auto start_time = std::chrono::high_resolution_clock::now();

                    try {
                        loadedFromDat = loadDatasetCompletely(*engine, filePath,
                                                              checkpointFile,
                                                              enableImportCheckpoint,
                                                              enableParallelCsv,
                                                              checkpointBaseRows,
                                                              loadMetrics);
                    } catch (const std::exception& e) {
                        success = false;
                        errorMsg = e.what();
                    }

                    // Measure Time After
                    auto end_time = std::chrono::high_resolution_clock::now();
                    loadTimeMs = std::chrono::duration<double, std::milli>(end_time - start_time).count();

                    // Measure Memory After
                    double memAfter = platform::currentProcessPrivateMemoryMB();

                    memoryMB = (memAfter > memBefore) ? (memAfter - memBefore) : 0.0;

                    if (success && !loadedFromDat) {
                        if (saveBinaryCache) {
                            std::string binPath = filePath + ".dat";
                            std::cout << "Dang luu bo dem nhi phan (.dat) de toi uu cho lan sau...\n";
                            auto c_start = std::chrono::high_resolution_clock::now();
                            if (engine->saveToBinary(binPath)) {
                                std::cout << ">> Luu bo dem nhi phan hoan tat!\n";
                            } else {
                                std::cout << ">> Ghi chu: Khong the luu bo dem nhi phan.\n";
                            }
                            auto c_end = std::chrono::high_resolution_clock::now();
                            cacheTimeMs = std::chrono::duration<double, std::milli>(c_end - c_start).count();
                        } else {
                            std::cout << "Bo qua ghi bo dem nhi phan (.dat) theo lua chon nguoi dung.\n";
                        }
                    }
                }

                std::stringstream json;
                if (success) {
                    Halo* engine = getWorkspace(fileParam);
                    json << "{\n"
                         << "  \"success\": true,\n"
                         << "  \"error\": \"\",\n"
                          << "  \"timeMs\": " << loadTimeMs << ",\n"
                          << "  \"cacheTimeMs\": " << cacheTimeMs << ",\n"
                          << "  \"metrics\": {\n"
                          << "    \"binaryLoadTimeMs\": " << loadMetrics.binaryLoadTimeMs << ",\n"
                          << "    \"csvImportTimeMs\": " << loadMetrics.csvImportTimeMs << ",\n"
                          << "    \"checkpointTimeMs\": " << loadMetrics.checkpointTimeMs << ",\n"
                          << "    \"checkpointBytesWritten\": " << loadMetrics.checkpointBytesWritten << ",\n"
                          << "    \"checkpointCount\": " << loadMetrics.checkpointCount << ",\n"
                          << "    \"estimatedRecords\": " << loadMetrics.estimatedRecords << ",\n"
                          << "    \"resumedCheckpoint\": " << (loadMetrics.resumedCheckpoint ? "true" : "false") << ",\n"
                          << "    \"parallelCsvParsing\": " << (loadMetrics.parallelCsvParsing ? "true" : "false") << ",\n"
                          << "    \"finalizeTimeMs\": " << loadMetrics.finalizeTimeMs << ",\n"
                          << "    \"sortTimeMs\": " << loadMetrics.sortTimeMs << ",\n"
                          << "    \"sortAlgorithm\": \"" << jsonEscape(loadMetrics.sortAlgorithm) << "\",\n"
                          << "    \"deduplicateTimeMs\": " << loadMetrics.deduplicateTimeMs << ",\n"
                          << "    \"indexBuildTimeMs\": " << loadMetrics.indexBuildTimeMs << ",\n"
                          << "    \"cacheSaveTimeMs\": " << cacheTimeMs << "\n"
                          << "  },\n"
                          << "  \"memoryMB\": " << memoryMB << ",\n"
                         << "  \"loadedFromDat\": " << (loadedFromDat ? "true" : "false") << ",\n"
                         << "  \"stats\": {\n"
                         << "    \"users\": " << engine->getUserCount() << ",\n"
                         << "    \"devices\": " << engine->getDeviceCount() << ",\n"
                         << "    \"apps\": " << engine->getAppCount() << ",\n"
                         << "    \"resources\": " << engine->getResourceCount() << ",\n"
                         << "    \"records\": " << engine->getRecordCount() << ",\n"
                         << "    \"totalRead\": " << engine->getTotalReadCount() << ",\n"
                         << "    \"skipped\": " << engine->getSkippedRowCount() << ",\n"
                         << "    \"replaced\": " << engine->getReplacedFieldRowCount() << ",\n"
                         << "    \"duplicates\": " << engine->getDuplicateMergedCount() << "\n"
                         << "  }\n"
                         << "}";
                } else {
                    json << "{\n"
                         << "  \"success\": false,\n"
                         << "  \"error\": \"" << jsonEscape(errorMsg) << "\"\n"
                         << "}";
                }

                responseBody = json.str();
                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/query-user") {
                std::string userId = getQueryParam(query, "userId");
                std::string loadedFile = getQueryParam(query, "loadedFile");

                uint64_t startTime = 0;
                uint64_t endTime = UINT64_MAX;
                std::string timeError;
                Vector<QueryRow> results;
                std::stringstream json;

                if (!parseTimeRange(query, startTime, endTime, timeError)) {
                    responseBody = jsonError(timeError);
                } else {
                    if (!loadedFile.empty()) {
                        Halo* engine = ensureWorkspaceLoaded(loadedFile);
                        if (engine != nullptr) {
                            engine->queryUserJourney(userId, startTime, endTime, results);
                        }
                    }

                    json << "{\n"
                         << "  \"success\": true,\n"
                         << "  \"userId\": \"" << jsonEscape(userId) << "\",\n"
                         << "  \"count\": " << results.size() << ",\n"
                         << "  \"data\": [\n";

                    for (size_t i = 0; i < results.size(); i++) {
                        json << "    {\n"
                             << "      \"timestamp\": " << results[i].timestamp << ",\n"
                             << "      \"device\": \"" << jsonEscape(results[i].device) << "\",\n"
                             << "      \"app\": \"" << jsonEscape(results[i].app) << "\",\n"
                             << "      \"resource\": \"" << jsonEscape(results[i].resource) << "\",\n"
                             << "      \"eventType\": \"" << jsonEscape(results[i].eventType) << "\",\n"
                             << "      \"location\": \"" << jsonEscape(results[i].location) << "\",\n"
                             << "      \"count\": " << results[i].count << "\n"
                             << "    }" << (i == results.size() - 1 ? "" : ",") << "\n";
                    }
                    json << "  ]\n}";
                    responseBody = json.str();
                }

                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/query-resource") {
                std::string resourceId = getQueryParam(query, "resourceId");
                std::string loadedFile = getQueryParam(query, "loadedFile");

                uint64_t startTime = 0;
                uint64_t endTime = UINT64_MAX;
                std::string timeError;
                Vector<QueryRow> results;
                std::stringstream json;

                if (!parseTimeRange(query, startTime, endTime, timeError)) {
                    responseBody = jsonError(timeError);
                } else {
                    if (!loadedFile.empty()) {
                        Halo* engine = ensureWorkspaceLoaded(loadedFile);
                        if (engine != nullptr) {
                            engine->queryResourceAccess(resourceId, startTime, endTime, results);
                        }
                    }

                    json << "{\n"
                         << "  \"success\": true,\n"
                         << "  \"resourceId\": \"" << jsonEscape(resourceId) << "\",\n"
                         << "  \"count\": " << results.size() << ",\n"
                         << "  \"data\": [\n";

                    for (size_t i = 0; i < results.size(); i++) {
                        json << "    {\n"
                             << "      \"timestamp\": " << results[i].timestamp << ",\n"
                             << "      \"user\": \"" << jsonEscape(results[i].user) << "\",\n"
                             << "      \"device\": \"" << jsonEscape(results[i].device) << "\",\n"
                             << "      \"app\": \"" << jsonEscape(results[i].app) << "\",\n"
                             << "      \"eventType\": \"" << jsonEscape(results[i].eventType) << "\",\n"
                             << "      \"location\": \"" << jsonEscape(results[i].location) << "\",\n"
                             << "      \"count\": " << results[i].count << "\n"
                             << "    }" << (i == results.size() - 1 ? "" : ",") << "\n";
                    }
                    json << "  ]\n}";
                    responseBody = json.str();
                }

                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/query-top") {
                std::string loadedFile = getQueryParam(query, "loadedFile");

                uint64_t startTime = 0;
                uint64_t endTime = UINT64_MAX;
                std::string timeError;
                Vector<string> resourceNames;
                Vector<uint64_t> accessCounts;
                std::stringstream json;

                if (!parseTimeRange(query, startTime, endTime, timeError)) {
                    responseBody = jsonError(timeError);
                } else {
                    if (!loadedFile.empty()) {
                        Halo* engine = ensureWorkspaceLoaded(loadedFile);
                        if (engine != nullptr) {
                            engine->queryTopResources(startTime, endTime, resourceNames, accessCounts);
                        }
                    }

                    json << "{\n"
                         << "  \"success\": true,\n"
                         << "  \"count\": " << resourceNames.size() << ",\n"
                         << "  \"data\": [\n";

                    for (size_t i = 0; i < resourceNames.size(); i++) {
                        json << "    {\n"
                             << "      \"resource\": \"" << jsonEscape(resourceNames[i]) << "\",\n"
                             << "      \"count\": " << accessCounts[i] << "\n"
                             << "    }" << (i == resourceNames.size() - 1 ? "" : ",") << "\n";
                    }
                    json << "  ]\n}";
                    responseBody = json.str();
                }

                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/anomalies") {
                std::string loadedFile = getQueryParam(query, "loadedFile");
                std::string type = getQueryParam(query, "type");
                std::string output = getQueryParam(query, "output");
                if (output.empty()) {
                    output = "screen";
                }

                Halo* engine = nullptr;
                if (!loadedFile.empty()) {
                    engine = ensureWorkspaceLoaded(loadedFile);
                }

                if (engine == nullptr) {
                    responseBody = jsonError("Workspace chua duoc nap hoac cache .dat khong hop le.");
                } else if (type.empty()) {
                    responseBody = jsonError("Chua chon loai bat thuong.");
                } else {
                    AnomalyParams params;
                    params.type = type;
                    params.n = getUintParam(query, "n", params.n);
                    params.windowSec = getUintParam(query, "windowSec", params.windowSec);
                    params.minGapSec = getUintParam(query, "minGapSec", params.minGapSec);
                    params.minSpacingSec = getUintParam(query, "minSpacingSec", params.minSpacingSec);
                    params.minEvents = getUintParam(query, "minEvents", params.minEvents);
                    params.maxCvPercent = getUintParam(query, "maxCvPercent", params.maxCvPercent);
                    params.failureRatioPercent =
                        getUintParam(query, "failureRatioPercent",
                                     params.failureRatioPercent);
                    params.startHour = getIntParam(query, "startHour", params.startHour);
                    params.endHour = getIntParam(query, "endHour", params.endHour);
                    params.sessionSec = getUintParam(query, "sessionSec", params.sessionSec);
                    params.maxDurationSec = getUintParam(query, "maxDurationSec", params.maxDurationSec);
                    params.silenceSec = getUintParam(query, "silenceSec", params.silenceSec);
                    params.burstSec = getUintParam(query, "burstSec", params.burstSec);
                    params.coveragePercent =
                        getUintParam(query, "coveragePercent", params.coveragePercent);

                    std::string outputFile = "";
                    std::ofstream fullOutput;
                    try {
                        if (output == "file") {
                            fs::create_directories("./data/anomaly_results");
                            outputFile = "./data/anomaly_results/anomaly_" + type + "_" +
                                         std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".jsonl";
                            fullOutput.open(outputFile, std::ios::trunc);
                            if (!fullOutput.is_open()) {
                                throw std::runtime_error("Khong the mo file de ghi ket qua: " + outputFile);
                            }
                        }

                        Vector<AnomalyResult> results;
                        int maxResults = 500;
                        auto start = std::chrono::high_resolution_clock::now();
                        uint64_t total = engine->detectAnomalies(
                            params, results, maxResults, fullOutput.is_open() ? &fullOutput : nullptr);
                        if (fullOutput.is_open()) {
                            fullOutput.close();
                        }
                        auto end = std::chrono::high_resolution_clock::now();
                        double timeMs = std::chrono::duration<double, std::milli>(end - start).count();

                        std::stringstream json;
                        json << "{\n"
                             << "  \"success\": true,\n"
                             << "  \"type\": \"" << jsonEscape(type) << "\",\n"
                             << "  \"totalCount\": " << total << ",\n"
                             << "  \"previewCount\": " << results.size() << ",\n"
                             << "  \"outputFile\": \"" << jsonEscape(outputFile) << "\",\n"
                             << "  \"summary\": \"Detected " << total << " anomalies in " << timeMs << " ms\",\n"
                             << "  \"timeMs\": " << timeMs << ",\n"
                             << "  \"data\": [\n";
                        for (size_t i = 0; i < results.size(); i++) {
                            json << "    {\n"
                                 << "      \"type\": \"" << jsonEscape(results[i].type) << "\",\n"
                                 << "      \"severity\": \"" << jsonEscape(results[i].severity) << "\",\n"
                                 << "      \"user\": \"" << jsonEscape(results[i].user) << "\",\n"
                                 << "      \"device\": \"" << jsonEscape(results[i].device) << "\",\n"
                                 << "      \"app\": \"" << jsonEscape(results[i].app) << "\",\n"
                                 << "      \"resource\": \"" << jsonEscape(results[i].resource) << "\",\n"
                                 << "      \"location\": \"" << jsonEscape(results[i].location) << "\",\n"
                                 << "      \"timestamp\": " << results[i].timestamp << ",\n"
                                 << "      \"count\": " << results[i].count << ",\n"
                                 << "      \"detail\": \"" << jsonEscape(results[i].detail) << "\"";
                            if (!results[i].records.empty()) {
                                json << ",\n      \"records\": [\n";
                                for (size_t j = 0; j < results[i].records.size(); j++) {
                                    json << "        {\n"
                                         << "          \"eventType\": \"" << jsonEscape(results[i].records[j].eventType) << "\",\n"
                                         << "          \"user\": \"" << jsonEscape(results[i].records[j].user) << "\",\n"
                                         << "          \"device\": \"" << jsonEscape(results[i].records[j].device) << "\",\n"
                                         << "          \"app\": \"" << jsonEscape(results[i].records[j].app) << "\",\n"
                                         << "          \"resource\": \"" << jsonEscape(results[i].records[j].resource) << "\",\n"
                                         << "          \"location\": \"" << jsonEscape(results[i].records[j].location) << "\",\n"
                                         << "          \"timestamp\": " << results[i].records[j].timestamp << ",\n"
                                         << "          \"timestampUtc\": \"" << jsonEscape(formatUtcEpochText(results[i].records[j].timestamp)) << "\"\n"
                                         << "        }" << (j == results[i].records.size() - 1 ? "" : ",") << "\n";
                                }
                                json << "      ]\n";
                            } else {
                                json << "\n";
                            }
                            json << "    }" << (i == results.size() - 1 ? "" : ",") << "\n";
                        }
                        json << "  ]\n}";
                        responseBody = json.str();
                    } catch (const std::exception& e) {
                        if (fullOutput.is_open()) {
                            fullOutput.close();
                        }
                        responseBody = jsonError(std::string("Loi he thong: ") + e.what());
                    } catch (...) {
                        if (fullOutput.is_open()) {
                            fullOutput.close();
                        }
                        responseBody = jsonError("Loi he thong khong xac dinh.");
                    }
                }

                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/list-anomaly-files") {
                std::stringstream json;
                json << "[";
                bool first = true;
                const fs::path resultDirectory = "./data/anomaly_results";

                try {
                    if (fs::exists(resultDirectory) && fs::is_directory(resultDirectory)) {
                        for (const auto& entry : fs::directory_iterator(resultDirectory)) {
                            const std::string filename = entry.path().filename().string();
                            const bool validName =
                                filename.rfind("anomaly_", 0) == 0 &&
                                entry.path().extension() == ".jsonl";
                            if (!entry.is_regular_file() || !validName) {
                                continue;
                            }
                            if (!first) {
                                json << ",";
                            }
                            json << "\"" << jsonEscape(filename) << "\"";
                            first = false;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Loi khi list anomaly files: " << e.what() << "\n";
                }

                json << "]";
                responseBody = json.str();
                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/get-anomaly-file-content") {
                const std::string filename = getQueryParam(query, "file");
                const bool validName =
                    !filename.empty() &&
                    filename.find('/') == std::string::npos &&
                    filename.find('\\') == std::string::npos &&
                    filename.rfind("anomaly_", 0) == 0 &&
                    fs::path(filename).extension() == ".jsonl";

                if (!validName) {
                    responseBody = jsonError("Ten file anomaly khong hop le.");
                } else {
                    const fs::path filePath = fs::path("./data/anomaly_results") / filename;
                    std::ifstream input(filePath);
                    if (!input.is_open()) {
                        responseBody = jsonError("Khong tim thay file anomaly.");
                    } else {
                        std::stringstream content;
                        std::string line = "";
                        int lineCount = 0;
                        bool truncated = false;
                        while (std::getline(input, line)) {
                            if (lineCount >= 500) {
                                truncated = true;
                                break;
                            }
                            content << line << "\n";
                            lineCount++;
                        }

                        std::stringstream json;
                        json << "{"
                             << "\"success\":true,"
                             << "\"filename\":\"" << jsonEscape(filename) << "\","
                             << "\"previewCount\":" << lineCount << ","
                             << "\"truncated\":" << (truncated ? "true" : "false") << ","
                             << "\"content\":\"" << jsonEscape(content.str()) << "\""
                             << "}";
                        responseBody = json.str();
                    }
                }

                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else if (path == "/api/stats") {
                std::string loadedFile = getQueryParam(query, "loadedFile");
                std::stringstream json;
                Halo* engine = nullptr;
                if (!loadedFile.empty()) {
                    engine = ensureWorkspaceLoaded(loadedFile);
                }

                if (engine != nullptr) {
                    json << "{\n"
                         << "  \"users\": " << engine->getUserCount() << ",\n"
                         << "  \"devices\": " << engine->getDeviceCount() << ",\n"
                         << "  \"apps\": " << engine->getAppCount() << ",\n"
                         << "  \"resources\": " << engine->getResourceCount() << ",\n"
                         << "  \"records\": " << engine->getRecordCount() << ",\n"
                         << "  \"totalRead\": " << engine->getTotalReadCount() << ",\n"
                         << "  \"skipped\": " << engine->getSkippedRowCount() << ",\n"
                         << "  \"replaced\": " << engine->getReplacedFieldRowCount() << ",\n"
                         << "  \"duplicates\": " << engine->getDuplicateMergedCount() << "\n"
                         << "}";
                } else {
                    json << "{\n"
                         << "  \"users\": 0,\n"
                         << "  \"devices\": 0,\n"
                         << "  \"apps\": 0,\n"
                         << "  \"resources\": 0,\n"
                         << "  \"records\": 0,\n"
                         << "  \"totalRead\": 0,\n"
                         << "  \"skipped\": 0,\n"
                         << "  \"replaced\": 0,\n"
                         << "  \"duplicates\": 0\n"
                         << "}";
                }

                responseBody = json.str();
                responseHeader =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            } else {
                responseBody = "{\"error\": \"Khong tim thay endpoint\"}";
                responseHeader =
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Content-Length: " +
                    std::to_string(responseBody.length()) +
                    "\r\n"
                    "Connection: close\r\n\r\n";
            }

            // Send response back to the browser
            clientSocket.sendAll(responseHeader.c_str(), responseHeader.length());
            clientSocket.sendAll(responseBody.c_str(), responseBody.length());
        }

        // Graceful shutdown to prevent TCP reset/hang issues
        clientSocket.drainAndClose();
    }

    // 6. Cleanup workspaces to prevent memory leaks
    for (size_t i = 0; i < workspaces.size(); i++) {
        delete workspaces[i];
    }

    return 0;
}
