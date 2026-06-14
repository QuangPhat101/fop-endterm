#pragma once
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
using std::string;

struct Node {
    string key;
    int value;
    Node* next;
};

class HashTable {
   private:
    Node** buckets;
    size_t bucketCount;
    size_t itemCount;

    uint64_t hashString(const string& key) const;
    size_t bucketIndex(const string& key) const;
    void initBuckets();
    void rehash(size_t newBucketCount);
    size_t nextBucketCount(size_t current) const;

   public:
    explicit HashTable(size_t bucketCount = 262144);
    ~HashTable();

    HashTable(const HashTable& other) = delete;
    HashTable& operator=(const HashTable& other) = delete;

    bool find(const string& key, int& value) const;
    void insert(const string& key, int value);
    void clear();
    size_t size() const {
        return itemCount;
    }
};
