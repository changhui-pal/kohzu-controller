#pragma once
/**
 * Dispatcher.hpp
 *
 * 개선 요약:
 * - 동일 키의 다중 pending을 허용하도록 자료구조 변경 (deque of promises)
 * - spontaneous 핸들러 호출을 제한된 worker queue로 처리하여 스레드 폭증 방지
 *
 * 사용:
 *   auto fut = dispatcher.addPending(key);
 *   // ... writer.enqueue(...)
 *   auto resp = fut.get(); // 또는 fut.wait_for(...)
 */

#include <future>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <thread>
#include <condition_variable>
#include <queue>
#include <atomic>

#include "Parser.hpp" // Response 정의

namespace kohzu::protocol {

class Dispatcher {
public:
    using Response = kohzu::protocol::Response;
    using SpontaneousHandler = std::function<void(const Response&)>;

    Dispatcher();
    ~Dispatcher();

    // register a pending promise for key, return future to wait on
    std::future<Response> addPending(const std::string& key);

    // try to fulfill one pending promise for key (first-in). Returns true if fulfilled.
    bool tryFulfill(const std::string& key, const Response& response);

    // remove/fulfill one pending for key with exception
    void removePendingWithException(const std::string& key, const std::string& message);

    // cancel all pending promises with the given exception message
    void cancelAllPendingWithException(const std::string& message);

    // spontaneous handlers registration
    void registerSpontaneousHandler(SpontaneousHandler h);

    // notify spontaneous (will dispatch to handlers asynchronously via worker queue)
    void notifySpontaneous(const Response& resp);

private:
    // pending map: key -> deque<promise<Response>>
    std::unordered_map<std::string, std::deque<std::promise<Response>>> pending_;
    std::mutex mtx_; // protects pending_

    // spontaneous handlers and worker task queue
    std::vector<SpontaneousHandler> spontaneousHandlers_;
    std::mutex handlerMtx_;

    std::deque<std::function<void()>> taskQueue_;
    std::mutex taskMtx_;
    std::condition_variable taskCv_;
    std::vector<std::thread> taskWorkers_;
    std::atomic<bool> stopWorkers_{false};

    // internal worker count
    static constexpr size_t DEFAULT_SPONT_WORKERS = 2;
    void startWorkers(size_t n = DEFAULT_SPONT_WORKERS);
    void stopAndJoinWorkers();
};

} // namespace kohzu::protocol
