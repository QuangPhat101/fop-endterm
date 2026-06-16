CXX ?= g++
STD ?= -std=c++23
OPT ?= -O2 -DNDEBUG -ffunction-sections -fdata-sections
WARN ?= -Wall -Wextra
SRC := $(wildcard src/*.cpp)
TEST_SRC := tests/anomaly_tests.cpp src/Halo.cpp src/RecordStorage.cpp src/idTable.cpp src/HashTable.cpp src/Platform.cpp

ifeq ($(OS),Windows_NT)
MKDIR_RELEASE := if not exist release mkdir release
RM_RELEASE := if exist release\halo del /Q release\halo & if exist release\24120117.exe del /Q release\24120117.exe & if exist release\anomaly_tests.exe del /Q release\anomaly_tests.exe & if exist release\anomaly_tests_linux del /Q release\anomaly_tests_linux
RUN_WIN_TEST := release\anomaly_tests.exe
else
MKDIR_RELEASE := mkdir -p release
RM_RELEASE := rm -f release/halo release/24120117.exe release/anomaly_tests.exe release/anomaly_tests_linux
RUN_WIN_TEST := ./release/anomaly_tests.exe
endif

.PHONY: all windows linux test-windows test-linux clean release

all: linux

release:
	$(MKDIR_RELEASE)

windows: release
	$(CXX) $(OPT) -Wl,--gc-sections -s $(STD) $(SRC) -o release/24120117.exe -lws2_32 -lpsapi

linux: release
	$(CXX) $(OPT) -Wl,--gc-sections -s $(STD) $(SRC) -o release/24120117.exe

test-windows: release
	$(CXX) -O2 $(WARN) $(STD) -Isrc $(TEST_SRC) -o release/anomaly_tests.exe -lws2_32 -lpsapi
	$(RUN_WIN_TEST)

test-linux: release
	$(CXX) -O2 $(WARN) $(STD) -Isrc $(TEST_SRC) -o release/anomaly_tests_linux
	./release/anomaly_tests_linux

clean:
	$(RM_RELEASE)
