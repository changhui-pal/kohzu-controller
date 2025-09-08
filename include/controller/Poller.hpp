#pragma once
// include/controller/Poller.hpp

#include "controller/MotorController.hpp"
#include "controller/StateCache.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_map>

namespace kohzu::controller {

class Poller {
public:
    // motorCtrl: shared MotorController (must be connected before start())
    // axes: vector of axis numbers to poll (1..32)
    // interval: poll loop interval (per-axis polling cadence)
    Poller(std::shared_ptr<MotorController> motorCtrl,
           std::vector<int> axes,
           std::chrono::milliseconds interval = std::chrono::milliseconds(100));

    ~Poller();

    // start background polling thread
    void start();

    // stop polling thread (blocks until stopped)
    void stop();

    // change axes at runtime
    void setAxes(const std::vector<int>& axes);

    // read-only access to state cache
    StateCache& cache() { return cache_; }

    // check running state
    bool isRunning() const { return running_.load(); }

private:
    void run(); // worker

    std::shared_ptr<MotorController> motorCtrl_;
    StateCache cache_;
    std::chrono::milliseconds interval_;
    std::vector<int> axes_;
    std::mutex axesMutex_;

    std::thread worker_;
    std::atomic<bool> running_{false};

    // in-flight futures keyed by "CMD:axis" (non-blocking poll of readiness)
    std::mutex inflightMutex_;
    std::unordered_map<std::string, std::future<kohzu::protocol::Response>> inflight_;
};
} // namespace kohzu::controller

