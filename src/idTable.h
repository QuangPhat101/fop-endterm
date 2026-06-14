#pragma once
#include <string>

#include "HashTable.h"
#include "Vector.h"
using std::string;

class IdTable {
   private:
    Vector<string> names;
    HashTable index;

   public:
    IdTable();
    ~IdTable();

    const Vector<string>& getNames() const;
    void setNames(const Vector<string>& newNames);
    void setNames(Vector<string>&& newNames);

    int getOrAdd(const string& name);
    int findId(const string& name) const;
    string getName(int id) const;
    int size() const;
    void shrinkToFit();
    void clear();
};
