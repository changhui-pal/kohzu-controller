// src/protocol/Dispatcher.cpp

#include "protocol/Dispatcher.hpp"

#include <stdexcept>

namespace kohzu::protocol {

Dispatcher::~Dispatcher() {
    // On destruction, ensure pending promises are completed with an exception
    cancelAllPendingWithException("Dispatcher destroyed");
}

std::future<Response> Dispatcher::addPending(const std::string& key) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    std::promise<Response> promise;
    std::future<Response> future = promise.get_future();
    // emplace may move the promise; to avoid invalidating our local, move into map directly
    pendingMap_.emplace(key, std::move(promise));
    return future;
}

bool Dispatcher::tryFulfill(const std::string& key, const Response& response) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = pendingMap_.find(key);
    if (it != pendingMap_.end()) {
        try {
            it->second.set_value(response);
        } catch (...) {
            // swallow exceptions from set_value
        }
        pendingMap_.erase(it);
        return true;
    }
    return false;
}

void Dispatcher::removePendingWithException(const std::string& key, const std::string& message) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = pendingMap_.find(key);
    if (it != pendingMap_.end()) {
        try {
            it->second.set_exception(std::make_exception_ptr(std::runtime_error(message)));
        } catch (...) {
            // ignore
        }
        pendingMap_.erase(it);
    }
}

void Dispatcher::cancelPending(const std::string& key) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingMap_.erase(key);
}

void Dispatcher::cancelAllPendingWithException(const std::string& message) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    for (auto &kv : pendingMap_) {
        try {
            kv.second.set_exception(std::make_exception_ptr(std::runtime_error(message)));
        } catch (...) {
            // ignore
        }
    }
    pendingMap_.clear();
}

void Dispatcher::registerSpontaneousHandler(SpontaneousHandler handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    spontaneousHandlers_.push_back(std::move(handler));
}

void Dispatcher::notifySpontaneous(const Response& response) {
    std::vector<SpontaneousHandler> handlersCopy;
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handlersCopy = spontaneousHandlers_;
    }
    for (std::size_t i = 0; i < handlersCopy.size(); ++i) {
        try {
            handlersCopy[i](response);
        } catch (...) {
            // swallow
        }
    }
}

} // namespace kohzu::protocol

