// src/controller/KohzuManager.cpp
#include "KohzuManager.hpp"

#include <iostream>
#include <chrono>
#include <thread>
#include <system_error>

namespace kohzu::controller {

KohzuManager::KohzuManager(const std::string& host,
                           uint16_t port,
                           bool autoReconnect,
                           ms reconnectInterval,
                           ms pollInterval,
                           ms fastPollInterval)
    : host_(host),
      port_(port),
      autoReconnect_(autoReconnect),
      reconnectInterval_(reconnectInterval),
      pollInterval_(pollInterval),
      fastPollInterval_(fastPollInterval),
      tcpClient_(nullptr),
      dispatcher_(nullptr),
      controller_(nullptr),
      cache_(std::make_shared<StateCache>()),
      poller_(nullptr),
      running_(false),
      stopRequested_(false) {}

KohzuManager::~KohzuManager() {
    stop();
}

void KohzuManager::startAsync() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (running_.load()) return;
    running_.store(true);
    stopRequested_.store(false);

    // Start reconnection thread
    reconThread_ = std::thread(&KohzuManager::reconnectionLoop, this);
}

void KohzuManager::stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_.load() && !controller_) {
            // nothing to stop
        }
        stopRequested_.store(true);
        running_.store(false);
    }

    if (reconThread_.joinable()) {
        reconThread_.join();
    }

    // teardown controller/poller
    teardown();
}

bool KohzuManager::connectOnce() {
    std::lock_guard<std::mutex> lk(mtx_);
    try {
        // create fresh tcp client and dispatcher
        tcpClient_ = std::make_shared<kohzu::comm::AsioTcpClient>();
        dispatcher_ = std::make_shared<kohzu::protocol::Dispatcher>();

        // create motor controller
        controller_ = std::make_shared<MotorController>(tcpClient_, dispatcher_);

        // create poller using shared cache and controller
        poller_ = std::make_shared<Poller>(controller_, cache_, std::vector<int>{}, pollInterval_, fastPollInterval_);

        // register operation callbacks: manager uses poller to track active axes
        controller_->registerOperationCallbacks(
            [this](int axis) { this->notifyOperationStarted(axis); },
            [this](int axis) { this->notifyOperationFinished(axis); }
        );

        // connect tcp client (synchronous)
        tcpClient_->connect(host_, port_);
        // start io thread and register callback through controller->start
        tcpClient_->start();

        // start controller (creates writer, registers recv handler, start cb worker)
        controller_->start();

        // register spontaneous handler(s) that manager might have (none yet)
        // poller start
        poller_->start();

        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[KohzuManager] connectOnce failed: " << ex.what() << std::endl;
        // cleanup partials
        try { teardown(); } catch (...) {}
        return false;
    } catch (...) {
        std::cerr << "[KohzuManager] connectOnce unknown error\n";
        try { teardown(); } catch (...) {}
        return false;
    }
}

bool KohzuManager::isRunning() const noexcept {
    return running_.load();
}

bool KohzuManager::isConnected() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return controller_ ? controller_->isConnected() : false;
}

void KohzuManager::reconnectionLoop() {
    // If autoReconnect_ is false, attempt a single connect and return
    if (!autoReconnect_) {
        bool ok = connectOnce();
        if (!ok) {
            std::cerr << "[KohzuManager] single connectOnce failed and autoReconnect disabled\n";
        }
        running_.store(false);
        return;
    }

    // autoReconnect loop
    while (!stopRequested_.load()) {
        bool ok = connectOnce();
        if (ok) {
            // connected; wait until disconnected or stopRequested_
            std::cerr << "[KohzuManager] connected successfully\n";
            // Poll connection; exit loop if stopRequested_ set
            while (!stopRequested_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (!controller_ || !controller_->isConnected()) {
                    std::cerr << "[KohzuManager] detected disconnection, will attempt reconnect\n";
                    // teardown existing broken resources and break to outer loop
                    teardown();
                    break;
                }
            }
            if (stopRequested_.load()) break;
        } else {
            // failed to connect; wait and retry
            std::this_thread::sleep_for(reconnectInterval_);
        }
    }

    running_.store(false);
}

void KohzuManager::teardown() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (poller_) {
        try { poller_->stop(); } catch (...) {}
        poller_.reset();
    }
    if (controller_) {
        try { controller_->stop(); } catch (...) {}
        controller_.reset();
    }
    if (tcpClient_) {
        try { tcpClient_->stop(); tcpClient_->disconnect(); } catch (...) {}
        tcpClient_.reset();
    }
    if (dispatcher_) {
        try { dispatcher_.reset(); } catch (...) {}
    }
    // cache_ remains (we keep last known state), do not clear by default
}

void KohzuManager::setupControllerAndPoller() {
    // convenience if needed (not used in current connectOnce which already sets up)
}

void KohzuManager::moveAbsoluteAsync(int axis, long position, AsyncCallback cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!controller_) {
        if (cb) {
            cb({}, std::make_exception_ptr(std::runtime_error("controller not connected")));
        }
        return;
    }
    // Command format: APS <axis> <position> (params order depends on device protocol)
    std::vector<std::string> params;
    params.push_back(std::to_string(axis));
    params.push_back(std::to_string(position));
    // The MotorController will call onOperationStart if configured for movement cmds
    controller_->sendAsync("APS", params, cb);
}

void KohzuManager::registerSpontaneousHandler(SpontaneousHandler h) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (controller_) {
        controller_->registerSpontaneousHandler(std::move(h));
    } else {
        // If controller not yet created, store handler via dispatcher creation later.
        // Simpler approach: create dispatcher now and register; but we keep it simple:
        // create a temporary dispatcher to hold the handler until controller creation.
        if (!dispatcher_) {
            dispatcher_ = std::make_shared<kohzu::protocol::Dispatcher>();
        }
        dispatcher_->registerSpontaneousHandler(std::move(h));
    }
}

void KohzuManager::setPollAxes(const std::vector<int>& axes) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (poller_) poller_->setAxes(axes);
}

void KohzuManager::addPollAxis(int axis) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (poller_) poller_->addAxis(axis);
}

void KohzuManager::removePollAxis(int axis) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (poller_) poller_->removeAxis(axis);
}

void KohzuManager::notifyOperationStarted(int axis) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (poller_) poller_->notifyOperationStarted(axis);
}

void KohzuManager::notifyOperationFinished(int axis) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (poller_) poller_->notifyOperationFinished(axis);
}

std::unordered_map<int, AxisState> KohzuManager::snapshotState() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (cache_) return cache_->snapshot();
    return {};
}

} // namespace kohzu::controller
