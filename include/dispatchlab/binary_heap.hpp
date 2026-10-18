#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dispatchlab {

// Array-backed binary min-heap. `Less(a, b)` == true means a has higher
// priority (comes out first). Dijkstra/A* use it with lazy deletion: a node may
// be pushed several times and stale entries are discarded on pop, so no
// decrease_key is needed.
//
// push/pop: O(log n), top/empty/size: O(1).
template <class T, class Less = std::less<T>>
class BinaryHeap {
public:
    BinaryHeap() = default;
    explicit BinaryHeap(Less less) : less_(std::move(less)) {}

    bool empty() const { return data_.empty(); }
    std::size_t size() const { return data_.size(); }
    void clear() { data_.clear(); }
    void reserve(std::size_t n) { data_.reserve(n); }
    std::size_t capacity() const { return data_.capacity(); }

    const T& top() const {
        if (data_.empty()) throw std::out_of_range("BinaryHeap::top on empty heap");
        return data_.front();
    }

    void push(const T& value) {
        data_.push_back(value);
        sift_up(data_.size() - 1);
    }

    void pop() {
        if (data_.empty()) throw std::out_of_range("BinaryHeap::pop on empty heap");
        if (data_.size() == 1) {
            data_.pop_back();
            return;
        }
        data_.front() = std::move(data_.back());
        data_.pop_back();
        sift_down(0);
    }

    // Removes and returns the top element.
    T take() {
        T v = top();
        pop();
        return v;
    }

    // Checks the heap property for every parent/child pair (used by tests).
    bool is_valid() const {
        for (std::size_t i = 1; i < data_.size(); ++i) {
            if (less_(data_[i], data_[(i - 1) / 2])) return false;
        }
        return true;
    }

private:
    // "Hole" technique: move the element once instead of swapping repeatedly.
    void sift_up(std::size_t i) {
        T v = std::move(data_[i]);
        while (i > 0) {
            const std::size_t parent = (i - 1) / 2;
            if (!less_(v, data_[parent])) break;
            data_[i] = std::move(data_[parent]);
            i = parent;
        }
        data_[i] = std::move(v);
    }

    void sift_down(std::size_t i) {
        const std::size_t n = data_.size();
        T v = std::move(data_[i]);
        while (true) {
            std::size_t child = 2 * i + 1;
            if (child >= n) break;
            if (child + 1 < n && less_(data_[child + 1], data_[child])) ++child;
            if (!less_(data_[child], v)) break;
            data_[i] = std::move(data_[child]);
            i = child;
        }
        data_[i] = std::move(v);
    }

    std::vector<T> data_;
    Less less_{};
};

}  // namespace dispatchlab
