// src/protocol/Dispatcher.cpp
#include "protocol/Dispatcher.hpp"
#include "protocol/Parser.hpp" // for Response type

#include <chrono>
#include <exception>
#include <future>
#include <iostream>

namespace kohzu::protocol {

Dispatcher::Dispatcher() = default;

Dispatcher::~Dispatcher() {
    cancelAllPendingWithException("Dispatcher shutting down");
}

std::future<Response> Dispatcher::addPending(const std::string& key) {
    std::promise<Response> prom;
    auto fut = prom.get_future();

    std::lock_guard<std::mutex> lk(mtx_);
    // insert only if not existing
    if (pending_.count(key)) {
        // existing pending for same key: this may indicate misuse; still create a new entry with unique suffix?
        // For simplicity, we return the new future but log warning.
        std::cerr << "[Dispatcher] Warning: addPending called for existing key: " << key << std::endl;
    }
    pending_.emplace(key, std::move(prom));
    return fut;
}

bool Dispatcher::tryFulfill(const std::string& key, const Response& response) {
    std::promise<Response> prom;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = pending_.find(key);
        if (it == pending_.end()) {
            return false;
        }
        // move promise out
        prom = std::move(it->second);
        pending_.erase(it);
    }
    try {
        prom.set_value(response);
    } catch (const std::future_error& fe) {
        std::cerr << "[Dispatcher] set_value future_error: " << fe.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[Dispatcher] set_value unknown exception\n";
        return false;
    }
    return true;
}

void Dispatcher::removePendingWithException(const std::string& key, const std::string& message) {
    std::promise<Response> prom;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = pending_.find(key);
        if (it != pending_.end()) {
            prom = std::move(it->second);
            pending_.erase(it);
            found = true;
        }
    }
    if (found) {
        try {
            prom.set_exception(std::make_exception_ptr(std::runtime_error(message)));
        } catch (const std::future_error& fe) {
            std::cerr << "[Dispatcher] set_exception future_error: " << fe.what() << std::endl;
        } catch (...) {
            std::cerr << "[Dispatcher] set_exception unknown exception\n";
        }
    }
}

void Dispatcher::cancelAllPendingWithException(const std::string& message) {
    std::vector<std::promise<Response>> toCancel;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto &kv : pending_) {
            toCancel.push_back(std::move(kv.second));
        }
        pending_.clear();
    }

    for (auto &prom : toCancel) {
        try {
            prom.set_exception(std::make_exception_ptr(std::runtime_error(message)));
        } catch (const std::future_error& fe) {
            std::cerr << "[Dispatcher] set_exception future_error during cancelAll: " << fe.what() << std::endl;
        } catch (...) {
            std::cerr << "[Dispatcher] set_exception unknown exception during cancelAll\n";
        }
    }
}

void Dispatcher::registerSpontaneousHandler(SpontaneousHandler h) {
    std::lock_guard<std::mutex> lk(handlerMtx_);
    spontaneousHandlers_.push_back(std::move(h));
}

void Dispatcher::notifySpontaneous(const Response& resp) {
    // Copy handlers under lock then invoke them asynchronously (std::async)
    std::vector<SpontaneousHandler> handlers;
    {
        std::lock_guard<std::mutex> lk(handlerMtx_);
        handlers = spontaneousHandlers_;
    }
    for (auto &h : handlers) {
        try {
            // launch detached async to avoid blocking caller (which is usually io thread)
            std::async(std::launch::async, [h, resp]() {
                try {
                    h(resp);
                } catch (const std::exception& ex) {
                    std::cerr << "[Dispatcher] spontaneous handler threw: " << ex.what() << std::endl;
                } catch (...) {
                    std::cerr << "[Dispatcher] spontaneous handler threw unknown exception" << std::endl;
                }
            });
        } catch (const std::exception& ex) {
            std::cerr << "[Dispatcher] failed to launch async spontaneous handler: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "[Dispatcher] failed to launch async spontaneous handler (unknown)\n";
        }
    }
}

} // namespace kohzu::protocol
