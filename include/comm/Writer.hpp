#pragma once
#include "ITcpClient.hpp"
#include <functional>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <thread>
#include <atomic>

namespace kohzu::comm {

class Writer {
public:
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    enum class EnqueueResult { OK, Stopped, Overflow };

    Writer(std::shared_ptr<ITcpClient> client, std::size_t capacity = 1000);
    ~Writer();

    void start();
    void stop(bool flush = true);

    EnqueueResult enqueue(const std::string& line);
    void registerErrorHandler(ErrorHandler eh);

private:
    void workerLoop();

    std::shared_ptr<ITcpClient> client_;
    std::size_t capacity_;
    std::deque<std::string> q_;
    std::mutex mtx_;
    std::condition_variable cv_not_full_;
    std::condition_variable cv_not_empty_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread workerThread_;
    ErrorHandler errorHandler_;
};

} // namespace kohzu::comm
