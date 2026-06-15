#pragma once
#include <cstddef>
#include <string>
#include <string_view>

#include "HashTable.h"
#include "Vector.h"
using std::string;

class IdTable {
   private:
    static constexpr size_t MAX_DIRECT_ID = 8 * 1024 * 1024;

    Vector<string> names;
    HashTable index;
    string numericPrefix;
    Vector<int> numericIndex;

    bool parseNumericKey(std::string_view id, size_t& numericKey) const;
    void setNumericMapping(size_t numericKey, int internalId);
    void rebuildIndexes();

   public:
    explicit IdTable(std::string_view prefix = {});
    ~IdTable();

    const Vector<string>& getNames() const;
    void setNames(const Vector<string>& newNames);
    void setNames(Vector<string>&& newNames);

    int getOrAdd(std::string_view name);
    int findId(std::string_view name) const;
    string getName(int id) const;
    size_t size() const;
    void reserve(size_t expectedUnique);
    void shrinkToFit();
    void clear();
};
