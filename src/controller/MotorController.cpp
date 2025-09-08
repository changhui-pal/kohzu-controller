// src/controller/MotorController.cpp

#include "controller/MotorController.hpp"

#include <iostream>
#include <stdexcept>

using namespace kohzu::controller;
namespace kc = kohzu::comm;
namespace kp = kohzu::protocol;

MotorController::MotorController(std::shared_ptr<kohzu::comm::ITcpClient> comm)
: comm_(comm), writer_(nullptr), dispatcher_() {
    // register receive handler to route lines to dispatcher
    comm_->registerRecvHandler([this](const std::string& line) {
        this->onLineReceived(line);
    });

    // start callback worker thread
    callbackWorkerStopRequested_.store(false);
    callbackWorkerThread_ = std::thread([this]() { this->callbackWorkerLoop(); });
}

MotorController::~MotorController() {
    try {
        stop();
    } catch (...) {
        // destructor must not throw
    }

    // ensure worker stopped
    callbackWorkerStopRequested_.store(true);
    callbackQueueCv_.notify_one();
    if (callbackWorkerThread_.joinable()) {
        callbackWorkerThread_.join();
    }
}

void MotorController::connect(const std::string& host, uint16_t port) {
    comm_->connect(host, port);
    writer_ = std::make_unique<kohzu::comm::Writer>(comm_->socket());
}

void MotorController::stop() {
    // First, cancel any pending responses so callers don't hang
    try {
        dispatcher_.cancelAllPendingWithException("MotorController stopping");
    } catch (...) {
        // swallow any exception
    }

    // request callback worker stop and wake it
    callbackWorkerStopRequested_.store(true);
    callbackQueueCv_.notify_one();

    if (writer_) {
        writer_->stop();
        writer_.reset();
    }
    if (comm_) {
        comm_->close();
    }

    // join callback worker
    if (callbackWorkerThread_.joinable()) {
        callbackWorkerThread_.join();
    }
}

std::string MotorController::makeMatchKey(const std::string& cmd, const std::vector<std::string>& params) {
    std::string key = cmd;
    if (!params.empty() && !params[0].empty()) {
        key += ":";
        key += params[0];
    }
    return key;
}

void MotorController::registerSpontaneousHandler(kohzu::protocol::SpontaneousHandler handler) {
    dispatcher_.registerSpontaneousHandler(std::move(handler));
}

void MotorController::onLineReceived(const std::string& line) {
    kp::Response resp = kp::Parser::parse(line);

    if (!resp.valid) {
        std::cerr << "Parser: invalid/malformed response received. raw=\"" << line << "\"\n";
        return;
    }

    if (resp.cmd == "SYS") {
        dispatcher_.notifySpontaneous(resp);
        return;
    }

    std::string key;
    if (!resp.axis.empty()) {
        key = resp.cmd + ":" + resp.axis;
    } else {
        key = resp.cmd;
    }

    if (!dispatcher_.tryFulfill(key, resp)) {
        dispatcher_.notifySpontaneous(resp);
    }
}

std::future<kp::Response> MotorController::sendAsync(const std::string& cmd, const std::vector<std::string>& params) {
    if (!writer_) {
        throw std::runtime_error("Writer not initialized; call connect first");
    }
    std::string matchKey = MotorController::makeMatchKey(cmd, params);
    std::future<kp::Response> future = dispatcher_.addPending(matchKey);

    std::string cmdLine = kp::CommandBuilder::makeCommand(cmd, params, false);
    try {
        writer_->enqueue(cmdLine);
    } catch (const std::exception& e) {
        dispatcher_.removePendingWithException(matchKey, std::string("enqueue failed: ") + e.what());
        throw;
    }

    return future;
}

// NEW: callback-style sendAsync
void MotorController::sendAsync(const std::string& cmd, const std::vector<std::string>& params, AsyncCallback callback) {
    if (!writer_) {
        throw std::runtime_error("Writer not initialized; call connect first");
    }
    if (!callback) {
        throw std::invalid_argument("callback is empty");
    }

    std::string matchKey = MotorController::makeMatchKey(cmd, params);
    std::future<kp::Response> future = dispatcher_.addPending(matchKey);

    std::string cmdLine = kp::CommandBuilder::makeCommand(cmd, params, false);
    try {
        writer_->enqueue(cmdLine);
    } catch (const std::exception& e) {
        dispatcher_.removePendingWithException(matchKey, std::string("enqueue failed: ") + e.what());
        throw;
    }

    // push task to callback queue
    CallbackTask task{ std::move(future), std::move(callback) };
    pushCallbackTask(std::move(task));
}

kp::Response MotorController::sendSync(const std::string& cmd, const std::vector<std::string>& params,
                                       std::chrono::milliseconds timeout) {
    if (!writer_) {
        throw std::runtime_error("Writer not initialized; call connect first");
    }

    std::string matchKey = MotorController::makeMatchKey(cmd, params);
    std::future<kp::Response> future = dispatcher_.addPending(matchKey);

    std::string cmdLine = kp::CommandBuilder::makeCommand(cmd, params, false);

    try {
        writer_->enqueue(cmdLine);
    } catch (const std::exception& e) {
        dispatcher_.removePendingWithException(matchKey, std::string("enqueue failed: ") + e.what());
        throw;
    }

    if (future.wait_for(timeout) == std::future_status::ready) {
        kp::Response resp = future.get();
        return resp;
    } else {
        dispatcher_.removePendingWithException(matchKey, std::string("timeout waiting for response"));
        throw std::runtime_error("timeout waiting for response");
    }
}

// helper to push task into queue
void MotorController::pushCallbackTask(CallbackTask&& task) {
    {
        std::lock_guard<std::mutex> lk(callbackQueueMutex_);
        callbackQueue_.push_back(std::move(task));
    }
    callbackQueueCv_.notify_one();
}

// worker loop implementation
void MotorController::callbackWorkerLoop() {
    using namespace std::chrono_literals;

    while (!callbackWorkerStopRequested_.load()) {
        CallbackTask task;
        bool haveTask = false;

        {
            std::unique_lock<std::mutex> lk(callbackQueueMutex_);
            if (callbackQueue_.empty()) {
                // wait until new task or stop requested
                callbackQueueCv_.wait_for(lk, 200ms, [this]() {
                    return !callbackQueue_.empty() || callbackWorkerStopRequested_.load();
                });
            }

            if (!callbackQueue_.empty()) {
                task = std::move(callbackQueue_.front());
                callbackQueue_.pop_front();
                haveTask = true;
            }
        }

        if (!haveTask) {
            continue; // check stop flag and loop
        }

        // Wait for the future to become ready, but poll periodically so we can respond to stop request.
        std::future<kp::Response>& fut = task.fut;
        while (!callbackWorkerStopRequested_.load()) {
            if (fut.wait_for(100ms) == std::future_status::ready) {
                // ready -> process it
                try {
                    kp::Response resp = fut.get();
                    try {
                        task.cb(resp, nullptr);
                    } catch (...) {
                        // swallow exceptions from callback
                    }
                } catch (...) {
                    // future.get() threw -> pass exception to callback
                    std::exception_ptr ep = std::current_exception();
                    try {
                        task.cb(kp::Response(), ep);
                    } catch (...) {
                        // swallow
                    }
                }
                break;
            }
            // else not ready -> loop and check stop flag
        }

        // if stop requested and future not ready, skip calling callback (pending should have been canceled by dispatcher)
    }

    // On exit, optionally process remaining tasks by signaling exception
    // (but dispatcher.cancelAllPendingWithException is expected to have been called on stop())
    std::deque<CallbackTask> remaining;
    {
        std::lock_guard<std::mutex> lk(callbackQueueMutex_);
        remaining.swap(callbackQueue_);
    }
    for (auto &task : remaining) {
        try {
            // try to get if ready
            if (task.fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                try {
                    kp::Response resp = task.fut.get();
                    task.cb(resp, nullptr);
                } catch (...) {
                    task.cb(kp::Response(), std::current_exception());
                }
            } else {
                // not ready -> signal error
                task.cb(kp::Response(), std::make_exception_ptr(std::runtime_error("MotorController stopping; task canceled")));
            }
        } catch (...) {
            // swallow
        }
    }
}

