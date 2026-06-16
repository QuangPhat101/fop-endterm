#include "HashTable.h"

#include <limits>
#include <stdexcept>

HashTable::HashTable(size_t initialSlotCount) {
    slotCount = initialSlotCount < 16 ? 16 : initialSlotCount;
    slotCount = nextSlotCount(slotCount - 1);
    itemCount = 0;
    slots = NULL;
}

HashTable::~HashTable() {
    clear();
}

void HashTable::initSlots() {
    if (slots != NULL) {
        return;
    }
    slots = new Slot[slotCount];
}

uint64_t HashTable::hashString(std::string_view key) const {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < key.length(); i++) {
        hash ^= static_cast<unsigned char>(key[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t HashTable::mixHash(uint64_t hash) const {
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return hash;
}

size_t HashTable::slotIndex(uint64_t hash) const {
    return static_cast<size_t>(mixHash(hash) & static_cast<uint64_t>(slotCount - 1));
}

size_t HashTable::probeStep(uint64_t hash) const {
    uint64_t mixed = mixHash(hash ^ 0x9e3779b97f4a7c15ULL);
    return static_cast<size_t>((mixed | 1ULL) & static_cast<uint64_t>(slotCount - 1));
}

size_t HashTable::nextSlotCount(size_t current) const {
    size_t next = 16;
    while (next <= current) {
        if (next > std::numeric_limits<size_t>::max() / 2) {
            throw std::length_error("HashTable qua lon");
        }
        next *= 2;
    }
    return next;
}

size_t HashTable::slotCountForItems(size_t expectedItems) const {
    if (expectedItems == 0) {
        return 16;
    }
    if (expectedItems > (std::numeric_limits<size_t>::max() - 9) / 10) {
        throw std::length_error("HashTable reserve qua lon");
    }
    size_t target = (expectedItems * 10 + 6) / 7;
    if (target < 16) {
        target = 16;
    }
    return nextSlotCount(target - 1);
}

void HashTable::rehash(size_t newSlotCount) {
    initSlots();
    Slot* oldSlots = slots;
    size_t oldSlotCount = slotCount;

    slotCount = newSlotCount < 16 ? 16 : newSlotCount;
    slotCount = nextSlotCount(slotCount - 1);
    slots = new Slot[slotCount];

    for (size_t i = 0; i < oldSlotCount; i++) {
        if (oldSlots[i].occupied) {
            size_t index = slotIndex(oldSlots[i].hash);
            size_t step = probeStep(oldSlots[i].hash);
            while (slots[index].occupied) {
                index = (index + step) & (slotCount - 1);
            }
            slots[index] = oldSlots[i];
        }
    }

    delete[] oldSlots;
}

bool HashTable::find(std::string_view key, const Vector<string>& names,
                     int& value) const {
    if (slots == NULL || itemCount == 0) {
        return false;
    }

    uint64_t hash = hashString(key);
    size_t index = slotIndex(hash);
    size_t step = probeStep(hash);

    for (size_t probe = 0; probe < slotCount; probe++) {
        const Slot& slot = slots[index];
        if (!slot.occupied) {
            return false;
        }
        if (slot.hash == hash && slot.value >= 0 &&
            static_cast<size_t>(slot.value) < names.size() &&
            std::string_view(names[static_cast<size_t>(slot.value)]) == key) {
            value = slot.value;
            return true;
        }
        index = (index + step) & (slotCount - 1);
    }
    return false;
}

void HashTable::insert(std::string_view key, const Vector<string>& names,
                       int value) {
    initSlots();
    if ((itemCount + 1) * 10 > slotCount * 7) {
        rehash(nextSlotCount(slotCount));
    }

    uint64_t hash = hashString(key);
    size_t index = slotIndex(hash);
    size_t step = probeStep(hash);

    for (size_t probe = 0; probe < slotCount; probe++) {
        Slot& slot = slots[index];
        if (!slot.occupied) {
            slot.hash = hash;
            slot.value = value;
            slot.occupied = true;
            itemCount++;
            return;
        }
        if (slot.hash == hash && slot.value >= 0 &&
            static_cast<size_t>(slot.value) < names.size() &&
            std::string_view(names[static_cast<size_t>(slot.value)]) == key) {
            slot.value = value;
            return;
        }
        index = (index + step) & (slotCount - 1);
    }

    rehash(nextSlotCount(slotCount));
    insert(key, names, value);
}

void HashTable::reserve(size_t expectedItems) {
    size_t newSlotCount = slotCountForItems(expectedItems);
    if (newSlotCount <= slotCount) {
        return;
    }
    if (slots == NULL) {
        slotCount = newSlotCount;
    } else {
        rehash(newSlotCount);
    }
}

void HashTable::clear() {
    delete[] slots;
    slots = NULL;
    itemCount = 0;
}
