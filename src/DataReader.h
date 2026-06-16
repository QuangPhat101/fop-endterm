#pragma once
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "Halo.h"

namespace platform {
class LineReader;
}

class DataReader {
   private:
    struct ParsedCsvRow {
        bool countTotal = false;
        bool skipped = false;
        bool replaced = false;
        DataRecords record;
        std::string userId;
        std::string deviceId;
        std::string appId;
        std::string resourceId;
    };

    std::string filePath;
    bool forceParallelParse = false;

    event_Type parseEvent(std::string_view evStr, bool& valid) const;
    location parseLocation(std::string_view locStr, bool& valid) const;

    int splitCSV(std::string_view line, std::string_view* fields, int maxFields) const;
    bool isValidId(std::string_view value, std::string_view prefix) const;
    void parseLineToRow(std::string_view line, ParsedCsvRow& parsed) const;
    bool commitParsedRow(Halo& engine, const ParsedCsvRow& parsed);
    bool processLine(Halo& engine, std::string_view line);
    bool readSomeParallel(Halo& engine, platform::LineReader& file, int maxRows,
                          uint64_t fileSize, long long& nextOffset,
                          int& acceptedRows);

   public:
    explicit DataReader(std::string dataFile, bool parallelCsv = false)
        : filePath(dataFile), forceParallelParse(parallelCsv) {}

    void readAll(Halo& engine);
    bool readSome(Halo& engine, long long startOffset, int maxRows,
                  long long& nextOffset, int& acceptedRows);
};
