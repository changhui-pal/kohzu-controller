// src/controller/Poller.cpp

#include "controller/Poller.hpp"
#include "protocol/Parser.hpp"

#include <chrono>
#include <iostream>
#include <sstream>

using namespace kohzu::controller;
namespace kp = kohzu::protocol;

static std::string axisToStr(int axis) {
    return std::to_string(axis);
}

static std::string makeKey(const std::string& cmd, int axis) {
    return cmd + ":" + std::to_string(axis);
}

Poller::Poller(std::shared_ptr<MotorController> motorCtrl,
               std::vector<int> axes,
               std::chrono::milliseconds interval)
    : motorCtrl_(std::move(motorCtrl)), interval_(interval), axes_(std::move(axes)) {
}

Poller::~Poller() {
    stop();
}

void Poller::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }
    worker_ = std::thread([this]() { run(); });
}

void Poller::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return; // already stopped
    }
    if (worker_.joinable()) worker_.join();

    // clear inflight (best-effort - let futures destruct)
    {
        std::lock_guard<std::mutex> lk(inflightMutex_);
        inflight_.clear();
    }
}

void Poller::setAxes(const std::vector<int>& axes) {
    std::lock_guard<std::mutex> lk(axesMutex_);
    axes_ = axes;
}

void Poller::run() {
    using namespace std::chrono;
    const milliseconds zero(0);

    while (running_.load()) {
        // copy axis list
        std::vector<int> axesCopy;
        {
            std::lock_guard<std::mutex> lk(axesMutex_);
            axesCopy = axes_;
        }

        // 1) Send requests for axes that don't have inflight requests
        for (int axis : axesCopy) {
            // RDP (position)
            {
                std::string key = makeKey("RDP", axis);
                std::lock_guard<std::mutex> lk(inflightMutex_);
                if (inflight_.find(key) == inflight_.end()) {
                    try {
                        // sendAsync returns future; store it
                        auto fut = motorCtrl_->sendAsync("RDP", { axisToStr(axis) });
                        inflight_.emplace(key, std::move(fut));
                    } catch (const std::exception& e) {
                        std::cerr << "Poller: sendAsync RDP failed axis=" << axis << " : " << e.what() << "\n";
                    }
                }
            }

            // STR (status)
            {
                std::string key = makeKey("STR", axis);
                std::lock_guard<std::mutex> lk(inflightMutex_);
                if (inflight_.find(key) == inflight_.end()) {
                    try {
                        auto fut = motorCtrl_->sendAsync("STR", { axisToStr(axis) });
                        inflight_.emplace(key, std::move(fut));
                    } catch (const std::exception& e) {
                        std::cerr << "Poller: sendAsync STR failed axis=" << axis << " : " << e.what() << "\n";
                    }
                }
            }
        }

        // 2) Check inflight futures for readiness (non-blocking) and process
        {
            std::lock_guard<std::mutex> lk(inflightMutex_);
            for (auto it = inflight_.begin(); it != inflight_.end(); ) {
                std::future<kp::Response>& f = it->second;
                if (f.wait_for(zero) == std::future_status::ready) {
                    try {
                        kp::Response resp = f.get();
                        // parse axis from resp.axis (parser already sets it)
                        int axisNum = 0;
                        if (!resp.axis.empty()) {
                            axisNum = std::stoi(resp.axis);
                        }

                        if (resp.cmd == "RDP") {
                            // params[0] contains motor pulse value per manual
                            if (!resp.params.empty()) {
                                try {
                                    std::int64_t pos = std::stoll(resp.params[0]);
                                    cache_.updatePosition(axisNum, pos, resp.raw);
                                } catch (...) {
                                    // ignore parse error
                                }
                            }
                        } else if (resp.cmd == "STR") {
                            // params[0] is driving state: 0 stopped, 1 operating, 2 feedback operating
                            if (!resp.params.empty()) {
                                try {
                                    int drv = std::stoi(resp.params[0]);
                                    bool running = (drv != 0);
                                    cache_.updateRunning(axisNum, running, resp.raw);
                                } catch (...) {
                                    // ignore parse error
                                }
                            }
                        } else {
                            // other responses - ignore here (or could update raw)
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Poller: future.get threw: " << e.what() << "\n";
                    }
                    it = inflight_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // sleep a bit (interval_) before next round
        std::this_thread::sleep_for(interval_);
    }
}

