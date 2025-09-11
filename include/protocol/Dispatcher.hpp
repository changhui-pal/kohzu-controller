#pragma once
#include <future>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <thread>
#include <condition_variable>
#include <atomic>

#include "Parser.hpp"

namespace kohzu::protocol {

class Dispatcher {
public:
    using Response = kohzu::protocol::Response;
    using SpontaneousHandler = std::function<void(const Response&)>;

    Dispatcher();
    ~Dispatcher();

    // add pending -> returns future to wait on
    std::future<Response> addPending(const std::string& key);

    // try fulfill FIFO one pending for key, return true if fulfilled
    bool tryFulfill(const std::string& key, const Response& response);

    // remove single pending for key and set_exception
    void removePendingWithException(const std::string& key, const std::string& message);

    // cancel all pending with exception
    void cancelAllPendingWithException(const std::string& message);

    // spontaneous handlers
    void registerSpontaneousHandler(SpontaneousHandler h);
    void notifySpontaneous(const Response& resp);

private:
    // pending map: key -> deque of promises (FIFO)
    std::unordered_map<std::string, std::deque<std::promise<Response>>> pending_;
    std::mutex mtx_;

    std::vector<SpontaneousHandler> spontaneousHandlers_;
    std::mutex handlerMtx_;

    // worker pool for handling spontaneous notifications
    std::deque<std::function<void()>> taskQueue_;
    std::mutex taskMtx_;
    std::condition_variable taskCv_;
    std::vector<std::thread> taskWorkers_;
    std::atomic<bool> stopWorkers_{false};
    void startWorkers(size_t n = 2);
    void stopAndJoinWorkers();
};

} // namespace kohzu::protocol
