#pragma once

#include <cstddef>
#include <utility>

#include "Vector.h"

template <typename T>
struct DefaultLessFromLe {
    bool operator()(const T& a, const T& b) const {
        return (a <= b) && !(b <= a);
    }
};

template <typename T, typename Less>
void mergeWithBuffer(Vector<T>& arr, Vector<T>& buffer, size_t left, size_t mid,
                     size_t right, Less less) {
    size_t i = left;
    size_t j = mid + 1;
    size_t k = left;

    while (i <= mid && j <= right) {
        if (less(arr[j], arr[i])) {
            buffer[k] = arr[j];
            j++;
        } else {
            buffer[k] = arr[i];
            i++;
        }
        k++;
    }

    while (i <= mid) {
        buffer[k] = arr[i];
        i++;
        k++;
    }
    while (j <= right) {
        buffer[k] = arr[j];
        j++;
        k++;
    }

    for (size_t idx = left; idx <= right; idx++) {
        arr[idx] = buffer[idx];
    }
}

template <typename T, typename Less>
void mergeSortWithBuffer(Vector<T>& arr, Vector<T>& buffer, size_t left,
                         size_t right, Less less) {
    if (left >= right) {
        return;
    }

    size_t mid = left + (right - left) / 2;
    mergeSortWithBuffer(arr, buffer, left, mid, less);
    mergeSortWithBuffer(arr, buffer, mid + 1, right, less);
    mergeWithBuffer(arr, buffer, left, mid, right, less);
}

template <typename T, typename Less>
void mergeSort(Vector<T>& arr, Less less) {
    if (arr.size() <= 1) {
        return;
    }

    Vector<T> buffer;
    buffer.setSize(arr.size());
    mergeSortWithBuffer(arr, buffer, 0, arr.size() - 1, less);
}

template <typename T>
void mergeSort(Vector<T>& arr) {
    mergeSort(arr, DefaultLessFromLe<T>{});
}

template <typename T, typename Less>
void mergeSort(Vector<T>& arr, int left, int right, Less less) {
    if (left < 0 || right < left) {
        return;
    }

    Vector<T> buffer;
    buffer.setSize(arr.size());
    mergeSortWithBuffer(arr, buffer, static_cast<size_t>(left),
                        static_cast<size_t>(right), less);
}

template <typename T>
void mergeSort(Vector<T>& arr, int left, int right) {
    mergeSort(arr, left, right, DefaultLessFromLe<T>{});
}

template <typename T, typename Less>
void insertionSortRange(Vector<T>& arr, size_t left, size_t right, Less less) {
    if (right <= left) {
        return;
    }
    for (size_t i = left + 1; i <= right; i++) {
        T value = arr[i];
        size_t j = i;
        while (j > left && less(value, arr[j - 1])) {
            arr[j] = arr[j - 1];
            j--;
        }
        arr[j] = value;
    }
}

template <typename T, typename Less>
size_t partitionRange(Vector<T>& arr, size_t left, size_t right, Less less) {
    size_t mid = left + (right - left) / 2;

    if (less(arr[mid], arr[left])) {
        using std::swap;
        swap(arr[mid], arr[left]);
    }
    if (less(arr[right], arr[left])) {
        using std::swap;
        swap(arr[right], arr[left]);
    }
    if (less(arr[right], arr[mid])) {
        using std::swap;
        swap(arr[right], arr[mid]);
    }

    T pivot = arr[mid];
    size_t i = left;
    size_t j = right;

    while (true) {
        while (less(arr[i], pivot)) {
            i++;
        }
        while (less(pivot, arr[j])) {
            if (j == 0) {
                break;
            }
            j--;
        }

        if (i >= j) {
            return j;
        }

        using std::swap;
        swap(arr[i], arr[j]);
        i++;
        if (j == 0) {
            return 0;
        }
        j--;
    }
}

template <typename T, typename Less>
void hybridSortImpl(Vector<T>& arr, size_t left, size_t right, size_t depthLimit,
                    Less less) {
    if (left >= right) {
        return;
    }

    size_t length = right - left + 1;
    if (length <= 32) {
        insertionSortRange(arr, left, right, less);
        return;
    }

    if (depthLimit == 0) {
        Vector<T> buffer;
        buffer.setSize(arr.size());
        mergeSortWithBuffer(arr, buffer, left, right, less);
        return;
    }

    size_t pivot = partitionRange(arr, left, right, less);
    if (pivot > left) {
        hybridSortImpl(arr, left, pivot, depthLimit - 1, less);
    }
    if (pivot + 1 < right) {
        hybridSortImpl(arr, pivot + 1, right, depthLimit - 1, less);
    }
}

template <typename T, typename Less>
void hybridSort(Vector<T>& arr, Less less) {
    if (arr.size() <= 1) {
        return;
    }

    size_t depthLimit = 0;
    for (size_t n = arr.size(); n > 1; n >>= 1) {
        depthLimit++;
    }
    depthLimit *= 2;

    hybridSortImpl(arr, 0, arr.size() - 1, depthLimit, less);
}

template <typename T>
void hybridSort(Vector<T>& arr) {
    hybridSort(arr, DefaultLessFromLe<T>{});
}

template <typename T, typename Less>
void hybridSort(Vector<T>& arr, int left, int right, Less less) {
    if (left < 0 || right < left) {
        return;
    }

    size_t begin = static_cast<size_t>(left);
    size_t end = static_cast<size_t>(right);
    if (begin >= arr.size() || end >= arr.size()) {
        return;
    }

    size_t depthLimit = 0;
    for (size_t n = end - begin + 1; n > 1; n >>= 1) {
        depthLimit++;
    }
    depthLimit *= 2;

    hybridSortImpl(arr, begin, end, depthLimit, less);
}

template <typename T>
void hybridSort(Vector<T>& arr, int left, int right) {
    hybridSort(arr, left, right, DefaultLessFromLe<T>{});
}
