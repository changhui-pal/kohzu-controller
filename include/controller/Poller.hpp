#pragma once
/**
 * Poller.hpp
 *
 * Poller: 지정된 axis들을 폴링하여 StateCache를 갱신.
 *
 * 사용법 (간단):
 *   auto poller = std::make_shared<Poller>(motorController, stateCache, {1,2,3}, std::chrono::milliseconds(200));
 *   poller->start();
 *   // Manager가 동작 시작 시:
 *   poller->notifyOperationStarted(axis);
 *   // Manager가 동작 종료 시:
 *   poller->notifyOperationFinished(axis);
 *   poller->stop();
 *
 * 주요 동작:
 *  - active axis: fast poll (e.g., 100ms)
 *  - idle axis: slow poll (e.g., pollInterval)
 *  - 각 axis별 inflight 요청을 추적하여 중복 요청을 피함
 *  - notifyOperationFinished()는 동기 final reads (sendSync)로 최종 위치를 보장
 */

#include <chrono>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "../controller/MotorController.hpp"
#include "StateCache.hpp" // 기존 헤더 사용(필요시 조정)

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

    // inflight RDP futures: axis -> future
    std::unordered_map<int, std::future<kohzu::protocol::Response>> inflightRdp_;
    std::mutex inflightMtx_;

    // active axes set (movement in progress)
    std::unordered_set<int> activeAxes_;
    std::mutex activeMtx_;

    // last polled time per axis
    std::unordered_map<int, std::chrono::steady_clock::time_point> lastPolled_;
};

} // namespace kohzu::controller