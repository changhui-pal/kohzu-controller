#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

/**
 * @brief A thread-safe queue template class for multi-threaded environments.
 * @tparam T The type of data to be stored in the queue.
 */
template <typename T>
class ThreadSafeQueue {
public:
    /**
     * @brief Pushes data to the queue.
     * @param value The data to be pushed.
     */
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push(value);
        cv_.notify_one();
    }

    /**
     * @brief Pops data from the queue. Waits until data arrives if the queue is empty.
     * @return The data popped from the queue.
     */
    T pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T value = queue_.front();
        queue_.pop();
        return value;
    }

    /**
     * @brief Tries to pop data from the queue with a timeout.
     * @param value A reference to the variable to store the data.
     * @param timeout_ms Timeout duration in milliseconds.
     * @return True if data was successfully retrieved, false if a timeout occurred.
     */
    bool try_pop(T& value, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !queue_.empty(); })) {
            value = queue_.front();
            queue_.pop();
            return true;
        }
        return false;
    }

    /**
     * @brief Checks if the queue is empty.
     * @return True if the queue is empty, false otherwise.
     */
    bool empty() {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

#endif // THREAD_SAFE_QUEUE_H
