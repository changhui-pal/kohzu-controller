#pragma once
/**
 * Poller.hpp
 *
 * Poller: 지정된 axis들을 폴링하여 StateCache를 갱신.
 *
 * 주요: inflight 요청을 std::shared_future로 관리하여
 *       여러 스레드(또는 이동/복사 필요 지점)에서 안전하게 사용합니다.
 */

#include <chrono>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <future>

#include "controller/MotorController.hpp"
#include "controller/StateCache.hpp" // 기존 헤더 사용(필요시 조정)

namespace kohzu::controller {

class Poller {
public:
    using ms = std::chrono::milliseconds;
    // motor: MotorController (shared), cache: StateCache (shared)
    Poller(std::shared_ptr<MotorController> motor,
           std::shared_ptr<StateCache> cache,
           const std::vector<int>& axes = {},
           ms pollInterval = ms(500),
           ms fastPollInterval = ms(100));

    ~Poller();

    // start/stop background poller thread
    void start();
    void stop();

    // axis list management (thread-safe)
    void setAxes(const std::vector<int>& axes);
    void addAxis(int axis);
    void removeAxis(int axis);

    // Manager calls these to indicate an operation on an axis started/stopped
    // notifyOperationStarted: add to active axes and trigger immediate read
    // notifyOperationFinished: remove from active axes and perform final synchronous reads (RDP+STR)
    void notifyOperationStarted(int axis);
    void notifyOperationFinished(int axis);

private:
    void runLoop();
    void scheduleRdp(int axis); // send async RDP and store future in inflight
    void handleCompletedInflight(); // check inflight futures and update cache

    std::shared_ptr<MotorController> motor_;
    std::shared_ptr<StateCache> cache_;

    std::vector<int> axesOrder_; // axes to poll (iteration order)
    std::mutex axesMtx_;

    ms pollInterval_;
    ms fastPollInterval_;

    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool running_{false};

    // inflight RDP futures: axis -> shared_future (copyable)
    std::unordered_map<int, std::shared_future<kohzu::protocol::Response>> inflightRdp_;
    std::mutex inflightMtx_;

    // active axes set (movement in progress)
    std::unordered_set<int> activeAxes_;
    std::mutex activeMtx_;

    // last polled time per axis
    std::unordered_map<int, std::chrono::steady_clock::time_point> lastPolled_;
};

} // namespace kohzu::controller
