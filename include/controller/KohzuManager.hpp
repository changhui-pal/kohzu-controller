#pragma once
// include/controller/KohzuManager.hpp

#include "comm/AsioTcpClient.hpp"
#include "controller/MotorController.hpp"
#include "controller/Poller.hpp"
#include "controller/StateCache.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace kohzu::controller {

/*
 KohzuManager
 - 자동 연결 전용 상위 래퍼 (연결, 동작, 실시간 위치 갱신을 위한 Poller/StateCache 관리)
 - Poller/StateCache는 Manager가 생성/소유. Poller는 기본적으로 '정지' 상태로 생성됩니다.
 - 동작 명령(responseMethod == 0 : "when completed")을 발행하면 active operation 카운터를 올리고 Poller를 시작합니다.
 - 해당 동작의 응답(완료 또는 오류)을 받으면 카운터를 내리고, 0이면 Poller를 멈춥니다.
*/
class KohzuManager {
public:
    using ConnectionHandler = std::function<void(bool connected, const std::string& message)>;
    using ActionCallback = std::function<void(const kohzu::protocol::Response&, std::exception_ptr)>;

    KohzuManager(const std::string& host, uint16_t port,
                 bool autoReconnect = false,
                 std::chrono::milliseconds reconnectInterval = std::chrono::milliseconds(5000),
                 std::chrono::milliseconds pollInterval = std::chrono::milliseconds(100));

    ~KohzuManager();

    // Start background connect attempts (returns immediately).
    void startAsync();

    // Stop background operations and disconnect if connected. Blocks until stopped.
    void stop();

    // Single connect attempt (blocking). Returns true on success and sets outMessage.
    bool connectOnce(std::string& outMessage);

    // Query connection state
    bool isConnected() const;

    // Access MotorController (may be nullptr if not connected)
    std::shared_ptr<MotorController> getMotorController() const;

    // Register connection state handler. Handlers are called from manager thread.
    void registerConnectionHandler(ConnectionHandler handler);

    // Set which axes Poller should monitor (can be called before or after connect).
    // If poller already exists, it will be updated with new axes.
    void setPollAxes(const std::vector<int>& axes);

    // Access StateCache (may be null if poller not created yet).
    // Returns nullptr if no poller exists yet.
    StateCache* getStateCache();

    // ---- Action APIs (비동기) ----
    // Move absolute. Returns false immediately if request couldn't be sent (e.g., not connected).
    bool moveAbsoluteAsync(int axis,
                           std::int64_t absolutePosition,
                           int speedTable = 0,
                           int responseMethod = 0, // 0 = when completed (default)
                           ActionCallback cb = nullptr);

    // Move relative.
    bool moveRelativeAsync(int axis,
                           std::int64_t delta,
                           int speedTable = 0,
                           int responseMethod = 0,
                           ActionCallback cb = nullptr);

private:
    void runConnectLoop();

    // helpers to manage poller lifecycle w.r.t active operations
    void notifyOperationStarted();
    void notifyOperationFinished();

    const std::string host_;
    const uint16_t port_;
    const bool autoReconnect_;
    const std::chrono::milliseconds reconnectInterval_;
    const std::chrono::milliseconds pollInterval_;

    // owned instances (created on demand)
    std::shared_ptr<kohzu::comm::AsioTcpClient> tcpClient_;
    std::shared_ptr<MotorController> motorCtrl_;

    // poller / state cache (owned by manager)
    std::unique_ptr<Poller> poller_;
    std::vector<int> pollAxes_;

    // background thread for connect attempts
    mutable std::mutex threadMutex_;
    std::thread connectThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    // connection handler list
    mutable std::mutex handlersMutex_;
    std::vector<ConnectionHandler> handlers_;

    // operation activity counter (when >0, poller should run)
    std::atomic<int> activeOperations_{0};
    std::mutex pollerMutex_; // protects poller_ creation/stop/start

    // connect thread synchronization
    std::mutex connectMutex_;

    // prevent copying
    KohzuManager(const KohzuManager&) = delete;
    KohzuManager& operator=(const KohzuManager&) = delete;
};

} // namespace kohzu::controller

