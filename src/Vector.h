#pragma once

#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

template <typename T>
class Vector {
   private:
    T* data;
    size_t count;
    size_t capacity;

    static constexpr size_t maxSafeCapacity() {
        return std::numeric_limits<std::ptrdiff_t>::max() / sizeof(T);
    }

    void resize(size_t newCapa) {
        if (newCapa > maxSafeCapacity()) {
            throw std::length_error("Vector allocation too large");
        }
        T* newData = new T[newCapa];
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (count > 0) {
                std::memcpy(newData, data, count * sizeof(T));
            }
        } else {
            for (size_t i = 0; i < count; i++) {
                newData[i] = std::move(data[i]);
            }
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
        if (capa > maxSafeCapacity()) {
            throw std::length_error("Vector allocation too large");
        }
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
        if (other.capacity > maxSafeCapacity()) {
            throw std::length_error("Vector allocation too large");
        }
        count = other.count;
        capacity = other.capacity;
        data = (capacity > 0) ? new T[capacity] : nullptr;
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (count > 0) {
                std::memcpy(data, other.data, count * sizeof(T));
            }
        } else {
            for (size_t i = 0; i < count; i++) {
                data[i] = other.data[i];
            }
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
        if (other.capacity > maxSafeCapacity()) {
            throw std::length_error("Vector allocation too large");
        }
        T* newData = (other.capacity > 0) ? new T[other.capacity] : nullptr;
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (other.count > 0) {
                std::memcpy(newData, other.data, other.count * sizeof(T));
            }
        } else {
            for (size_t i = 0; i < other.count; i++) {
                newData[i] = other.data[i];
            }
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
            if (capacity >= maxSafeCapacity()) {
                throw std::length_error("Vector capacity overflow");
            }
            size_t newCapa = (capacity == 0) ? 10 : capacity * 2;
            if (newCapa > maxSafeCapacity()) {
                newCapa = maxSafeCapacity();
            }
            resize(newCapa);
        }
        data[count] = value;
        count++;
    }
    void pushBack(T&& value) {
        if (count == capacity) {
            if (capacity >= maxSafeCapacity()) {
                throw std::length_error("Vector capacity overflow");
            }
            size_t newCapa = (capacity == 0) ? 10 : capacity * 2;
            if (newCapa > maxSafeCapacity()) {
                newCapa = maxSafeCapacity();
            }
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
