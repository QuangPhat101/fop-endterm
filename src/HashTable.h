#pragma once
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "Vector.h"
using std::string;

struct Node {
    uint64_t hash;
    int value;
    Node* next;
};

class HashTable {
   private:
    Node** buckets;
    size_t bucketCount;
    size_t itemCount;

    uint64_t hashString(std::string_view key) const;
    size_t bucketIndex(uint64_t hash) const;
    void initBuckets();
    void rehash(size_t newBucketCount);
    size_t nextBucketCount(size_t current) const;

   public:
    explicit HashTable(size_t bucketCount = 262144);
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
