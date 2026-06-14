#include "HashTable.h"

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

uint64_t HashTable::hashString(const string& key) const {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < key.length(); i++) {
        hash ^= static_cast<unsigned char>(key[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

size_t HashTable::bucketIndex(const string& key) const {
    return static_cast<size_t>(hashString(key) % bucketCount);
}

size_t HashTable::nextBucketCount(size_t current) const {
    const size_t sizes[] = {262144, 524288, 1048576, 2097152, 4194304,
                            8388608, 16777216, 33554432, 67108864};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if (sizes[i] > current) {
            return sizes[i];
        }
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
            size_t index = bucketIndex(node->key);
            node->next = buckets[index];
            buckets[index] = node;
            node = next;
        }
    }

    delete[] oldBuckets;
}

bool HashTable::find(const string& key, int& value) const {
    if (buckets == NULL) {
        return false;
    }

    size_t index = static_cast<size_t>(hashString(key) % bucketCount);
    Node* node = buckets[index];
    while (node != NULL) {
        if (node->key == key) {
            value = node->value;
            return true;
        }
        node = node->next;
    }
    return false;
}

void HashTable::insert(const string& key, int value) {
    initBuckets();
    if ((itemCount + 1) * 4 > bucketCount * 3) {
        rehash(nextBucketCount(bucketCount));
    }

    size_t index = bucketIndex(key);
    Node* node = buckets[index];
    while (node != NULL) {
        if (node->key == key) {
            node->value = value;
            return;
        }
        node = node->next;
    }

    Node* newNode = new Node();
    newNode->key = key;
    newNode->value = value;
    newNode->next = buckets[index];
    buckets[index] = newNode;
    itemCount++;
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
