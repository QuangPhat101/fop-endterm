#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "Halo.h"

namespace {
void addEvent(Halo& halo, const std::string& user, event_Type event,
              location loc, uint64_t timestamp) {
    DataRecords rec;
    rec.eventTypeTag = event;
    rec.locationTag = loc;
    rec.timestamp = timestamp;
    halo.processRecord(rec, user, "device-1", "app-1", "resource-1");
}

uint64_t detect(Halo& halo, AnomalyParams params, Vector<AnomalyResult>& results) {
    return halo.detectAnomalies(params, results, 100);
}

void testImpossibleTravelGrouping() {
    Halo halo;
    addEvent(halo, "traveler", LOGIN, LOC_VN, 1000);
    addEvent(halo, "traveler", LOGIN, LOC_US, 4600);
    addEvent(halo, "border-user", LOGIN, LOC_US, 1000);
    addEvent(halo, "border-user", LOGIN, LOC_CA, 1001);
    halo.finalizeLoading();

    AnomalyParams params;
    params.type = "A5";
    Vector<AnomalyResult> results;
    assert(detect(halo, params, results) == 1);
    assert(results.size() == 1);
    assert(results[0].records.size() == 2);
    assert(results[0].records[0].location == "VN");
    assert(results[0].records[1].location == "US");
    assert(results[0].timestamp == 1000);
    assert(results[0].endTimestamp == 4600);
    assert(results[0].detail.find("Không khả thi") != std::string::npos);
    assert(results[0].detail.find("km") != std::string::npos);
}

void testConsecutiveFailedLoginGrouping() {
    Halo halo;
    for (uint64_t i = 0; i < 5; i++) {
        addEvent(halo, "failed-user", FAILED_LOGIN, LOC_VN, 100 + i * 10);
    }
    halo.finalizeLoading();

    AnomalyParams params;
    params.type = "A2";
    params.n = 5;
    Vector<AnomalyResult> results;
    assert(detect(halo, params, results) == 1);
    assert(results.size() == 1);
    assert(results[0].count == 5);
    assert(results[0].records.size() == 5);
    assert(results[0].detail.find("5") != std::string::npos);
    assert(results[0].detail.find("N=5") != std::string::npos);
}

void testConsecutiveLoginGrouping() {
    Halo halo;
    for (uint64_t i = 0; i < 5; i++) {
        addEvent(halo, "login-user", LOGIN, LOC_VN, 100 + i * 10);
    }
    halo.finalizeLoading();

    AnomalyParams params;
    params.type = "A8";
    params.n = 5;
    params.windowSec = 3600;
    Vector<AnomalyResult> results;
    assert(detect(halo, params, results) == 1);
    assert(results[0].count == 5);
    assert(results[0].records.size() == 5);
    assert(results[0].detail.find("5") != std::string::npos);
}

void testSpeedAnomaly() {
    Halo halo;
    for (uint64_t i = 0; i < 6; i++) {
        addEvent(halo, "speed-bot", ACCESS, LOC_VN, 100 + i);
    }
    halo.finalizeLoading();

    AnomalyParams params;
    params.type = "D2";
    params.minGapSec = 2;
    params.n = 5;
    Vector<AnomalyResult> results;
    assert(detect(halo, params, results) == 1);
    assert(results[0].records.size() == 6);
    assert(results[0].detail.find("5") != std::string::npos);
}

void testRobotTiming() {
    Halo halo;
    for (uint64_t i = 0; i < 6; i++) {
        addEvent(halo, "cron-user", ACCESS, LOC_VN, i * 60);
    }
    const uint64_t irregular[] = {0, 10, 100, 120, 1000, 1010};
    for (uint64_t timestamp : irregular) {
        addEvent(halo, "human-user", ACCESS, LOC_VN, timestamp);
    }
    halo.finalizeLoading();

    AnomalyParams params;
    params.type = "D8";
    params.minEvents = 6;
    params.maxCvPercent = 10;
    Vector<AnomalyResult> results;
    assert(detect(halo, params, results) == 1);
    assert(results[0].records.size() == 6);
}

void testLowAndSlowBruteForce() {
    Halo halo;
    for (uint64_t day = 0; day < 5; day++) {
        addEvent(halo, "slow-attacker", FAILED_LOGIN, LOC_US, day * 86400ULL);
    }
    halo.finalizeLoading();

    AnomalyParams params;
    params.type = "C4";
    params.n = 5;
    params.minSpacingSec = 43200;
    params.windowSec = 7 * 86400ULL;
    Vector<AnomalyResult> results;
    assert(detect(halo, params, results) == 1);
    assert(results[0].records.size() == 5);
    assert(results[0].detail.find("5") != std::string::npos);
}

void testLocationFailureConcentration() {
    Halo halo;
    for (uint64_t i = 0; i < 9; i++) {
        addEvent(halo, "vn-user-" + std::to_string(i), FAILED_LOGIN, LOC_VN, 100 + i);
    }
    addEvent(halo, "vn-success", LOGIN, LOC_VN, 110);
    addEvent(halo, "small-sample-1", FAILED_LOGIN, LOC_US, 100);
    addEvent(halo, "small-sample-2", FAILED_LOGIN, LOC_US, 101);
    halo.finalizeLoading();

    AnomalyParams params;
    params.type = "D10";
    params.windowSec = 3600;
    params.minEvents = 10;
    params.failureRatioPercent = 90;
    Vector<AnomalyResult> results;
    assert(detect(halo, params, results) == 1);
    assert(results[0].records.size() == 10);
    assert(results[0].detail.find("9/10") != std::string::npos);
}
}  // namespace

int main() {
    testImpossibleTravelGrouping();
    testConsecutiveFailedLoginGrouping();
    testConsecutiveLoginGrouping();
    testSpeedAnomaly();
    testRobotTiming();
    testLowAndSlowBruteForce();
    testLocationFailureConcentration();
    std::cout << "All anomaly tests passed.\n";
    return 0;
}
