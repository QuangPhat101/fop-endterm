#pragma once
#include <cstdint>
#include <iostream>

enum event_Type : uint8_t {
    LOGIN,
    LOGOUT,
    TOKEN_REFRESH,
    ACCESS,
    FAILED_LOGIN,
    OPEN_APP,
    DOWNLOAD,
    ADMIN_ACTION,
    UNKNOWN_EVENT
};

enum location : uint8_t {
    LOC_US,
    LOC_VN,
    LOC_JP,
    LOC_KR,
    LOC_SG,
    LOC_CN,
    LOC_DE,
    LOC_FR,
    LOC_UK,
    LOC_AU,
    LOC_CA,
    LOC_IN,
    LOC_BR,
    LOC_RU,
    LOC_TH,
    UNKNOWN_LOCATION
};

struct DataRecords {
    int userID = -1;
    int deviceID = -1;
    int appID = -1;
    int resourceID = -1;
    uint32_t Count = 1;
    event_Type eventTypeTag = UNKNOWN_EVENT;
    location locationTag = UNKNOWN_LOCATION;
    uint64_t timestamp = 0;

    bool operator<=(const DataRecords& other) const {
        if (timestamp != other.timestamp) {
            return timestamp < other.timestamp;
        }
        if (userID != other.userID) {
            return userID < other.userID;
        }
        if (deviceID != other.deviceID) {
            return deviceID < other.deviceID;
        }
        if (appID != other.appID) {
            return appID < other.appID;
        }
        if (resourceID != other.resourceID) {
            return resourceID < other.resourceID;
        }
        if (eventTypeTag != other.eventTypeTag) {
            return eventTypeTag < other.eventTypeTag;
        }
        return locationTag <= other.locationTag;
    }

    bool operator==(const DataRecords& other) const {
        return timestamp == other.timestamp && userID == other.userID &&
               deviceID == other.deviceID && appID == other.appID &&
               resourceID == other.resourceID && eventTypeTag == other.eventTypeTag &&
               locationTag == other.locationTag;
    }
};

static_assert(sizeof(DataRecords) == 32, "DataRecords layout must stay compact");
