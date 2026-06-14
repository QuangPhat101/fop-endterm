#include "idTable.h"

#include <utility>

IdTable::IdTable() {
    // Do nothing
}
IdTable::~IdTable() {
    index.clear();
}

int IdTable::getOrAdd(const string& id) {
    int existID = -1;

    if (index.find(id, existID)) {
        return existID;
    }
    int newID = names.size();
    names.pushBack(id);
    index.insert(id, newID);

    return newID;
}
int IdTable::findId(const string& id) const {
    int existID = -1;
    // Tìm kiếm trong HashTable index, nếu thấy trả về mã ID số nguyên tương ứng
    if (index.find(id, existID)) {
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
    index.clear();
    for (int i = 0; i < names.size(); i++) {
        index.insert(names[i], i);
    }
}

void IdTable::setNames(Vector<string>&& newNames) {
    names = std::move(newNames);
    index.clear();
    for (int i = 0; i < names.size(); i++) {
        index.insert(names[i], i);
    }
}

string IdTable::getName(int internalId) const {
    // Kiểm tra tính hợp lệ của chỉ mục trước khi truy cập mảng để tránh crash hệ thống
    if (internalId >= 0 && internalId < names.size()) {
        return names[internalId];  // Gọi toán tử operator[] của class Vector
    }
    // Trả về chuỗi rỗng nếu mã số nội bộ không hợp lệ
    return "";
}
int IdTable::size() const {
    return names.size();
}

void IdTable::shrinkToFit() {
    names.shrinkToFit();
}

void IdTable::clear() {
    index.clear();
    names.clear();
}
