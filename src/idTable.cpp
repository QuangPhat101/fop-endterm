#include "idTable.h"

#include <limits>
#include <stdexcept>
#include <utility>

#include "ParseUtils.h"

IdTable::IdTable(std::string_view prefix) : numericPrefix(prefix) {}
IdTable::~IdTable() {
    index.clear();
}

bool IdTable::parseNumericKey(std::string_view id, size_t& numericKey) const {
    if (numericPrefix.empty() || id.size() <= numericPrefix.size() ||
        id.substr(0, numericPrefix.size()) != numericPrefix) {
        return false;
    }

    std::string_view suffix = id.substr(numericPrefix.size());
    if (suffix.size() > 1 && suffix[0] == '0') {
        return false;
    }

    uint64_t parsed = 0;
    if (!halo::parseUint64Strict(suffix, parsed) ||
        parsed > MAX_DIRECT_ID) {
        return false;
    }
    numericKey = static_cast<size_t>(parsed);
    return true;
}

void IdTable::setNumericMapping(size_t numericKey, int internalId) {
    if (numericKey >= numericIndex.size()) {
        size_t oldSize = numericIndex.size();
        size_t targetSize = oldSize == 0 ? 1024 : oldSize * 2;
        if (targetSize <= numericKey) {
            targetSize = numericKey + 1;
        }
        if (targetSize > MAX_DIRECT_ID + 1) {
            targetSize = MAX_DIRECT_ID + 1;
        }
        numericIndex.setSize(targetSize);
        for (size_t i = oldSize; i < targetSize; i++) {
            numericIndex[i] = -1;
        }
    }
    numericIndex[numericKey] = internalId;
}

void IdTable::rebuildIndexes() {
    index.clear();
    index.reserve(names.size());
    numericIndex.clear();
    if (names.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error("Qua nhieu ID duy nhat cho kieu int noi bo");
    }
    for (size_t i = 0; i < names.size(); i++) {
        size_t numericKey = 0;
        if (parseNumericKey(names[i], numericKey)) {
            setNumericMapping(numericKey, static_cast<int>(i));
        } else {
            index.insert(names[i], names, static_cast<int>(i));
        }
    }
}

int IdTable::getOrAdd(std::string_view id) {
    int existID = -1;
    size_t numericKey = 0;

    if (parseNumericKey(id, numericKey)) {
        if (numericKey < numericIndex.size() && numericIndex[numericKey] >= 0) {
            return numericIndex[numericKey];
        }
    } else if (index.find(id, names, existID)) {
        return existID;
    }
    if (names.size() >= static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error("Qua nhieu ID duy nhat cho kieu int noi bo");
    }
    int newID = static_cast<int>(names.size());
    names.pushBack(string(id));
    if (parseNumericKey(id, numericKey)) {
        setNumericMapping(numericKey, newID);
    } else {
        index.insert(names[static_cast<size_t>(newID)], names, newID);
    }

    return newID;
}
int IdTable::findId(std::string_view id) const {
    int existID = -1;
    size_t numericKey = 0;
    if (parseNumericKey(id, numericKey)) {
        if (numericKey < numericIndex.size()) {
            return numericIndex[numericKey];
        }
        return -1;
    }
    // Tìm kiếm trong HashTable index, nếu thấy trả về mã ID số nguyên tương ứng
    if (index.find(id, names, existID)) {
        return existID;
    }
    // Quy ước: Trả về -1 nếu chuỗi ID không tồn tại trong hệ thống
    return -1;
}

const Vector<string>& IdTable::getNames() const {
    return names;
}

void IdTable::setNames(const Vector<string>& newNames) {
    names = newNames;
    rebuildIndexes();
}

void IdTable::setNames(Vector<string>&& newNames) {
    names = std::move(newNames);
    rebuildIndexes();
}

string IdTable::getName(int internalId) const {
    // Kiểm tra tính hợp lệ của chỉ mục trước khi truy cập mảng để tránh crash hệ thống
    if (internalId >= 0 && static_cast<size_t>(internalId) < names.size()) {
        return names[internalId];  // Gọi toán tử operator[] của class Vector
    }
    // Trả về chuỗi rỗng nếu mã số nội bộ không hợp lệ
    return "";
}
size_t IdTable::size() const {
    return names.size();
}

void IdTable::reserve(size_t expectedUnique) {
    if (expectedUnique > 0) {
        names.reserve(expectedUnique);
    }
    index.reserve(expectedUnique);
}

void IdTable::shrinkToFit() {
    names.shrinkToFit();
}

void IdTable::clear() {
    index.clear();
    numericIndex.clear();
    names.clear();
}
