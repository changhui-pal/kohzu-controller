// src/controller/Poller.cpp
#include "Poller.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>

namespace kohzu::controller {

Poller::Poller(std::shared_ptr<MotorController> motor,
               std::shared_ptr<StateCache> cache,
               const std::vector<int>& axes,
               ms pollInterval,
               ms fastPollInterval)
    : motor_(std::move(motor)),
      cache_(std::move(cache)),
      axesOrder_(axes),
      pollInterval_(pollInterval),
      fastPollInterval_(fastPollInterval) {
    // initialize lastPolled_ timestamps to epoch 0
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(axesMtx_);
    for (int a : axesOrder_) lastPolled_[a] = now - pollInterval_;
}

Poller::~Poller() {
    stop();
}

void Poller::start() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (running_) return;
    running_ = true;
    worker_ = std::thread(&Poller::runLoop, this);
}

void Poller::stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_) return;
        running_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();

    // clear inflight futures map
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        inflightRdp_.clear();
    }
}

void Poller::setAxes(const std::vector<int>& axes) {
    std::lock_guard<std::mutex> lk(axesMtx_);
    axesOrder_ = axes;
    auto now = std::chrono::steady_clock::now();
    for (int a : axesOrder_) {
        if (lastPolled_.find(a) == lastPolled_.end()) lastPolled_[a] = now - pollInterval_;
    }
}

void Poller::addAxis(int axis) {
    std::lock_guard<std::mutex> lk(axesMtx_);
    if (std::find(axesOrder_.begin(), axesOrder_.end(), axis) == axesOrder_.end()) {
        axesOrder_.push_back(axis);
        lastPolled_[axis] = std::chrono::steady_clock::now() - pollInterval_;
    }
}

void Poller::removeAxis(int axis) {
    {
        std::lock_guard<std::mutex> lk(axesMtx_);
        axesOrder_.erase(std::remove(axesOrder_.begin(), axesOrder_.end(), axis), axesOrder_.end());
        lastPolled_.erase(axis);
    }
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        inflightRdp_.erase(axis);
    }
    {
        std::lock_guard<std::mutex> lk(activeMtx_);
        activeAxes_.erase(axis);
    }
}

void Poller::notifyOperationStarted(int axis) {
    {
        std::lock_guard<std::mutex> lk(activeMtx_);
        activeAxes_.insert(axis);
    }
    // schedule an immediate RDP for start position
    scheduleRdp(axis);
    cv_.notify_all();
}

void Poller::notifyOperationFinished(int axis) {
    {
        std::lock_guard<std::mutex> lk(activeMtx_);
        activeAxes_.erase(axis);
    }

    // perform final synchronous reads for guaranteed final position/status
    try {
        // RDP: absolute position
        auto rdpResp = motor_->sendSync("RDP", { std::to_string(axis) }, std::chrono::milliseconds(5000));
        if (rdpResp.valid && !rdpResp.params.empty()) {
            try {
                long pos = std::stol(rdpResp.params[0]);
                cache_->updatePosition(axis, pos);
            } catch (...) {
                // store raw if cannot parse
                cache_->updateRaw(axis, rdpResp.raw);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Poller] notifyOperationFinished: RDP failed for axis " << axis << " : " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Poller] notifyOperationFinished: RDP unknown error for axis " << axis << std::endl;
    }

    try {
        // STR: status (running flag etc.)
        auto strResp = motor_->sendSync("STR", { std::to_string(axis) }, std::chrono::milliseconds(2000));
        if (strResp.valid && !strResp.params.empty()) {
            // interpret first param as running flag '1' or '0' if applicable
            bool running = false;
            try {
                running = (std::stol(strResp.params[0]) != 0);
            } catch (...) {
                // leave running as false if parse fails
            }
            cache_->updateRunning(axis, running);
            cache_->updateRaw(axis, strResp.raw);
        }
    } catch (const std::exception& e) {
        std::cerr << "[Poller] notifyOperationFinished: STR failed for axis " << axis << " : " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Poller] notifyOperationFinished: STR unknown error for axis " << axis << std::endl;
    }

    // ensure we don't leave any inflight RDP for this axis
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        inflightRdp_.erase(axis);
    }
}

void Poller::scheduleRdp(int axis) {
    // if already inflight, skip
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        if (inflightRdp_.count(axis)) return;
    }

    try {
        // sendAsync returns future<Response>
        auto fut = motor_->sendAsync("RDP", { std::to_string(axis) });
        std::lock_guard<std::mutex> lk(inflightMtx_);
        inflightRdp_.emplace(axis, std::move(fut));
    } catch (const std::exception& e) {
        std::cerr << "[Poller] scheduleRdp: sendAsync failed for axis " << axis << " : " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Poller] scheduleRdp: unknown error for axis " << axis << std::endl;
    }
}

void Poller::handleCompletedInflight() {
    std::vector<int> finished;
    // snapshot keys to check
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        for (auto &kv : inflightRdp_) {
            int axis = kv.first;
            auto &fut = kv.second;
            if (fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                finished.push_back(axis);
            }
        }
    }

    for (int axis : finished) {
        std::future<kohzu::protocol::Response> fut;
        {
            std::lock_guard<std::mutex> lk(inflightMtx_);
            auto it = inflightRdp_.find(axis);
            if (it == inflightRdp_.end()) continue;
            fut = std::move(it->second);
            inflightRdp_.erase(it);
        }

        try {
            auto resp = fut.get(); // may throw if set_exception
            if (!resp.valid) {
                std::cerr << "[Poller] invalid RDP response axis " << axis << " raw=" << resp.raw << std::endl;
                continue;
            }
            // RDP expected param[0] = position
            if (!resp.params.empty()) {
                try {
                    long pos = std::stol(resp.params[0]);
                    cache_->updatePosition(axis, pos);
                } catch (...) {
                    cache_->updateRaw(axis, resp.raw);
                }
            } else {
                cache_->updateRaw(axis, resp.raw);
            }
        } catch (const std::exception& e) {
            std::cerr << "[Poller] inflight future.get() exception for axis " << axis << " : " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[Poller] inflight future.get() unknown exception for axis " << axis << std::endl;
        }
    }
}

void Poller::runLoop() {
    auto nextWake = std::chrono::steady_clock::now();
    while (true) {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (!running_) break;
        }

        auto now = std::chrono::steady_clock::now();

        // 1) handle any completed inflight futures
        handleCompletedInflight();

        // 2) schedule new RDPs when due
        std::vector<int> axesCopy;
        {
            std::lock_guard<std::mutex> lk(axesMtx_);
            axesCopy = axesOrder_;
        }

        for (int axis : axesCopy) {
            // determine desired interval for this axis
            bool isActive = false;
            {
                std::lock_guard<std::mutex> lk(activeMtx_);
                isActive = (activeAxes_.find(axis) != activeAxes_.end());
            }
            ms desired = isActive ? fastPollInterval_ : pollInterval_;

            auto lastIt = lastPolled_.find(axis);
            auto last = (lastIt != lastPolled_.end()) ? lastIt->second : (now - desired);
            if (now - last >= desired) {
                // schedule if not already inflight
                {
                    std::lock_guard<std::mutex> lk(inflightMtx_);
                    if (inflightRdp_.count(axis) == 0) {
                        scheduleRdp(axis);
                        // update lastPolled to avoid immediate re-schedule (even if schedule failed we set it)
                        lastPolled_[axis] = now;
                    } else {
                        // if inflight, skip scheduling; lastPolled remains old so we'll retry after interval
                    }
                }
            }
        }

        // sleep a short while (responsiveness tick)
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, std::chrono::milliseconds(50), [this]() { return !running_; });
    }

    // final: handle any remaining inflight
    handleCompletedInflight();
}

} // namespace kohzu::controller
