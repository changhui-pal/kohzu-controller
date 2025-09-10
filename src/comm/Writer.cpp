#include "comm/Writer.hpp"
#include <iostream>

namespace kohzu::comm {

Writer::Writer(std::shared_ptr<ITcpClient> client, std::size_t maxQueueSize)
    : client_(std::move(client)),
      queue_(),
      maxQueueSize_(std::max<std::size_t>(1, maxQueueSize)),
      running_(false),
      stopRequested_(false) {
}

Writer::~Writer() {
    try {
        stop(true);
    } catch (...) {
        // destructor must not throw
    }
}

void Writer::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }
    stopRequested_.store(false);
    workerThread_ = std::thread(&Writer::workerLoop, this);
}

void Writer::stop(bool flush) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stopRequested_.store(true);
        // if not flushing, we may clear the queue to speed up exit
        if (!flush) {
            queue_.clear();
            cv_not_full_.notify_all();
        }
    }
    cv_not_empty_.notify_all();

    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    running_.store(false);
}

void Writer::enqueue(const std::string& line) {
    std::unique_lock<std::mutex> lk(mtx_);
    // Wait until there is space or stop requested
    cv_not_full_.wait(lk, [this]() {
        return stopRequested_.load() || queue_.size() < maxQueueSize_;
    });

    if (stopRequested_.load()) {
        throw std::runtime_error("Writer is stopping/stopped; enqueue rejected");
    }
    queue_.push_back(line);
    lk.unlock();
    cv_not_empty_.notify_one();
}

bool Writer::tryEnqueue(const std::string& line) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (stopRequested_.load()) return false;
    if (queue_.size() >= maxQueueSize_) return false;
    queue_.push_back(line);
    cv_not_empty_.notify_one();
    return true;
}

std::size_t Writer::queuedSize() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return queue_.size();
}

void Writer::registerErrorHandler(ErrorHandler eh) {
    std::lock_guard<std::mutex> lk(mtx_);
    errorHandler_ = std::move(eh);
}

void Writer::workerLoop() {
    // Loop until stopRequested and queue empty (if flush) or until stopRequested immediate exit
    for (;;) {
        std::string item;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_not_empty_.wait(lk, [this]() {
                return stopRequested_.load() || !queue_.empty();
            });

            if (queue_.empty()) {
                // woke up because stopRequested_ or spurious wake; check stop
                if (stopRequested_.load()) {
                    break;
                } else {
                    continue;
                }
            }

            // pop front
            item = std::move(queue_.front());
            queue_.pop_front();
            // notify any waiting enqueue that there's space
            cv_not_full_.notify_one();
        }

        // Attempt to send the line via ITcpClient
        try {
            // Ensure the client exists and is connected; let client's sendLine throw if necessary
            if (!client_) {
                throw std::runtime_error("Writer: ITcpClient is null");
            }
            client_->sendLine(item);
        } catch (...) {
            // Capture exception and call error handler (if set)
            std::exception_ptr ep = std::current_exception();
            // call handler on worker thread (synchronous)
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (errorHandler_) {
                    try {
                        errorHandler_(ep);
                    } catch (...) {
                        // swallow to avoid thread termination
                    }
                }
            }
            // After an error on send, typical choices:
            //  - continue sending remaining items (best-effort)
            //  - stop immediately
            // Here we choose to stop to avoid further failures and to let upper layers react.
            // Set stopRequested_ and notify
            stopRequested_.store(true);
            cv_not_empty_.notify_all();
            break;
        }
    } // end for

    // Exiting: optionally clear queue (we keep it, but stop() semantics decide flush)
}

} // namespace kohzu::comm
