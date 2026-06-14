#pragma once
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "Halo.h"

class DataReader {
   private:
    std::string filePath;

    event_Type parseEvent(std::string_view evStr, bool& valid);
    location parseLocation(std::string_view locStr, bool& valid);

    int splitCSV(std::string_view line, std::string_view* fields, int maxFields);
    bool isValidId(std::string_view value, std::string_view prefix);
    bool processLine(Halo& engine, const std::string& line);

   public:
    explicit DataReader(std::string dataFile) : filePath(dataFile) {}

    void readAll(Halo& engine);
    bool readSome(Halo& engine, long long startOffset, int maxRows,
                  long long& nextOffset, int& acceptedRows);
};
