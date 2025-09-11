#include "comm/Writer.hpp"
#include <iostream>
#include <thread>
#include <exception>

namespace kohzu::comm {

Writer::Writer(std::shared_ptr<ITcpClient> client, std::size_t capacity)
    : client_(client), capacity_(capacity) {}

Writer::~Writer() {
    stop(false);
}

void Writer::start() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (running_) return;
    running_ = true;
    stopRequested_.store(false);
    workerThread_ = std::thread([this]() { workerLoop(); });
}

void Writer::stop(bool flush) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        running_ = false;
        stopRequested_.store(true);
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }
    if (workerThread_.joinable()) workerThread_.join();
    if (!flush) {
        std::lock_guard<std::mutex> lk(mtx_);
        while (!q_.empty()) q_.pop();
    }
}

Writer::EnqueueResult Writer::enqueue(const std::string& line) {
    std::unique_lock<std::mutex> lk(mtx_);
    if (stopRequested_.load() || !running_) return EnqueueResult::Stopped;
    if (q_.size() >= capacity_) return EnqueueResult::Overflow;
    q_.push(line);
    cv_not_empty_.notify_one();
    return EnqueueResult::OK;
}

void Writer::registerErrorHandler(ErrorHandler eh) {
    std::lock_guard<std::mutex> lk(mtx_);
    errorHandler_ = std::move(eh);
}

void Writer::workerLoop() {
    for (;;) {
        std::string item;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_not_empty_.wait(lk, [this]() { return stopRequested_.load() || !q_.empty(); });
            if (stopRequested_.load() && q_.empty()) break;
            if (!q_.empty()) {
                item = std::move(q_.front());
                q_.pop();
                cv_not_full_.notify_one();
            } else {
                continue;
            }
        }

        // Attempt to send the line via ITcpClient
        try {
            if (!client_) {
                throw std::runtime_error("Writer: ITcpClient is null");
            }
            client_->sendLine(item);
        } catch (const std::exception& e) {
            std::exception_ptr ep = std::current_exception();
            ErrorHandler eh;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                eh = errorHandler_;
            }
            if (eh) {
                try {
                    std::thread([eh, ep]() {
                        try { eh(ep); } catch (...) { /* swallow */ }
                    }).detach();
                } catch (...) {
                    try { eh(ep); } catch (...) { /* swallow */ }
                }
            }
            stopRequested_.store(true);
            cv_not_empty_.notify_all();
            break;
        } catch (...) {
            std::exception_ptr ep = std::current_exception();
            ErrorHandler eh;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                eh = errorHandler_;
            }
            if (eh) {
                try {
                    std::thread([eh, ep]() {
                        try { eh(ep); } catch (...) { /* swallow */ }
                    }).detach();
                } catch (...) {
                    try { eh(ep); } catch (...) { /* swallow */ }
                }
            }
            stopRequested_.store(true);
            cv_not_empty_.notify_all();
            break;
        }
    } // end for
}

} // namespace kohzu::comm
