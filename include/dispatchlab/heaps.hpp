#pragma once

// Alternative priority queues for the priority-queue study (plan.md, A2).
// All expose the subset of the BinaryHeap interface Dijkstra uses:
// push, take, top, empty, size, clear, capacity.

#include <array>
#include <bit>
#include <cstddef>
#include <functional>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dispatchlab/types.hpp"

namespace dispatchlab {

// D-ary min-heap. A wider node halves the tree height for D=4, trading more
// comparisons per sift-down for fewer levels (and better cache use per level).
template <class T, unsigned D, class Less = std::less<T>>
class DaryHeap {
    static_assert(D >= 2, "D-ary heap needs D >= 2");

public:
    bool empty() const { return data_.empty(); }
    std::size_t size() const { return data_.size(); }
    void clear() { data_.clear(); }
    std::size_t capacity() const { return data_.capacity(); }
    const T& top() const {
        if (data_.empty()) throw std::out_of_range("DaryHeap::top on empty heap");
        return data_.front();
    }
    void push(const T& v) {
        data_.push_back(v);
        std::size_t i = data_.size() - 1;
        T x = std::move(data_[i]);
        while (i > 0) {
            const std::size_t p = (i - 1) / D;
            if (!less_(x, data_[p])) break;
            data_[i] = std::move(data_[p]);
            i = p;
        }
        data_[i] = std::move(x);
    }
    T take() {
        if (data_.empty()) throw std::out_of_range("DaryHeap::take on empty heap");
        T out = std::move(data_.front());
        T x = std::move(data_.back());
        data_.pop_back();
        const std::size_t n = data_.size();
        if (n) {
            std::size_t i = 0;
            while (true) {
                const std::size_t c0 = D * i + 1;
                if (c0 >= n) break;
                std::size_t best = c0;
                const std::size_t cend = std::min(c0 + D, n);
                for (std::size_t c = c0 + 1; c < cend; ++c) {
                    if (less_(data_[c], data_[best])) best = c;
                }
                if (!less_(data_[best], x)) break;
                data_[i] = std::move(data_[best]);
                i = best;
            }
            data_[i] = std::move(x);
        }
        return out;
    }
    bool is_valid() const {
        for (std::size_t i = 1; i < data_.size(); ++i) {
            if (less_(data_[i], data_[(i - 1) / D])) return false;
        }
        return true;
    }

private:
    std::vector<T> data_;
    Less less_{};
};

// Radix heap for monotone integer keys (Ahuja, Mehlhorn, Orlin, Tarjan 1990):
// every pushed key must be >= the last key taken, which holds for Dijkstra with
// non-negative weights. Items live in 65 buckets by the highest bit in which
// their key differs from the last taken key; amortized O(log C) per item.
// Equal keys come out in unspecified order (costs are unaffected).
template <class T>
class RadixHeap {
public:
    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    void clear() {
        for (auto& b : buckets_) b.clear();
        size_ = 0;
        last_ = 0;
    }
    std::size_t capacity() const {
        std::size_t c = 0;
        for (const auto& b : buckets_) c += b.capacity();
        return c;
    }
    void push(const T& v) {
        if (v.key < last_) throw std::logic_error("RadixHeap: non-monotone key");
        buckets_[bucket_of(v.key)].push_back(v);
        ++size_;
    }
    const T& top() {
        refill();
        return buckets_[0].back();
    }
    T take() {
        refill();
        T v = buckets_[0].back();
        buckets_[0].pop_back();
        --size_;
        return v;
    }

private:
    std::size_t bucket_of(Dist key) const {
        return key == last_ ? 0 : static_cast<std::size_t>(64 - std::countl_zero(key ^ last_));
    }
    // Moves the smallest non-empty bucket's items down, relative to its minimum.
    void refill() {
        if (size_ == 0) throw std::out_of_range("RadixHeap: empty");
        if (!buckets_[0].empty()) return;
        std::size_t i = 1;
        while (buckets_[i].empty()) ++i;
        Dist mn = buckets_[i][0].key;
        for (const auto& x : buckets_[i]) mn = std::min(mn, x.key);
        last_ = mn;
        std::vector<T> moving;
        moving.swap(buckets_[i]);
        for (const auto& x : moving) buckets_[bucket_of(x.key)].push_back(x);
        moving.clear();
        buckets_[i].swap(moving);  // keep the capacity
    }

    std::array<std::vector<T>, 65> buckets_;
    std::size_t size_ = 0;
    Dist last_ = 0;
};

// std::priority_queue adapter (reference point for the study).
template <class T, class Less>
class StdHeap {
    struct Greater {
        bool operator()(const T& a, const T& b) const { return Less{}(b, a); }
    };

public:
    bool empty() const { return q_.empty(); }
    std::size_t size() const { return q_.size(); }
    void clear() { q_ = {}; }
    std::size_t capacity() const { return q_.size(); }  // not observable; reports size
    const T& top() const { return q_.top(); }
    void push(const T& v) { q_.push(v); }
    T take() {
        T v = q_.top();
        q_.pop();
        return v;
    }

private:
    std::priority_queue<T, std::vector<T>, Greater> q_;
};

}  // namespace dispatchlab
