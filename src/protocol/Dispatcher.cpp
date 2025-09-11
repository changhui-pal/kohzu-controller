#include "protocol/Dispatcher.hpp"
#include <iostream>

namespace kohzu::protocol {

Dispatcher::Dispatcher() {
    startWorkers(2);
}

Dispatcher::~Dispatcher() {
    // stop workers then cancel pending for safe destruction
    stopAndJoinWorkers();
    cancelAllPendingWithException("Dispatcher shutting down");
}

void Dispatcher::startWorkers(size_t n) {
    stopWorkers_.store(false);
    for (size_t i = 0; i < n; ++i) {
        taskWorkers_.emplace_back([this]() {
            while (!stopWorkers_.load()) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lk(taskMtx_);
                    taskCv_.wait(lk, [this]() { return stopWorkers_.load() || !taskQueue_.empty(); });
                    if (stopWorkers_.load() && taskQueue_.empty()) break;
                    if (!taskQueue_.empty()) {
                        task = std::move(taskQueue_.front());
                        taskQueue_.pop_front();
                    } else continue;
                }
                try {
                    if (task) task();
                } catch (const std::exception& e) {
                    std::cerr << "[Dispatcher::worker] task threw: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[Dispatcher::worker] task threw unknown exception\n";
                }
            }
        });
    }
}

void Dispatcher::stopAndJoinWorkers() {
    stopWorkers_.store(true);
    taskCv_.notify_all();
    for (auto &t : taskWorkers_) {
        if (t.joinable()) {
            try { t.join(); } catch (...) {}
        }
    }
    taskWorkers_.clear();
}

std::future<Dispatcher::Response> Dispatcher::addPending(const std::string& key) {
    std::promise<Response> prom;
    auto fut = prom.get_future();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_[key].push_back(std::move(prom));
    }
    return fut;
}

bool Dispatcher::tryFulfill(const std::string& key, const Response& response) {
    std::promise<Response> prom;
    bool have = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = pending_.find(key);
        if (it == pending_.end() || it->second.empty()) return false;
        prom = std::move(it->second.front());
        it->second.pop_front();
        if (it->second.empty()) pending_.erase(it);
        have = true;
    }
    if (have) {
        try {
            prom.set_value(response);
        } catch (const std::exception& e) {
            std::cerr << "[Dispatcher] set_value exception: " << e.what() << std::endl;
            return false;
        } catch (...) {
            std::cerr << "[Dispatcher] set_value unknown exception\n";
            return false;
        }
        return true;
    }
    return false;
}

void Dispatcher::removePendingWithException(const std::string& key, const std::string& message) {
    std::promise<Response> prom;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = pending_.find(key);
        if (it != pending_.end() && !it->second.empty()) {
            prom = std::move(it->second.front());
            it->second.pop_front();
            if (it->second.empty()) pending_.erase(it);
            found = true;
        }
    }
    if (found) {
        try {
            prom.set_exception(std::make_exception_ptr(std::runtime_error(message)));
        } catch (...) {
            std::cerr << "[Dispatcher] removePendingWithException failed to set_exception\n";
        }
    }
}

void Dispatcher::cancelAllPendingWithException(const std::string& message) {
    std::vector<std::promise<Response>> moved;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto &kv : pending_) {
            for (auto &p : kv.second) moved.push_back(std::move(p));
        }
        pending_.clear();
    }
    for (auto &p : moved) {
        try { p.set_exception(std::make_exception_ptr(std::runtime_error(message))); }
        catch (...) { std::cerr << "[Dispatcher] cancelAllPendingWithException set_exception failed\n"; }
    }
}

void Dispatcher::registerSpontaneousHandler(SpontaneousHandler h) {
    std::lock_guard<std::mutex> lk(handlerMtx_);
    spontaneousHandlers_.push_back(std::move(h));
}

void Dispatcher::notifySpontaneous(const Response& resp) {
    std::vector<SpontaneousHandler> handlers;
    {
        std::lock_guard<std::mutex> lk(handlerMtx_);
        handlers = spontaneousHandlers_;
    }
    if (handlers.empty()) return;
    {
        std::lock_guard<std::mutex> lk(taskMtx_);
        for (auto &h : handlers) {
            taskQueue_.emplace_back([h, resp]() {
                try { h(resp); } catch (const std::exception& e) { std::cerr << "[Dispatcher] handler threw: " << e.what() << std::endl; }
                catch (...) { std::cerr << "[Dispatcher] handler threw unknown\n"; }
            });
        }
    }
    taskCv_.notify_all();
}

} // namespace kohzu::protocol
