#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace iqa {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("queue capacity must be positive");
        }
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
        if (closed_) {
            return false;
        }

        queue_.push_back(std::move(item));
        max_observed_size_ = std::max(max_observed_size_, queue_.size());
        not_empty_.notify_one();
        return true;
    }

    bool pushDropOldest(T item, bool& dropped) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }

        dropped = queue_.size() == capacity_;
        if (dropped) {
            queue_.pop_front();
        }
        queue_.push_back(std::move(item));
        max_observed_size_ = std::max(max_observed_size_, queue_.size());
        not_empty_.notify_one();
        return true;
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t maxObservedSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_observed_size_;
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> queue_;
    std::size_t max_observed_size_ = 0;
    bool closed_ = false;
};

}  // namespace iqa
