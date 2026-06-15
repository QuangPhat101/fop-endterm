#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

template <typename T>
class Vector {
   private:
    T* data;
    size_t count;
    size_t capacity;

    void resize(size_t newCapa) {
        T* newData = new T[newCapa];
        for (size_t i = 0; i < count; i++) {
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
    Vector(size_t capa) {
        capacity = capa;
        count = 0;
        data = (capacity > 0) ? new T[capacity] : nullptr;
    }
    Vector(int capa) {
        capacity = (capa > 0) ? static_cast<size_t>(capa) : 0;
        count = 0;
        data = (capacity > 0) ? new T[capacity] : nullptr;
    }
    Vector(const Vector& other) {
        count = other.count;
        capacity = other.capacity;
        data = (capacity > 0) ? new T[capacity] : nullptr;
        for (size_t i = 0; i < count; i++) {
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
        for (size_t i = 0; i < other.count; i++) {
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
            if (capacity > std::numeric_limits<size_t>::max() / 2) {
                throw std::length_error("Vector capacity overflow");
            }
            size_t newCapa = (capacity == 0) ? 10 : capacity * 2;
            resize(newCapa);
        }
        data[count] = value;
        count++;
    }
    void pushBack(T&& value) {
        if (count == capacity) {
            if (capacity > std::numeric_limits<size_t>::max() / 2) {
                throw std::length_error("Vector capacity overflow");
            }
            size_t newCapa = (capacity == 0) ? 10 : capacity * 2;
            resize(newCapa);
        }
        data[count] = std::move(value);
        count++;
    }
    T& operator[](size_t index) {
        return data[index];
    }

    const T& operator[](size_t index) const {
        return data[index];
    }

    size_t size() const {
        return count;
    }

    bool empty() const {
        return count == 0;
    }

    size_t getCapacity() const {
        return capacity;
    }

    T* rawData() {
        return data;
    }

    const T* rawData() const {
        return data;
    }

    void reserve(size_t newCapa) {
        if (newCapa > capacity) {
            resize(newCapa);
        }
    }
    void reserve(int newCapa) {
        if (newCapa > 0) {
            reserve(static_cast<size_t>(newCapa));
        }
    }
    void setSize(size_t newSize) {
        if (newSize > capacity) {
            resize(newSize);
        }
        count = newSize;
    }
    void setSize(int newSize) {
        setSize(newSize > 0 ? static_cast<size_t>(newSize) : 0);
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
        for (size_t i = 0; i < count; i++) {
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
