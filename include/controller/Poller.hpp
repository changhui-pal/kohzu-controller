#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <thread>
#include <future>
#include <memory>
#include <chrono>

#include "../protocol/Parser.hpp"

namespace kohzu::controller {

class MotorController; // forward
class StateCache; // forward (assumed to exist)

class Poller {
public:
    using ms = std::chrono::milliseconds;
    Poller(std::shared_ptr<MotorController> motor,
           std::shared_ptr<StateCache> cache,
           const std::vector<int>& axes = {},
           ms pollInterval = ms(500),
           ms fastPollInterval = ms(100));
    ~Poller();

    void start();
    void stop();

    void setAxes(const std::vector<int>& axes);
    void addAxis(int axis);
    void removeAxis(int axis);

    void notifyOperationStarted(int axis);
    void notifyOperationFinished(int axis);

private:
    void runLoop();
    void scheduleRdp(int axis);
    void handleCompletedInflight();

    std::shared_ptr<MotorController> motor_;
    std::shared_ptr<StateCache> cache_;

    std::vector<int> axesOrder_;
    std::mutex axesMtx_;

    ms pollInterval_;
    ms fastPollInterval_;

    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool running_{false};

    // inflight: axis -> shared_future<Response>
    std::unordered_map<int, std::shared_future<kohzu::protocol::Response>> inflightRdp_;
    std::mutex inflightMtx_;

    std::unordered_set<int> activeAxes_;
    std::mutex activeMtx_;

    std::unordered_map<int, std::chrono::steady_clock::time_point> lastPolled_;
};
} // namespace kohzu::controller
