#pragma once
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "Vector.h"
using std::string;

class HashTable {
   private:
    struct Slot {
        uint64_t hash = 0;
        int value = -1;
        bool occupied = false;
    };

    Slot* slots;
    size_t slotCount;
    size_t itemCount;

    uint64_t hashString(std::string_view key) const;
    uint64_t mixHash(uint64_t hash) const;
    size_t slotIndex(uint64_t hash) const;
    size_t probeStep(uint64_t hash) const;
    void initSlots();
    void rehash(size_t newSlotCount);
    size_t nextSlotCount(size_t current) const;
    size_t slotCountForItems(size_t expectedItems) const;

   public:
    explicit HashTable(size_t initialSlotCount = 262144);
    ~HashTable();

    HashTable(const HashTable& other) = delete;
    HashTable& operator=(const HashTable& other) = delete;

    bool find(std::string_view key, const Vector<string>& names,
              int& value) const;
    void insert(std::string_view key, const Vector<string>& names, int value);
    void reserve(size_t expectedItems);
    void clear();
    size_t size() const {
        return itemCount;
    }
};
