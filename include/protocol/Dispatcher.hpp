#pragma once
// include/protocol/Dispatcher.hpp

#include "protocol/Parser.hpp" // Response 정의 포함
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kohzu::protocol {

using SpontaneousHandler = std::function<void(const Response&)>;

class Dispatcher {
public:
    Dispatcher() = default;
    ~Dispatcher();

    // addPending: create a promise for key and return future.
    std::future<Response> addPending(const std::string& key);

    // tryFulfill: if a pending exists for key, set its value and erase.
    bool tryFulfill(const std::string& key, const Response& response);

    // removePendingWithException: set exception and erase pending (used on timeout/cancel)
    void removePendingWithException(const std::string& key, const std::string& message);

    // cancelPending without exception (erase silently)
    void cancelPending(const std::string& key);

    // cancelAllPendingWithException: set exceptions for all pending promises and clear map
    void cancelAllPendingWithException(const std::string& message);

    // register spontaneous handler
    void registerSpontaneousHandler(SpontaneousHandler handler);

    // notify spontaneous (safe copy then call)
    void notifySpontaneous(const Response& response);

private:
    std::mutex pendingMutex_;
    std::unordered_map<std::string, std::promise<Response>> pendingMap_;

    std::mutex handlerMutex_;
    std::vector<SpontaneousHandler> spontaneousHandlers_;
};

} // namespace kohzu::protocol

