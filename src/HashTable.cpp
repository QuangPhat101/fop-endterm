#include "HashTable.h"

#include <limits>

HashTable::HashTable(size_t initialBucketCount) {
    bucketCount = initialBucketCount < 16 ? 16 : initialBucketCount;
    itemCount = 0;
    buckets = NULL;
}

HashTable::~HashTable() {
    clear();
}

void HashTable::initBuckets() {
    if (buckets != NULL) {
        return;
    }
    buckets = new Node*[bucketCount];
    for (size_t i = 0; i < bucketCount; i++) {
        buckets[i] = NULL;
    }
}

uint64_t HashTable::hashString(std::string_view key) const {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < key.length(); i++) {
        hash ^= static_cast<unsigned char>(key[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

size_t HashTable::bucketIndex(uint64_t hash) const {
    return static_cast<size_t>(hash % bucketCount);
}

size_t HashTable::nextBucketCount(size_t current) const {
    const size_t sizes[] = {262144, 524288, 1048576, 2097152, 4194304,
                            8388608, 16777216, 33554432, 67108864};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if (sizes[i] > current) {
            return sizes[i];
        }
    }
    if (current > (std::numeric_limits<size_t>::max() - 1) / 2) {
        return std::numeric_limits<size_t>::max();
    }
    return current * 2 + 1;
}

void HashTable::rehash(size_t newBucketCount) {
    initBuckets();
    Node** oldBuckets = buckets;
    size_t oldBucketCount = bucketCount;

    bucketCount = newBucketCount;
    buckets = new Node*[bucketCount];
    for (size_t i = 0; i < bucketCount; i++) {
        buckets[i] = NULL;
    }

    for (size_t i = 0; i < oldBucketCount; i++) {
        Node* node = oldBuckets[i];
        while (node != NULL) {
            Node* next = node->next;
            size_t index = bucketIndex(node->hash);
            node->next = buckets[index];
            buckets[index] = node;
            node = next;
        }
    }

    delete[] oldBuckets;
}

bool HashTable::find(std::string_view key, const Vector<string>& names,
                     int& value) const {
    if (buckets == NULL) {
        return false;
    }

    uint64_t hash = hashString(key);
    size_t index = bucketIndex(hash);
    Node* node = buckets[index];
    while (node != NULL) {
        if (node->hash == hash && node->value >= 0 &&
            static_cast<size_t>(node->value) < names.size() &&
            std::string_view(names[static_cast<size_t>(node->value)]) == key) {
            value = node->value;
            return true;
        }
        node = node->next;
    }
    return false;
}

void HashTable::insert(std::string_view key, const Vector<string>& names,
                       int value) {
    initBuckets();
    if ((itemCount + 1) * 4 > bucketCount * 3) {
        rehash(nextBucketCount(bucketCount));
    }

    uint64_t hash = hashString(key);
    size_t index = bucketIndex(hash);
    Node* node = buckets[index];
    while (node != NULL) {
        if (node->hash == hash && node->value >= 0 &&
            static_cast<size_t>(node->value) < names.size() &&
            std::string_view(names[static_cast<size_t>(node->value)]) == key) {
            node->value = value;
            return;
        }
        node = node->next;
    }

    Node* newNode = new Node();
    newNode->hash = hash;
    newNode->value = value;
    newNode->next = buckets[index];
    buckets[index] = newNode;
    itemCount++;
}

void HashTable::reserve(size_t expectedItems) {
    size_t targetBuckets = 16;
    if (expectedItems > 0) {
        if (expectedItems > (std::numeric_limits<size_t>::max() - 2) / 4) {
            targetBuckets = std::numeric_limits<size_t>::max();
        } else {
            targetBuckets = (expectedItems * 4 + 2) / 3;
        }
        if (targetBuckets < 16) {
            targetBuckets = 16;
        }
    }

    size_t newBucketCount = bucketCount;
    while (newBucketCount < targetBuckets) {
        size_t next = nextBucketCount(newBucketCount);
        if (next <= newBucketCount) {
            newBucketCount = targetBuckets;
            break;
        }
        newBucketCount = next;
    }

    if (newBucketCount <= bucketCount) {
        return;
    }
    if (buckets == NULL) {
        bucketCount = newBucketCount;
    } else {
        rehash(newBucketCount);
    }
}

void HashTable::clear() {
    if (buckets == NULL) {
        itemCount = 0;
        return;
    }
    for (size_t i = 0; i < bucketCount; i++) {
        Node* node = buckets[i];
        while (node != NULL) {
            Node* temp = node;
            node = node->next;
            delete temp;
        }
    }
    delete[] buckets;
    buckets = NULL;
    itemCount = 0;
}
