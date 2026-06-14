#pragma once

#include <utility>

template <typename T>
class Vector {
   private:
    T* data;
    int count;
    int capacity;

    void resize(int newCapa) {
        T* newData = new T[newCapa];
        for (int i = 0; i < count; i++) {
            newData[i] = std::move(data[i]);
        }
        delete[] data;
        data = newData;
        capacity = newCapa;
    }

   public:
    Vector() {
        data = nullptr;
        count = 0;
        capacity = 0;
    }
    Vector(int capa) {
        capacity = capa;
        count = 0;
        data = new T[capacity];
    }
    Vector(const Vector& other) {
        count = other.count;
        capacity = other.capacity;
        data = (capacity > 0) ? new T[capacity] : nullptr;
        for (int i = 0; i < count; i++) {
            data[i] = other.data[i];
        }
    }
    Vector(Vector&& other) noexcept {
        data = other.data;
        count = other.count;
        capacity = other.capacity;
        other.data = nullptr;
        other.count = 0;
        other.capacity = 0;
    }
    ~Vector() {
        if (data != nullptr) {
            delete[] data;
            data = nullptr;
        }
    }
    Vector& operator=(const Vector& other) {
        if (this == &other) {
            return *this;
        }
        T* newData = (other.capacity > 0) ? new T[other.capacity] : nullptr;
        for (int i = 0; i < other.count; i++) {
            newData[i] = other.data[i];
        }
        delete[] data;
        data = newData;
        count = other.count;
        capacity = other.capacity;
        return *this;
    }
    Vector& operator=(Vector&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        delete[] data;
        data = other.data;
        count = other.count;
        capacity = other.capacity;
        other.data = nullptr;
        other.count = 0;
        other.capacity = 0;
        return *this;
    }
    void pushBack(const T& value) {
        if (count == capacity) {
            int newCapa = (capacity == 0) ? 10 : capacity * 2;
            resize(newCapa);
        }
        data[count] = value;
        count++;
    }
    T& operator[](int index) {
        return data[index];
    }

    const T& operator[](int index) const {
        return data[index];
    }

    int size() const {
        return count;
    }

    int getCapacity() const {
        return capacity;
    }

    T* rawData() {
        return data;
    }

    const T* rawData() const {
        return data;
    }

    void reserve(int newCapa) {
        if (newCapa > capacity) {
            resize(newCapa);
        }
    }
    void setSize(int newSize) {
        if (newSize > capacity) {
            resize(newSize);
        }
        count = newSize;
    }
    void shrinkToFit() {
        if (count == 0) {
            clear();
            return;
        }
        if (count < capacity) {
            resize(count);
        }
    }
    void clear() {
        delete[] data;
        data = nullptr;
        count = 0;
        capacity = 0;
    }

   public:
    bool contains(const T& value) const {
        for (int i = 0; i < count; i++) {
            if (data[i] == value) {
                return true;
            }
        }
        return false;
    }
    void pushBackUnique(const T& value) {
        if (!contains(value)) {
            pushBack(value);
            return;
        }
    }
};
