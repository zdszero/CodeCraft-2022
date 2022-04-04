#pragma once

#include <algorithm>
#include <iostream>
#include <vector>

/// A priority queue with fixed size. When the maximum size was reached,
/// the element with the lowest priority would be removed automatically.
template <typename T, typename Compare = std::less<T>>
class fixed_size_priority_queue {
  public:
    fixed_size_priority_queue() : max_size_(0) {}
    fixed_size_priority_queue(size_t max_size) : max_size_(max_size) {}

    typedef typename std::vector<T>::iterator iterator;
    iterator begin() { return c_.begin(); }
    iterator end() { return c_.end(); }

    inline void push(const T &x) {
        if (c_.size() == max_size_) {
            typename std::vector<T>::iterator iterator_min =
                std::min_element(c_.begin(), c_.end(), cmp);
            if (*iterator_min < x) {
                *iterator_min = x;
                std::make_heap(c_.begin(), c_.end(), cmp);
            }
        } else {
            c_.push_back(x);
            std::make_heap(c_.begin(), c_.end(), cmp);
        }
    }

    inline void pop() {
        if (c_.empty())
            return;
        std::pop_heap(c_.begin(), c_.end(), cmp);
        c_.pop_back();
    }

    inline const T &top() const { return c_.front(); }

    inline const bool empty() const { return c_.empty(); }

    inline const size_t size() const { return c_.size(); }

    inline void enlarge_max_size(size_t max_size) {
        if (max_size_ < max_size)
            max_size_ = max_size;
    }

  protected:
    std::vector<T> c_;
    size_t max_size_;
    Compare cmp;

  private:
    // heap allocation is not allowed
    void *operator new(size_t);
    void *operator new[](size_t);
    void operator delete(void *);
    void operator delete[](void *);
};
