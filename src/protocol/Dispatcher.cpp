#include "protocol/Dispatcher.hpp"
#include "protocol/Parser.hpp"

#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <utility>

namespace kohzu::protocol {

Dispatcher::Dispatcher() {
    startWorkers();
}

Dispatcher::~Dispatcher() {
    // stop workers first to avoid tasks running while we cancel pending
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
                    } else {
                        continue;
                    }
                }
                try {
                    if (task) task();
                } catch (const std::exception& ex) {
                    std::cerr << "[Dispatcher::worker] task threw: " << ex.what() << std::endl;
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
            try {
                t.join();
            } catch (const std::exception& ex) {
                std::cerr << "[Dispatcher] worker join error: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[Dispatcher] worker join unknown error\n";
            }
        }
    }
    taskWorkers_.clear();
}

std::future<Response> Dispatcher::addPending(const std::string& key) {
    std::promise<Response> prom;
    auto fut = prom.get_future();

    {
        std::lock_guard<std::mutex> lk(mtx_);
        // push a new promise into deque for this key
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
        if (it == pending_.end() || it->second.empty()) {
            return false;
        }
        prom = std::move(it->second.front());
        it->second.pop_front();
        if (it->second.empty()) pending_.erase(it);
        have = true;
    }
    if (have) {
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
        } catch (const std::future_error& fe) {
            std::cerr << "[Dispatcher] set_exception future_error: " << fe.what() << std::endl;
        } catch (...) {
            std::cerr << "[Dispatcher] set_exception unknown exception\n";
        }
    }
}

void Dispatcher::cancelAllPendingWithException(const std::string& message) {
    std::vector<std::promise<Response>> moved;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        moved.reserve(64);
        for (auto &kv : pending_) {
            for (auto &p : kv.second) {
                moved.push_back(std::move(p));
            }
        }
        pending_.clear();
    }

    for (auto &p : moved) {
        try {
            p.set_exception(std::make_exception_ptr(std::runtime_error(message)));
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
    std::vector<SpontaneousHandler> handlers;
    {
        std::lock_guard<std::mutex> lk(handlerMtx_);
        handlers = spontaneousHandlers_;
    }
    if (handlers.empty()) return;

    // enqueue tasks for each handler (worker threads will execute them)
    {
        std::lock_guard<std::mutex> lk(taskMtx_);
        for (auto &h : handlers) {
            // copy handler and response into task
            taskQueue_.emplace_back([h, resp]() {
                try {
                    h(resp);
                } catch (const std::exception& ex) {
                    std::cerr << "[Dispatcher] spontaneous handler threw: " << ex.what() << std::endl;
                } catch (...) {
                    std::cerr << "[Dispatcher] spontaneous handler threw unknown exception\n";
                }
            });
        }
    }
    taskCv_.notify_all();
}

} // namespace kohzu::protocol
