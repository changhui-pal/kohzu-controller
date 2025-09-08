// src/controller/KohzuManager.cpp

#include "controller/KohzuManager.hpp"

#include <iostream>
#include <sstream>
#include <thread>

using namespace kohzu::controller;
namespace kc = kohzu::comm;
namespace kp = kohzu::protocol;

KohzuManager::KohzuManager(const std::string& host, uint16_t port,
                           bool autoReconnect,
                           std::chrono::milliseconds reconnectInterval,
                           std::chrono::milliseconds pollInterval)
    : host_(host),
      port_(port),
      autoReconnect_(autoReconnect),
      reconnectInterval_(reconnectInterval),
      pollInterval_(pollInterval),
      tcpClient_(nullptr),
      motorCtrl_(nullptr),
      poller_(nullptr)
{
}

KohzuManager::~KohzuManager() {
    stop();
}

void KohzuManager::startAsync() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }

    connectThread_ = std::thread([this]() { this->runConnectLoop(); });
}

void KohzuManager::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        // not running
    }

    // join connect thread
    if (connectThread_.joinable()) {
        connectThread_.join();
    }

    // stop poller if running
    {
        std::lock_guard<std::mutex> lk(pollerMutex_);
        if (poller_) {
            try {
                poller_->stop();
            } catch (...) {}
            poller_.reset();
        }
    }

    // stop motor controller cleanly
    if (motorCtrl_) {
        try {
            motorCtrl_->stop();
        } catch (...) {}
    }

    // clear tcp client and motor controller
    {
        std::lock_guard<std::mutex> lk(threadMutex_);
        motorCtrl_.reset();
        tcpClient_.reset();
    }

    connected_.store(false);

    // notify handlers we stopped
    {
        std::lock_guard<std::mutex> lk(handlersMutex_);
        for (auto &h : handlers_) {
            try { h(false, std::string("Stopped")); } catch (...) {}
        }
    }
}

bool KohzuManager::connectOnce(std::string& outMessage) {
    try {
        auto client = std::make_shared<kc::AsioTcpClient>();
        auto ctrl = std::make_shared<MotorController>(std::static_pointer_cast<kc::ITcpClient>(client));

        ctrl->connect(host_, port_);

        {
            std::lock_guard<std::mutex> lk(threadMutex_);
            tcpClient_ = client;
            motorCtrl_ = ctrl;
        }

        // Create poller now (but do not start it). Use pollAxes_ which may be empty (no polling)
        {
            std::lock_guard<std::mutex> lk(pollerMutex_);
            poller_.reset();
            poller_ = std::make_unique<Poller>(motorCtrl_, pollAxes_, pollInterval_);
            // Do not start automatically; will be started on demand when actions begin
        }

        outMessage = "Connected";
        return true;
    } catch (const std::exception& e) {
        outMessage = std::string("Connect failed: ") + e.what();
        return false;
    } catch (...) {
        outMessage = "Connect failed: unknown error";
        return false;
    }
}

bool KohzuManager::isConnected() const {
    return connected_.load();
}

std::shared_ptr<MotorController> KohzuManager::getMotorController() const {
    std::lock_guard<std::mutex> lk(threadMutex_);
    return motorCtrl_;
}

void KohzuManager::registerConnectionHandler(ConnectionHandler handler) {
    std::lock_guard<std::mutex> lk(handlersMutex_);
    handlers_.push_back(std::move(handler));
}

void KohzuManager::runConnectLoop() {
    while (running_.load()) {
        std::string msg;
        bool ok = connectOnce(msg);

        connected_.store(ok);

        {
            std::lock_guard<std::mutex> lk(handlersMutex_);
            for (auto &h : handlers_) {
                try { h(ok, msg); } catch (...) {}
            }
        }

        if (ok) {
            // stop loop after successful connect (no auto-monitor here).
            break;
        }

        if (!autoReconnect_) {
            break;
        }

        // wait before retry or until stop requested
        auto waited = std::chrono::milliseconds(0);
        const std::chrono::milliseconds unit(200);
        while (running_.load() && waited < reconnectInterval_) {
            std::this_thread::sleep_for(unit);
            waited += unit;
        }
    }
}

void KohzuManager::setPollAxes(const std::vector<int>& axes) {
    {
        std::lock_guard<std::mutex> lk(pollerMutex_);
        pollAxes_ = axes;
        if (poller_) {
            poller_->setAxes(axes);
        } else {
            // poller will be created on next connectOnce (or you can create it here if connected)
            std::lock_guard<std::mutex> lk2(threadMutex_);
            if (motorCtrl_) {
                poller_ = std::make_unique<Poller>(motorCtrl_, pollAxes_, pollInterval_);
            }
        }
    }
}

StateCache* KohzuManager::getStateCache() {
    std::lock_guard<std::mutex> lk(pollerMutex_);
    if (!poller_) return nullptr;
    return &(poller_->cache());
}

// --- helpers to manage poller lifecycle ---

void KohzuManager::notifyOperationStarted() {
    int prev = activeOperations_.fetch_add(1);
    if (prev == 0) {
        // start poller
        std::lock_guard<std::mutex> lk(pollerMutex_);
        if (poller_) {
            try { poller_->start(); } catch (...) {}
        }
    }
}

void KohzuManager::notifyOperationFinished() {
    int prev = activeOperations_.fetch_sub(1);
    int now = prev - 1;
    if (now <= 0) {
        activeOperations_.store(0);
        // stop poller
        std::lock_guard<std::mutex> lk(pollerMutex_);
        if (poller_) {
            try { poller_->stop(); } catch (...) {}
        }
    }
}

// helper: request a single RDP and STR and update the StateCache; when both done, invoke finalDone()
static void dispatchFinalReads(std::shared_ptr<MotorController> ctrl,
                               std::unique_ptr<Poller>& pollerPtr,
                               int axis,
                               std::function<void()> finalDone)
{
    // shared counter for two reads
    auto remaining = std::make_shared<std::atomic<int>>(2);

    // If pollerPtr (and thus cache) is null, we will still call finalDone when both attempts complete or fail.
    auto cachePtr = (pollerPtr ? &pollerPtr->cache() : nullptr);

    // RDP callback
    auto rdpCb = [axis, cachePtr, remaining, finalDone](const kp::Response& resp, std::exception_ptr ep) {
        if (!ep) {
            // parse position and update cache
            if (!resp.params.empty()) {
                try {
                    std::int64_t pos = std::stoll(resp.params[0]);
                    if (cachePtr) cachePtr->updatePosition(axis, pos, resp.raw);
                } catch (...) {
                    // ignore parse error
                }
            }
        }
        // decrement and check
        if (remaining->fetch_sub(1) == 1) {
            // last one
            try { finalDone(); } catch (...) {}
        }
    };

    // STR callback
    auto strCb = [axis, cachePtr, remaining, finalDone](const kp::Response& resp, std::exception_ptr ep) {
        if (!ep) {
            if (!resp.params.empty()) {
                try {
                    int drv = std::stoi(resp.params[0]);
                    bool running = (drv != 0);
                    if (cachePtr) cachePtr->updateRunning(axis, running, resp.raw);
                } catch (...) {
                    // ignore parse error
                }
            }
        }
        if (remaining->fetch_sub(1) == 1) {
            try { finalDone(); } catch (...) {}
        }
    };

    // send both reads (non-blocking). If sendAsync throws synchronously, decrement appropriate counter.
    try {
        ctrl->sendAsync("RDP", { std::to_string(axis) }, rdpCb);
    } catch (...) {
        if (remaining->fetch_sub(1) == 1) {
            try { finalDone(); } catch (...) {}
        }
    }

    try {
        ctrl->sendAsync("STR", { std::to_string(axis) }, strCb);
    } catch (...) {
        if (remaining->fetch_sub(1) == 1) {
            try { finalDone(); } catch (...) {}
        }
    }
}

// ---- Actions ----

bool KohzuManager::moveAbsoluteAsync(int axis,
                                     std::int64_t absolutePosition,
                                     int speedTable,
                                     int responseMethod,
                                     ActionCallback cb)
{
    if (!isConnected()) {
        if (cb) {
            try { cb(kp::Response(), std::make_exception_ptr(std::runtime_error("Not connected"))); } catch(...) {}
        }
        return false;
    }

    auto ctrl = getMotorController();
    if (!ctrl) {
        if (cb) {
            try { cb(kp::Response(), std::make_exception_ptr(std::runtime_error("MotorController not available"))); } catch(...) {}
        }
        return false;
    }

    std::vector<std::string> params;
    params.push_back(std::to_string(axis));                        // a
    params.push_back(std::to_string(speedTable));                  // b
    params.push_back(std::to_string(absolutePosition));            // c
    params.push_back(std::to_string(responseMethod));              // d

    try {
        // For ANY movement command, start the poller and mark operation active.
        notifyOperationStarted();

        // Immediately set running=true in cache so UI can show motion started instantly.
        {
            std::lock_guard<std::mutex> lk(pollerMutex_);
            if (poller_) {
                poller_->cache().updateRunning(axis, true, "cmd-started");
            }
        }

        // We'll wrap user's callback so that:
        //  - we invoke user's cb immediately (if provided)
        //  - then dispatch final reads (RDP & STR) to ensure StateCache final values are stored
        //  - after final reads complete, we call notifyOperationFinished()
        ActionCallback wrapped = [this, axis, cb](const kp::Response& resp, std::exception_ptr ep) {
            // 1) call user's callback right away
            if (cb) {
                try { cb(resp, ep); } catch (...) {}
            }

            // 2) dispatch final reads (RDP + STR) and when they both complete, finish operation (decrement counter)
            auto ctrlLocal = this->getMotorController(); // copy shared_ptr
            std::unique_lock<std::mutex> lk(this->pollerMutex_);
            // capture poller copy (non-owning pointer via unique_ptr reference)
            std::unique_ptr<Poller>& pollerRef = this->poller_;
            // define finalDone
            auto finalDone = [this]() {
                try { this->notifyOperationFinished(); } catch (...) {}
            };
            // release lock before async calls to avoid deadlocks
            lk.unlock();

            if (ctrlLocal) {
                dispatchFinalReads(ctrlLocal, pollerRef, axis, finalDone);
            } else {
                // no controller - just finish
                try { this->notifyOperationFinished(); } catch(...) {}
            }
        };

        // send command with wrapped callback
        ctrl->sendAsync("APS", params, std::move(wrapped));
        return true;
    } catch (const std::exception& e) {
        // if send failed synchronously, revert operation started
        try { notifyOperationFinished(); } catch(...) {}
        if (cb) {
            try { cb(kp::Response(), std::make_exception_ptr(e)); } catch(...) {}
        }
        return false;
    }
}

bool KohzuManager::moveRelativeAsync(int axis,
                                     std::int64_t delta,
                                     int speedTable,
                                     int responseMethod,
                                     ActionCallback cb)
{
    if (!isConnected()) {
        if (cb) {
            try { cb(kp::Response(), std::make_exception_ptr(std::runtime_error("Not connected"))); } catch(...) {}
        }
        return false;
    }

    auto ctrl = getMotorController();
    if (!ctrl) {
        if (cb) {
            try { cb(kp::Response(), std::make_exception_ptr(std::runtime_error("MotorController not available"))); } catch(...) {}
        }
        return false;
    }

    std::vector<std::string> params;
    params.push_back(std::to_string(axis));               // a
    params.push_back(std::to_string(speedTable));         // b
    params.push_back(std::to_string(delta));              // c
    params.push_back(std::to_string(responseMethod));     // d

    try {
        notifyOperationStarted();

        // Immediately mark running=true in cache
        {
            std::lock_guard<std::mutex> lk(pollerMutex_);
            if (poller_) {
                poller_->cache().updateRunning(axis, true, "cmd-started");
            }
        }

        ActionCallback wrapped = [this, axis, cb](const kp::Response& resp, std::exception_ptr ep) {
            if (cb) {
                try { cb(resp, ep); } catch (...) {}
            }

            auto ctrlLocal = this->getMotorController();
            std::unique_lock<std::mutex> lk(this->pollerMutex_);
            std::unique_ptr<Poller>& pollerRef = this->poller_;
            auto finalDone = [this]() {
                try { this->notifyOperationFinished(); } catch (...) {}
            };
            lk.unlock();

            if (ctrlLocal) {
                dispatchFinalReads(ctrlLocal, pollerRef, axis, finalDone);
            } else {
                try { this->notifyOperationFinished(); } catch(...) {}
            }
        };

        ctrl->sendAsync("RPS", params, std::move(wrapped));
        return true;
    } catch (const std::exception& e) {
        try { notifyOperationFinished(); } catch(...) {}
        if (cb) {
            try { cb(kp::Response(), std::make_exception_ptr(e)); } catch(...) {}
        }
        return false;
    }
}

